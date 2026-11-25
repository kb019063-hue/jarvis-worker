#pragma once

// The array-destructuring pull, shared by the tree-walker and the bytecode VM.
//
// Both paths bind `var [a, b] = source` the same way: one cursor, stepped one
// element at a time, closed on the normal way out and closed quietly on the
// abrupt one. It lives here rather than in either interpreter so the two
// cannot drift apart about what `Symbol.iterator` means; the tree-walker
// used to hold the only copy.

#include "litebird/js/JsOps.hpp"
#include "litebird/js/JsString.hpp"
#include "litebird/js/Heap.hpp"   // JsObject: array_elements

namespace litebird {

/// Pulls values out of an iterable one at a time, for array destructuring,
/// which must not consume more of an infinite generator than it binds.
///
/// Which way a source is read is the question every other loop here asks,
/// js_iterates_as_builtin(): an array whose `Symbol.iterator` is, by a plain
/// lookup, still the function this realm put on `Array.prototype` has its
/// elements read directly, and so does a string whose method and `next` are
/// still the installed ones. Everything else is asked for its iterator and
/// the protocol is followed — a replaced method, a subclass, a getter, an
/// object that only has `next`, a value that is not iterable at all (which is
/// the TypeError the language asks for). Before this, an array was recognised
/// by `is_array()` alone, so `var a = [1, 2]; a[Symbol.iterator] = ...` bound
/// the elements and not what the iterator yields, deleting the method off
/// `Array.prototype` did not stop `var [x] = [1]`, and a bare `{next(){}}`
/// was accepted as an iterator. (AGENTS.md, open tasks; Phase 102, finding
/// 108.)
class IterationCursor {
public:
    explicit IterationCursor(const JsValue& source) : m_source(source) {
        if (js_iterates_as_builtin(source)) {
            if (source.type == JsType::String) {
                m_mode = Mode::Units;
                m_text = source.string_val;
            } else {
                m_mode = Mode::Array;
            }
            return;
        }
        JsValue method;
        if (js_iterator_method(source, method)) {
            m_mode = Mode::Protocol;
            m_iterator = method.call_with_this(source, {});
            if (!m_iterator.is_object_like()) {
                js_throw_type_error("Result of the Symbol.iterator method is not an object");
            }
            m_next = m_iterator.get_property("next");
            return;
        }
        // A pattern asks for the iterator and nothing else. An object that only
        // has `next` is not iterable, however much it looks like an iterator:
        // `var [x] = { next() {} }`, and `for (var x of { next() {} })` beside
        // it, are TypeErrors. (IterationCursor used to accept the object, which
        // is the case `iteration.dat` records.)
        js_throw_type_error(js_describe_thrown(source) + " is not iterable");
    }

    /// The next value, or undefined once the source is exhausted.
    JsValue pull(bool for_of = false) {
        m_next_threw = false;
        if (m_done) return JsValue::make_undefined();
        switch (m_mode) {
            case Mode::Array: {
                // The iterator reads the current length each time, including
                // holes beyond the stored elements and changes made by the
                // body between pulls. js_array_get also sees inherited indices.
                const size_t length = static_cast<size_t>(js_to_number(m_source.get_property("length")));
                if (m_index < length) return js_array_get(m_source, m_index++);
                m_done = true;
                return JsValue::make_undefined();
            }
            case Mode::Units: {
                // A string iterator yields code points, and a lone surrogate
                // half is a code point of its own: the pair is only one once
                // both halves are there, so a unit inside one is yielded alone.
                if (m_index >= m_text.length()) {
                    m_done = true;
                    return JsValue::make_undefined();
                }
                size_t take = 1;
                const char16_t unit = m_text.unit_at(m_index);
                if (unit >= 0xD800 && unit <= 0xDBFF && m_index + 1 < m_text.length()) {
                    const char16_t low = m_text.unit_at(m_index + 1);
                    if (low >= 0xDC00 && low <= 0xDFFF) take = 2;
                }
                JsString piece = m_text.substring(m_index, m_index + take);
                m_index += take;
                return JsValue::make_string(piece);
            }
            case Mode::Protocol: {
                // A `next` that throws is *not* an abrupt completion the
                // pattern must close the iterator for: 7.4.8 step 7 leaves the
                // close to the caller only because this step is not reached,
                // so `[ , ] = iterable` with a throwing `next` calls `return`
                // zero times, while the same pattern with a throwing *default*
                // calls it once.
                JsValue step;
                bool completed = js_capture_throw([&] { step = js_call(m_next, m_iterator, {}); }, m_thrown);
                if (!completed) {
                    m_next_threw = true;
                    throw JsThrow{m_thrown};
                }
                if (!step.is_object_like()) {
                    if (for_of) js_throw_type_error("Iterator result " + js_describe_thrown(step) + " is not an object");
                    js_throw_type_error("Iterator result is not an object");
                }
                if (step.get_property("done").is_truthy()) {
                    m_done = true;
                    return JsValue::make_undefined();
                }
                return step.get_property("value");
            }
        }
        return JsValue::make_undefined();
    }

    bool done() const { return m_done; }
    /// An iterator step failed before a for-of body was entered. The loop
    /// does not invoke return() for a failure in next, done, or value.
    void abandon() { m_done = true; }
    /// True when the last `pull()` threw from `next` itself; see pull().
    bool next_threw() const { return m_next_threw; }

    /// Tells an iterator that will not be drained. IteratorClose (ECMA-262
    /// 7.4.11) checks what `return` is and what it gives back, but not a
    /// `return` that is missing or null — and not one that throws, because a
    /// pattern that binds fewer names than the iterator holds has not failed.
    void close() {
        if (m_mode != Mode::Protocol || m_done) return;
        m_done = true;
        JsValue ret = m_iterator.get_property("return");
        if (ret.is_nullish()) return;
        if (!ret.is_function()) js_throw_type_error(js_describe_thrown(ret) + " is not a function");
        JsValue result = ret.call_with_this(m_iterator, {});
        if (!result.is_object_like()) {
            js_throw_type_error("Iterator result " + js_describe_thrown(result) + " is not an object");
        }
    }

    /// IteratorClose (ECMA-262 7.4.11) with an **existing** abrupt completion,
    /// which is the case a pattern that fails part way through is in: the
    /// iterator is told it will not be drained, and whatever that says is
    /// discarded — the pattern's own throw is what the script sees. A throwing
    /// `return`, a `return` that is not callable and a `return` that answers a
    /// primitive are all swallowed here, where `close()` above reports them
    /// (a pattern that simply bound fewer names than the iterator holds has
    /// not failed, so *that* close must not hide a broken `return`).
    void close_quietly() {
        try {
            close();
        } catch (const JsThrow&) {
        } catch (const std::exception& ex) {
            if (js_is_internal_unwind(ex)) throw;
        }
    }

private:
    enum class Mode { Array, Units, Protocol };
    JsValue m_source;
    Mode m_mode{Mode::Array};
    JsString m_text;
    JsValue m_iterator;
    JsValue m_next;
    size_t m_index{0};
    bool m_done{false};
    bool m_next_threw{false};
    JsValue m_thrown;
};

} // namespace litebird
