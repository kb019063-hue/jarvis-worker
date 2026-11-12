#pragma once

// ECMAScript abstract operations shared by the interpreter, the bytecode VM and
// the built-in library: conversions, operators, calls, errors and the
// process-wide intrinsic objects (Object.prototype, Array.prototype, ...).
//
// Keeping one implementation here is what stops the tree-walker and the VM
// from disagreeing about what `==` or `+` means.

#include "litebird/js/JsValue.hpp"
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace litebird {

class JsObject;
class EventLoop;

// ---------------------------------------------------------------------------
// Exceptions
// ---------------------------------------------------------------------------

/// A JavaScript `throw` in flight. `value` is what a `catch` clause receives.
/// Derives from std::exception so host code that only knows about C++
/// exceptions still sees a readable message through what().
class JsThrow : public std::exception {
public:
    explicit JsThrow(JsValue thrown);
    const char* what() const noexcept override { return m_description.c_str(); }
    JsValue value;
private:
    std::string m_description;
};

enum class JsErrorKind { Error, TypeError, RangeError, SyntaxError, ReferenceError, EvalError, URIError };

JsValue js_make_error(JsErrorKind kind, const std::string& message);
[[noreturn]] void js_throw(JsValue value);
[[noreturn]] void js_throw_error(JsErrorKind kind, const std::string& message);
[[noreturn]] void js_throw_type_error(const std::string& message);
[[noreturn]] void js_throw_range_error(const std::string& message);

/// The value a `catch` clause should see for a C++ exception that escaped from
/// native code: the thrown value for JsThrow, otherwise a new Error.
JsValue js_value_from_exception(const std::exception& ex);

/// "TypeError: x is not a function" style text for an uncaught value. Never
/// runs script code, so it is safe to call while reporting an error.
std::string js_describe_thrown(const JsValue& value);

/// Reports an exception that reached the host with nothing left to catch it.
/// Prints "Uncaught <description>" to stderr and counts it; the CLI uses the
/// count to decide its exit status.
void js_report_uncaught(const JsValue& value);
void js_report_uncaught(const std::exception& ex);
int js_uncaught_error_count();

/// Reports a promise that was rejected with nothing listening.
void js_report_unhandled_rejection(const JsValue& reason);

/// True for exceptions the engine uses to unwind its own machinery (tearing
/// down a suspended generator). Host code must let these pass.
bool js_is_internal_unwind(const std::exception& ex);

/// Marks a stretch of host code that has called into script. When the
/// outermost one ends the JavaScript stack is empty again, which is the point
/// where HTML drains the microtask queue ("clean up after running script").
/// Without it, promise callbacks queued by an event listener would wait for
/// the next timer.
class JsScriptScope {
public:
    JsScriptScope();
    ~JsScriptScope();
    JsScriptScope(const JsScriptScope&) = delete;
    JsScriptScope& operator=(const JsScriptScope&) = delete;
private:
    EventLoop& m_loop;
};

/// Runs `body` at a boundary where the host calls into script with nothing
/// above it to catch: an event listener, a timer, a lifecycle callback. An
/// exception is reported as uncaught instead of escaping into host code.
template <typename Body>
void js_run_guarded(Body&& body) {
    JsScriptScope scope;
    try {
        body();
    } catch (const std::exception& ex) {
        if (js_is_internal_unwind(ex)) throw;
        js_report_uncaught(ex);
    }
}

/// addEventListener, removeEventListener or dispatchEvent bound to `target`;
/// undefined for any other name. Shared by every kind of DOM node and by the
/// `document` object.
JsValue js_event_target_method(const std::shared_ptr<class Node>& target, const std::string& name);

/// Calls a script callback from host code; see js_run_guarded.
JsValue js_call_guarded(const JsValue& fn, const JsValue& this_val, const std::vector<JsValue>& args);

/// Runs `body` and turns a JavaScript throw, or an exception from native code,
/// into a value. Returns true when `body` completed normally. Engine-internal
/// unwinds pass through untouched.
template <typename Body>
bool js_capture_throw(Body&& body, JsValue& thrown) {
    try {
        body();
        return true;
    } catch (const JsThrow& error) {
        thrown = error.value;
    } catch (const std::exception& ex) {
        if (js_is_internal_unwind(ex)) throw;
        thrown = js_value_from_exception(ex);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Conversions
// ---------------------------------------------------------------------------

/// Number::toString(): the shortest decimal that round-trips, with the
/// ECMAScript rules for when to switch to exponent notation.
std::string js_number_to_string(double value);
std::string js_number_to_string_radix(double value, int radix);
/// StringToNumber: "" is 0, "0x1f" is 31, anything unparsable is NaN.
double js_string_to_number(std::string_view text);
enum class JsPrimitiveHint { Default, Number, String };
/// ToPrimitive: objects are converted through valueOf()/toString(). The hint
/// picks which is tried first; with Default, dates prefer toString().
JsValue js_to_primitive(const JsValue& value, JsPrimitiveHint hint = JsPrimitiveHint::Default);
/// ToNumber. Unlike JsValue::to_number(), undefined and unparsable strings are NaN.
double js_to_number(const JsValue& value);
/// The primitive held by a wrapper object (`new String("a")`, `Object(5)`), or
/// null when `value` is not one.
const JsValue* js_wrapped_primitive(const JsValue& value);
/// ToObject: wraps a primitive in its wrapper object. Objects pass through;
/// null and undefined throw a TypeError.
JsValue js_to_object(const JsValue& value);
/// ToIntegerOrInfinity: truncates, NaN becomes 0.
double js_to_integer(double value);
int32_t js_to_int32(double value);
uint32_t js_to_uint32(double value);
/// ToString. Throws TypeError for symbols.
std::string js_to_string(const JsValue& value);
/// ToString, as the text of a string value. A value that is a string already
/// passes through with nothing copied, which is what every String.prototype
/// method wants for `this` and for its string arguments.
JsString js_to_js_string(const JsValue& value);
/// ToPropertyKey: the string a value addresses when used as `object[value]`.
std::string js_to_property_key(const JsValue& value);
const char* js_typeof(const JsValue& value);
/// `typeof value` as a string value. The answers are made once and shared.
JsValue js_typeof_value(const JsValue& value);

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------

/// `==` (IsLooselyEqual).
bool js_loose_equals(const JsValue& a, const JsValue& b);
/// SameValueZero: like `===` but NaN equals NaN. Used by includes(), Map, Set.
bool js_same_value_zero(const JsValue& a, const JsValue& b);
/// SameValue: Object.is().
bool js_same_value(const JsValue& a, const JsValue& b);
/// `a < b`. Returns 1 for true, 0 for false and -1 when a NaN is involved.
int js_less_than(const JsValue& a, const JsValue& b);
/// `a + b`: concatenation if either operand is a string after ToPrimitive.
JsValue js_add(const JsValue& a, const JsValue& b);
bool js_instance_of(const JsValue& value, const JsValue& constructor);

// ---------------------------------------------------------------------------
// Calls
// ---------------------------------------------------------------------------

/// Calls `fn` with `this`. Throws TypeError when `fn` is not callable.
JsValue js_call(const JsValue& fn, const JsValue& this_val, const std::vector<JsValue>& args);
/// `new ctor(...args)`. Throws TypeError when `ctor` is not a constructor.
JsValue js_construct(const JsValue& ctor, const std::vector<JsValue>& args);
/// A function that calls `fn` with `this` fixed to `receiver`.
JsValue js_bind_this(const JsValue& fn, const JsValue& receiver);

// ---------------------------------------------------------------------------
// Strings: UTF-16 semantics over UTF-8 storage
// ---------------------------------------------------------------------------

// The text of a string value is a JsString, which knows its length in code
// units and reads one in constant time. These helpers are for text that is
// not in a string value yet: they read all of it on every call.
bool js_is_ascii(std::string_view s);
/// Text on its way out of the engine as bytes: the console, an error report.
/// Half of a surrogate pair standing alone is not a character and has no
/// UTF-8 encoding; it goes out as U+FFFD, as it does from every engine.
std::string js_output_text(std::string text);
std::u16string js_utf8_to_utf16(std::string_view s);
std::string js_utf16_to_utf8(std::u16string_view s);
/// The number of UTF-16 code units in UTF-8 text.
size_t js_string_length(std::string_view s);
/// Appends the UTF-8 encoding of a code point. Lone surrogates are encoded as
/// three bytes so a split surrogate pair survives a round trip.
void js_append_code_point(std::string& out, uint32_t code_point);

// ---------------------------------------------------------------------------
// Iteration
// ---------------------------------------------------------------------------

/// Runs the iteration protocol over `iterable`. `visit` returns false to stop
/// early, in which case the iterator's return() method is called.
void js_iterate(const JsValue& iterable, const std::function<bool(const JsValue&)>& visit);
std::vector<JsValue> js_iterable_to_vector(const JsValue& iterable);
/// The property key used for Symbol.iterator.
extern const char* const kIteratorKey;
extern const char* const kAsyncIteratorKey;

/// Creates a symbol with a fresh unique key and remembers its description so
/// the value can be recovered from the key later.
JsValue js_new_symbol(const std::string& description);
/// A well-known or registered symbol with a fixed key, e.g. "@@iterator".
JsValue js_symbol_for_key(const std::string& key, const std::string& description);
/// The symbol whose property key is `key`, if `key` is a symbol key.
bool js_symbol_from_key(const std::string& key, JsValue& out);

// ---------------------------------------------------------------------------
// Intrinsics
// ---------------------------------------------------------------------------

/// A realm: one complete set of built-in prototypes and constructors.
///
/// Every interpreter owns a realm, so a page that patches Array.prototype does
/// not change it for the next page, and a worker thread never shares mutable
/// built-ins with the page that started it. Code with no interpreter at hand
/// (host bindings, unit tests) asks for the realm that is current on its
/// thread, or falls back to a process-wide default.
struct JsIntrinsics {
    JsObject* object_proto{nullptr};
    JsObject* function_proto{nullptr};
    JsObject* array_proto{nullptr};
    JsObject* string_proto{nullptr};
    JsObject* number_proto{nullptr};
    JsObject* boolean_proto{nullptr};
    JsObject* symbol_proto{nullptr};
    JsObject* iterator_proto{nullptr};
    JsObject* regexp_proto{nullptr};
    JsObject* date_proto{nullptr};
    JsObject* map_proto{nullptr};
    JsObject* set_proto{nullptr};
    JsObject* weakmap_proto{nullptr};
    JsObject* weakset_proto{nullptr};
    JsObject* promise_proto{nullptr};
    JsObject* error_protos[7]{};          // indexed by JsErrorKind

    /// Name/value pairs the interpreter defines in its global scope.
    std::vector<std::pair<std::string, JsValue>> globals;

    /// Allocates the prototype objects. The library itself is added by install().
    JsIntrinsics();
    ~JsIntrinsics();
    JsIntrinsics(const JsIntrinsics&) = delete;
    JsIntrinsics& operator=(const JsIntrinsics&) = delete;

    void add_global(const std::string& name, JsValue value);
    const JsValue* find_global(const std::string& name) const;

    /// Installs the built-in library. Safe to call more than once.
    void install();

    /// The realm objects created on this thread belong to right now.
    static JsIntrinsics& get();
    /// Selects the realm for this thread; nullptr selects the default realm.
    static void set_current(JsIntrinsics* realm);

private:
    std::atomic<bool> m_ready{false};
    bool m_installing{false};
    std::recursive_mutex m_install_mutex;
    std::vector<JsValue> m_roots;         // keeps the prototype cells alive across collections
};

// ---------------------------------------------------------------------------
// Helpers for defining built-ins
// ---------------------------------------------------------------------------

/// A native function carrying `name` and `length` like a script function does.
JsValue js_make_native(const char* name, int length, JsValue::NativeFn fn);
/// Defines a non-enumerable method, as built-in methods are.
void js_define_method(JsObject* object, const char* name, int length, JsValue::NativeFn fn);
void js_define_method(JsValue& object, const char* name, int length, JsValue::NativeFn fn);
/// Defines a non-enumerable data property.
void js_define_hidden(JsObject* object, const std::string& name, JsValue value);
/// Defines a non-enumerable accessor.
void js_define_getter(JsObject* object, const char* name, JsValue::NativeFn getter);
/// An object value wrapping an existing heap cell.
JsValue js_object_value(JsObject* object);
/// Argument `index`, or undefined when the call supplied fewer.
inline const JsValue& js_arg(const std::vector<JsValue>& args, size_t index) {
    static const JsValue undefined_value;
    return index < args.size() ? args[index] : undefined_value;
}

} // namespace litebird
