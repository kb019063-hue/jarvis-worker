// Array, Array.prototype and the iterator objects the built-ins hand out.

#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Heap.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

JsValue self() { return JsValue::current_this(); }

/// The array a method operates on. Real arrays are used in place. Array-likes
/// (`{0: "a", length: 1}`, strings) are copied into a temporary, which is
/// enough for the methods that only read.
JsValue receiver(const char* method, bool mutates = false) {
    JsValue me = self();
    if (me.is_array()) return me;
    if (me.is_nullish()) {
        js_throw_type_error(std::string("Array.prototype.") + method + " called on null or undefined");
    }
    if (mutates) {
        js_throw_type_error(std::string("Array.prototype.") + method + " requires an array");
    }
    std::vector<JsValue> elements;
    if (me.type == JsType::String) {
        const JsString& text = me.string_val;
        elements.reserve(text.length());
        for (size_t i = 0; i < text.length(); ++i) elements.push_back(JsValue::make_string(JsString::from_unit(text.unit_at(i))));
    } else if (me.is_object_like()) {
        double length = js_to_integer(js_to_number(me.get_property("length")));
        for (double i = 0; i < length && i < 4294967295.0; ++i) {
            elements.push_back(me.get_property(js_number_to_string(i)));
        }
    }
    return JsValue::make_array(elements);
}

std::deque<JsValue>& elements_of(const JsValue& array) {
    return array.object_val->array_elements;
}

/// True when `index` of `array` is a hole and must not be visited: absent from
/// the own keys, so the callback methods skip it and the copying methods keep
/// it a hole (`HasProperty` in the standard, which an array answers with
/// `!is_array_hole`).
bool hole_at(const JsValue& array, size_t index) {
    return array.is_array_hole(index);
}

/// `HasProperty(O, k)` as the callback methods ask it: the standard says a hole
/// is not the end of the question, because an index can be inherited from
/// `Array.prototype` (a setter-only accessor there still makes the index one
/// that `every` must visit). `has_property` walks the chain, so asking it is
/// the whole answer; `hole_at` is only right for the *own* half, which the
/// copying methods ask when they are carrying holes forward.
bool has_index(const JsValue& array, size_t index) {
    return array.has_property(std::to_string(index));
}

/// The value `O[k]` reads, holes included: a hole is `undefined` unless the
/// prototype has the index.
JsValue index_value(const JsValue& array, size_t index) {
    return array.get_property(std::to_string(index));
}

/// The array a method that copies reads from: `array` itself, unless one of
/// its holes reads as something, an accessor that `Object.defineProperty` put
/// on the index or an index the prototype has. Then it is a copy in which
/// each such hole is an element, the value `a[i]` reads: what the standard's
/// `HasProperty` and `Get` make of the place for `slice`, `concat` and `flat`,
/// and its `Get` for the methods that give every place a value. A hole that
/// nothing answers is a hole in the copy too. An array that never had a hole
/// is told apart by one pointer.
///
/// The copy has `count` places: the standard takes the length once, and the
/// caller that took it before something else could run (`with` and
/// `toSpliced` ask what their arguments are first) passes it; otherwise it
/// is the length now. A getter that makes the array longer adds nothing to
/// the copy, and a place that is gone by the time it is reached is read like
/// any other that is not there. The places from `skip_from` up to `skip_to`
/// are not read at all (`with` does not read the index it replaces, and
/// `toSpliced` not the ones it drops) and are holes in the copy.
///
/// The array itself is the answer when it has no hole to ask about and no
/// place is missing, or when nothing would answer for one: the caller copies
/// its elements, and makes the copy `count` long when it passed one.
JsValue read_through(const JsValue& array, size_t skip_from = 0, size_t skip_to = 0,
                     size_t count = std::numeric_limits<size_t>::max()) {
    if (count == std::numeric_limits<size_t>::max()) count = elements_of(array).size();
    const bool all_there = !array.object_val->array_holes && count <= elements_of(array).size();
    if (all_there || js_array_holes_read_undefined(array)) return array;
    JsValue copy = JsValue::make_array();
    auto& out = copy.object_val->array_elements;
    for (size_t i = 0; i < count; ++i) {
        if (i >= skip_from && i < skip_to) {
            out.push_back(JsValue::make_undefined());
            copy.set_array_hole(i);
            continue;
        }
        if (i < elements_of(array).size() && !array.is_array_hole(i)) {
            out.push_back(elements_of(array)[i]);
            continue;
        }
        const std::string key = std::to_string(i);
        const bool there = array.has_property(key);
        out.push_back(there ? array.get_property(key) : JsValue::make_undefined());
        if (!there) copy.set_array_hole(i);
    }
    return copy;
}

// Array iteration uses ToLength on the original receiver. In particular,
// Infinity and lengths above 2^53 - 1 must not be cast directly to size_t:
// out-of-range floating-to-integer conversion is undefined in C++.
size_t iteration_length(const JsValue& receiver) {
    const double length = js_to_integer(js_to_number(receiver.get_property("length")));
    if (length <= 0) return 0;
    return static_cast<size_t>(std::min(length, 9007199254740991.0));
}

/// Resolves a relative index argument (negative counts from the end) into
/// [0, length].
size_t relative_index(const JsValue& arg, size_t length, size_t fallback) {
    if (arg.is_undefined()) return fallback;
    double k = js_to_integer(js_to_number(arg));
    if (k < 0) {
        k += static_cast<double>(length);
        return k < 0 ? 0 : static_cast<size_t>(k);
    }
    return k > static_cast<double>(length) ? length : static_cast<size_t>(k);
}

JsValue require_callback(const JsValue& fn, const char* method) {
    if (!fn.is_function()) {
        js_throw_type_error(js_describe_thrown(fn) + " is not a function (in Array.prototype." + method + ")");
    }
    return fn;
}

thread_local std::vector<const JsObject*> t_joining;

std::string join_array(const JsValue& array, const std::string& separator) {
    const JsObject* cell = array.object_val;
    // An array that contains itself joins as an empty string instead of
    // recursing forever.
    if (std::find(t_joining.begin(), t_joining.end(), cell) != t_joining.end()) return std::string();
    t_joining.push_back(cell);
    struct Pop { ~Pop() { t_joining.pop_back(); } } pop;

    std::string out;
    // `join` reads each place as `a[i]` does. A hole reads `undefined` and
    // joins empty, unless the index has an accessor of its own or the
    // prototype has the index; whether any hole can is asked once, and again
    // after anything that may have run a script (`new Array(n + 1).join(s)`
    // is how a string was repeated before `repeat`, and is all holes).
    bool holes_known = false;
    bool holes_empty = false;
    // The length is taken once, as the standard takes it. A place that is no
    // longer there by the time it is reached (a getter or a toString made
    // the array shorter) is read like a hole.
    const size_t length = elements_of(array).size();
    for (size_t i = 0; i < length; ++i) {
        if (i) out += separator;
        JsValue item;
        if (i >= elements_of(array).size() || array.is_array_hole(i)) {
            if (!holes_known) {
                holes_empty = js_array_holes_read_undefined(array);
                holes_known = true;
            }
            if (holes_empty) continue;
            item = js_array_get(array, i);
            holes_known = false;
        } else {
            item = elements_of(array)[i];
        }
        if (item.type == JsType::String) out += item.string_val.view();
        else if (!item.is_nullish()) {
            out += js_to_string(item);
            if (item.is_object_like()) holes_known = false;      // its toString is a script
        }
    }
    return out;
}

/// Stable merge sort. Unlike std::sort it stays memory-safe when a script
/// comparator is inconsistent.
template <typename Less>
void merge_sort(std::vector<JsValue>& items, Less less) {
    if (items.size() < 2) return;
    std::vector<JsValue> buffer(items.size());
    for (size_t width = 1; width < items.size(); width *= 2) {
        for (size_t lo = 0; lo < items.size(); lo += 2 * width) {
            size_t mid = std::min(lo + width, items.size());
            size_t hi = std::min(lo + 2 * width, items.size());
            size_t a = lo, b = mid, out = lo;
            while (a < mid && b < hi) {
                if (less(items[b], items[a])) buffer[out++] = items[b++];
                else buffer[out++] = items[a++];
            }
            while (a < mid) buffer[out++] = items[a++];
            while (b < hi) buffer[out++] = items[b++];
        }
        items.swap(buffer);
    }
}

/// `holes` is carried alongside `items` so that sort can put the holes at the
/// end, as the standard does, without asking the comparator about them.
void sort_values(std::vector<JsValue>& items, std::vector<bool>& holes, const JsValue& comparator) {
    // undefined always sorts to the end and is never passed to the comparator;
    // a hole sorts after it, and is never passed either (the standard's SortCompare
    // is not reached for either, and the holes end up after every element).
    std::vector<JsValue> defined;
    size_t undefined_count = 0;
    size_t hole_count = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (holes[i]) { ++hole_count; continue; }
        if (items[i].is_undefined()) { ++undefined_count; continue; }
        defined.push_back(std::move(items[i]));
    }
    if (comparator.is_function()) {
        merge_sort(defined, [&](const JsValue& a, const JsValue& b) {
            double order = js_to_number(js_call(comparator, JsValue(), {a, b}));
            return order < 0;
        });
    } else {
        // Default order compares the string forms, so [10, 9, 1] sorts as 1, 10, 9.
        // The order is by UTF-16 code unit, like `<`. An element that is a
        // string already lends its text as the key; nothing is copied.
        std::vector<std::pair<JsString, JsValue>> keyed;
        keyed.reserve(defined.size());
        for (auto& item : defined) keyed.emplace_back(js_to_js_string(item), std::move(item));
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {
            return JsString::compare(a.first.view(), b.first.view()) < 0;
        });
        defined.clear();
        for (auto& entry : keyed) defined.push_back(std::move(entry.second));
    }
    items = std::move(defined);
    items.resize(items.size() + undefined_count + hole_count);
    holes.assign(items.size(), false);
    // The holes are the last places of all, after the elements that are undefined.
    for (size_t i = items.size() - hole_count; i < items.size(); ++i) holes[i] = true;
}

/// `holes` says, per place of `out`, whether it is a hole. A hole of the array
/// being flattened is skipped (it has no element to move); a hole of an inner
/// array is a place of the result that is itself a hole — `flat` copies from
/// the inner array, and copying a hole leaves one.
void flatten_into(std::vector<JsValue>& out, std::vector<bool>& holes, const JsValue& array, double depth) {
    js_check_stack();       // [[[[...]]]].flat(Infinity) recurses once per level
    for (size_t i = 0; i < elements_of(array).size(); ++i) {
        if (array.is_array_hole(i)) { if (depth < 1) { out.push_back(JsValue::make_undefined()); holes.push_back(true); } continue; }
        JsValue item = elements_of(array)[i];
        if (depth >= 1 && item.is_array()) flatten_into(out, holes, item, depth - 1);
        else { out.push_back(std::move(item)); holes.push_back(false); }
    }
}

JsValue iteration_result(const JsValue& value, bool done) {
    JsValue result = JsValue::make_object();
    result.set_property("value", value);
    result.set_property("done", JsValue::make_boolean(done));
    return result;
}

} // namespace

namespace {

/// What an iterator keeps: the step, and whether it has finished. The step is a
/// std::function the collector cannot see into, so the values it captured are
/// held separately by trace_extra (see js_make_iterator).
struct IteratorState {
    std::function<bool(JsValue& value)> step;
    bool finished{false};
};

/// The state behind `this`, for the `next` on an iterator's prototype. A
/// `next` called on anything that is not one of these iterators throws, which
/// is what the language says a prototype method does with a wrong receiver.
IteratorState& iterator_state(const JsValue& me) {
    if (!me.is_object_like() || me.object_val->klass != ObjectClass::Iterator || !me.object_val->internal) {
        js_throw_type_error("next method called on an object that is not an iterator");
    }
    return *static_cast<IteratorState*>(me.object_val->internal.get());
}

} // namespace

JsValue js_make_iterator(std::function<bool(JsValue& value)> step, std::vector<JsValue> roots, JsObject* proto) {
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = proto ? proto : JsIntrinsics::get().iterator_proto;
    cell->klass = ObjectClass::Iterator;
    cell->internal = std::make_shared<IteratorState>(IteratorState{std::move(step), false});
    if (!roots.empty()) {
        // `step` holds these inside a std::function, where the collector cannot see them.
        auto kept = std::make_shared<std::vector<JsValue>>(std::move(roots));
        cell->trace_extra = [kept](Heap& heap) {
            if (!heap.first_visit(kept.get())) return;
            for (const auto& value : *kept) heap.mark_value(value);
        };
    }
    return JsValue::make_object(cell);
}

void js_install_iterator_prototype(JsObject* proto, const char* tag) {
    // One `next` for every iterator of the kind: the step is the object's, the
    // function is the prototype's, and replacing the property here changes
    // what every loop over one of these iterators does -- which is why the
    // iteration fast paths ask whether it is still this function.
    js_define_method(proto, "next", 0, [](Args) {
        IteratorState& state = iterator_state(self());
        JsValue value;
        if (!state.finished && state.step(value)) return iteration_result(value, false);
        state.finished = true;
        return iteration_result(JsValue::make_undefined(), true);
    });
    if (tag) {
        auto& entry = proto->properties.entry("@@toStringTag");
        entry.second = JsValue::make_string(tag);
        entry.flags = PropertyMap::Configurable;   // { writable: false, enumerable: false, configurable: true }
    }
}

void install_array_builtins(JsIntrinsics& realm) {
    JsObject* ap = realm.array_proto;

    // Iterators are themselves iterable: %IteratorPrototype% carries @@iterator
    // and nothing else, and each kind of iterator's prototype sits between it
    // and the iterator (see js_install_iterator_prototype).
    js_define_method(realm.iterator_proto, kIteratorKey, 0, [](Args) { return self(); });
    js_define_method(realm.iterator_proto, "Symbol.iterator", 0, [](Args) { return self(); });
    js_install_iterator_prototype(realm.array_iterator_proto, "Array Iterator");

    // ---- Constructor ------------------------------------------------------
    JsValue array_ctor = js_make_native("Array", 1, [](Args args) {
        if (args.size() == 1 && args[0].type == JsType::Number) {
            double length = args[0].number_val;
            if (!(length >= 0) || length != std::floor(length) || length > 4294967295.0) {
                js_throw_range_error("Invalid array length");
            }
            if (length > (1u << 26)) js_throw_range_error("Array length exceeds the supported maximum");
            // `new Array(3)` makes three holes, not three elements that hold undefined.
            JsValue created = JsValue::make_array(std::vector<JsValue>(static_cast<size_t>(length)));
            for (size_t i = 0; i < static_cast<size_t>(length); ++i) created.set_array_hole(i);
            return created;
        }
        return JsValue::make_array(args);
    });
    array_ctor.define_property("prototype", js_object_value(ap), 0);
    js_define_hidden(ap, "constructor", array_ctor);

    js_define_method(array_ctor, "isArray", 1, [](Args args) {
        const JsValue& value = js_arg(args, 0);
        return JsValue::make_boolean(value.is_array() && value.object_val->klass != ObjectClass::Arguments);
    });
    js_define_method(array_ctor, "of", 0, [](Args args) { return JsValue::make_array(args); });
    js_define_method(array_ctor, "from", 1, [](Args args) {
        // Array.from ( items [ , mapfn [ , thisArg ] ] ), in the order of the
        // standard: the mapper is checked, then the source is *asked* for its
        // iterator (GetMethod(@@iterator)), and only a source that has none is
        // read as an array-like. An array and a string are asked like anything
        // else, so deleting the method from `Array.prototype` makes an array
        // array-like and not a `TypeError`, and a number whose prototype has
        // one is iterated (Phase 94).
        const JsValue& source = js_arg(args, 0);
        const JsValue& map_fn = js_arg(args, 1);
        const JsValue& this_arg = js_arg(args, 2);
        const bool mapping = !map_fn.is_undefined();
        if (mapping && !map_fn.is_function()) js_throw_type_error("Array.from: mapper is not a function");

        auto mapped = [&](const JsValue& value, size_t index) {
            if (!mapping) return value;
            return map_fn.call_with_this(this_arg, {value, JsValue::make_number(static_cast<double>(index))});
        };

        std::vector<JsValue> items;
        JsValue method;
        const bool iterable = js_iterator_method(source, method);
        if (iterable) {
            // Mapped as each value arrives, not after the last one: a mapper
            // that throws closes the iterator, and an iterator without an end
            // costs one value, not all of them.
            js_iterate(source, [&](const JsValue& value) {
                items.push_back(mapped(value, items.size()));
                return true;
            });
        } else {
            // ToObject(items), then ToLength(Get(items, "length")), then each
            // index as a property — which a string answers one code unit at a
            // time, and a hole in an array answers as `undefined`.
            if (source.is_nullish()) js_throw_type_error(js_describe_thrown(source) + " is not iterable");
            const double length_number = js_to_number(source.get_property("length"));
            double length = std::isnan(length_number) ? 0.0 : std::floor(length_number);
            if (!(length > 0)) length = 0;
            if (length > 9007199254740991.0) length = 9007199254740991.0; // 2^53 - 1
            if (length > (1u << 26)) js_throw_range_error("Invalid array length");
            for (double i = 0; i < length; ++i) {
                items.push_back(mapped(source.get_property(js_number_to_string(i)), static_cast<size_t>(i)));
            }
        }
        return JsValue::make_array(items);
    });
    realm.add_global("Array", array_ctor);

    // ---- Stack and queue --------------------------------------------------
    js_define_method(ap, "push", 1, [](Args args) {
        JsValue array = receiver("push", true);
        if (array.object_val->frozen) js_throw_type_error("Cannot add property, object is not extensible");
        auto& elements = elements_of(array);
        const size_t start = elements.size();
        elements.insert(elements.end(), args.begin(), args.end());
        for (size_t i = start; i < elements.size(); ++i) array.clear_array_hole(i);
        return JsValue::make_number(static_cast<double>(elements.size()));
    });
    js_define_method(ap, "pop", 0, [](Args) {
        JsValue array = receiver("pop", true);
        auto& elements = elements_of(array);
        if (elements.empty()) return JsValue::make_undefined();
        // `pop` reads the last place as `a[i]` does and then deletes it. For
        // a hole that is `undefined`, unless the index has an accessor of its
        // own or the prototype has the index; and an accessor goes with the
        // place, or it would be there again when the array grew back.
        const size_t last_index = elements.size() - 1;
        if (!array.is_array_hole(last_index)) {
            JsValue last = std::move(elements.back());
            elements.pop_back();
            return last;
        }
        JsValue last = js_array_get(array, last_index);
        // The getter may have changed the array: take the place only if it is still the last.
        if (elements.size() == last_index + 1) {
            if (array.object_val->has_accessors) {
                const std::string key = std::to_string(last_index);
                auto mapped = array.object_val->properties.find(key);
                if (mapped != array.object_val->properties.end()) {
                    if (!(mapped->flags & PropertyMap::Configurable)) {
                        js_throw_type_error("Cannot delete property '" + key + "' of [object Array]");
                    }
                    array.object_val->properties.erase(key);
                }
            }
            elements.pop_back();
        }
        return last;
    });
    js_define_method(ap, "shift", 0, [](Args) {
        JsValue array = receiver("shift", true);
        auto& elements = elements_of(array);
        if (elements.empty()) return JsValue::make_undefined();
        // The first place as `a[0]` reads it (see `pop`).
        JsValue first = array.is_array_hole(0) ? js_array_get(array, 0) : std::move(elements.front());
        if (elements.empty()) return first;       // a getter emptied the array
        elements.erase(elements.begin());
        if (array.object_val->array_holes) array.object_val->array_holes->shift(0, -1);
        return first;
    });
    js_define_method(ap, "unshift", 1, [](Args args) {
        JsValue array = receiver("unshift", true);
        auto& elements = elements_of(array);
        if (array.object_val->array_holes) {
            array.object_val->array_holes->shift(0, static_cast<std::ptrdiff_t>(args.size()));
        }
        elements.insert(elements.begin(), args.begin(), args.end());
        for (size_t i = 0; i < args.size(); ++i) array.clear_array_hole(i);
        return JsValue::make_number(static_cast<double>(elements.size()));
    });

    // ---- Copying and slicing ----------------------------------------------
    js_define_method(ap, "slice", 2, [](Args args) {
        JsValue array = read_through(receiver("slice"));
        const auto& elements = elements_of(array);
        size_t begin = relative_index(js_arg(args, 0), elements.size(), 0);
        size_t end = relative_index(js_arg(args, 1), elements.size(), elements.size());
        if (begin >= end) return JsValue::make_array();
        JsValue result = JsValue::make_array();
        result.object_val->array_elements.assign(elements.begin() + static_cast<std::ptrdiff_t>(begin),
                                                 elements.begin() + static_cast<std::ptrdiff_t>(end));
        for (size_t i = begin; i < end; ++i) {
            if (array.is_array_hole(i)) result.set_array_hole(i - begin);
        }
        return result;
    });
    auto splice_impl = []<typename Seq>(Seq& elements, Args args) {
        size_t start = relative_index(js_arg(args, 0), elements.size(), 0);
        size_t delete_count = 0;
        if (args.size() == 1) {
            delete_count = elements.size() - start;
        } else if (args.size() >= 2) {
            double requested = js_to_integer(js_to_number(args[1]));
            if (requested < 0) requested = 0;
            delete_count = static_cast<size_t>(std::min(requested, static_cast<double>(elements.size() - start)));
        }
        std::vector<JsValue> removed(elements.begin() + static_cast<std::ptrdiff_t>(start),
                                     elements.begin() + static_cast<std::ptrdiff_t>(start + delete_count));
        elements.erase(elements.begin() + static_cast<std::ptrdiff_t>(start),
                       elements.begin() + static_cast<std::ptrdiff_t>(start + delete_count));
        if (args.size() > 2) {
            elements.insert(elements.begin() + static_cast<std::ptrdiff_t>(start), args.begin() + 2, args.end());
        }
        return removed;
    };
    js_define_method(ap, "splice", 2, [splice_impl](Args args) {
        JsValue array = receiver("splice", true);
        if (args.empty()) return JsValue::make_array();
        // Which of the removed places were holes, before the erase moves them.
        std::vector<bool> removed_holes;
        {
            const size_t start = relative_index(js_arg(args, 0), elements_of(array).size(), 0);
            double requested = args.size() >= 2 ? js_to_integer(js_to_number(args[1]))
                                                : static_cast<double>(elements_of(array).size() - start);
            if (requested < 0) requested = 0;
            const size_t count = static_cast<size_t>(
                std::min(requested, static_cast<double>(elements_of(array).size() - start)));
            for (size_t i = 0; i < count; ++i) removed_holes.push_back(array.is_array_hole(start + i));
            const std::ptrdiff_t delta = static_cast<std::ptrdiff_t>(args.size() - 2) -
                                         static_cast<std::ptrdiff_t>(count);
            if (array.object_val->array_holes && delta != 0) {
                array.object_val->array_holes->shift(start, delta);
            }
            for (size_t i = start; i < start + count; ++i) array.clear_array_hole(i);
        }
        JsValue result = JsValue::make_array(splice_impl(elements_of(array), args));
        for (size_t i = 0; i < removed_holes.size() && i < result.object_val->array_elements.size(); ++i) {
            if (removed_holes[i]) result.set_array_hole(i);
        }
        return result;
    });
    js_define_method(ap, "toSpliced", 2, [](Args args) {
        // The length, then where to start and how many to drop, then the
        // places: the standard's order. The places that are dropped are not
        // read, so a getter on one of them is not called; and what the
        // arguments are is asked once, before any place is read.
        JsValue source = receiver("toSpliced");
        const size_t length = elements_of(source).size();
        const size_t start = relative_index(js_arg(args, 0), length, 0);
        size_t dropped = 0;
        if (args.size() == 1) {
            dropped = length - start;
        } else if (args.size() >= 2) {
            double requested = js_to_integer(js_to_number(args[1]));
            if (requested < 0) requested = 0;
            dropped = static_cast<size_t>(std::min(requested, static_cast<double>(length - start)));
        }
        JsValue array = read_through(source, start, start + dropped, length);
        std::vector<JsValue> copy(elements_of(array).begin(), elements_of(array).end());
        copy.resize(length, JsValue::make_undefined());
        copy.erase(copy.begin() + static_cast<std::ptrdiff_t>(start),
                   copy.begin() + static_cast<std::ptrdiff_t>(start + dropped));
        if (args.size() > 2) {
            copy.insert(copy.begin() + static_cast<std::ptrdiff_t>(start), args.begin() + 2, args.end());
        }
        return JsValue::make_array(copy);
    });
    js_define_method(ap, "concat", 1, [](Args args) {
        JsValue array = read_through(receiver("concat"));
        std::vector<JsValue> values(elements_of(array).begin(), elements_of(array).end());
        std::vector<bool> holes;
        for (size_t i = 0; i < values.size(); ++i) holes.push_back(array.is_array_hole(i));
        for (const auto& given : args) {
            if (given.is_array()) {
                const JsValue arg = read_through(given);
                for (size_t i = 0; i < elements_of(arg).size(); ++i) {
                    values.push_back(elements_of(arg)[i]);
                    holes.push_back(arg.is_array_hole(i));
                }
            } else {
                const JsValue& arg = given;
                values.push_back(arg);
                holes.push_back(false);
            }
        }
        JsValue result = JsValue::make_array(values);
        for (size_t i = 0; i < holes.size(); ++i) if (holes[i]) result.set_array_hole(i);
        return result;
    });
    js_define_method(ap, "join", 1, [](Args args) {
        JsValue array = receiver("join");
        std::string separator = js_arg(args, 0).is_undefined() ? "," : js_to_string(js_arg(args, 0));
        return JsValue::make_string(join_array(array, separator));
    });
    js_define_method(ap, "toString", 0, [](Args) {
        JsValue me = self();
        if (!me.is_array()) return JsValue::make_string("[object Object]");
        return JsValue::make_string(join_array(me, ","));
    });
    js_define_method(ap, "toLocaleString", 0, [](Args) {
        return JsValue::make_string(join_array(receiver("toLocaleString"), ","));
    });
    js_define_method(ap, "reverse", 0, [](Args) {
        JsValue array = receiver("reverse", true);
        std::reverse(elements_of(array).begin(), elements_of(array).end());
        if (array.object_val->array_holes) {
            auto& bits = array.object_val->array_holes->bits;
            const size_t total = elements_of(array).size();
            std::vector<bool> before;
            for (size_t i = 0; i < total; ++i) before.push_back(array.is_array_hole(i));
            for (size_t i = 0; i < total; ++i) {
                if (before[total - 1 - i]) array.set_array_hole(i);
                else array.clear_array_hole(i);
            }
            (void)bits;
        }
        return array;
    });
    js_define_method(ap, "toReversed", 0, [](Args) {
        JsValue array = read_through(receiver("toReversed"));
        std::vector<JsValue> copy(elements_of(array).begin(), elements_of(array).end());
        const size_t total = copy.size();
        std::vector<bool> holes;
        for (size_t i = 0; i < total; ++i) holes.push_back(array.is_array_hole(i));
        std::reverse(copy.begin(), copy.end());
        JsValue result = JsValue::make_array(copy);
        for (size_t i = 0; i < total; ++i) if (holes[total - 1 - i]) result.set_array_hole(i);
        return result;
    });
    js_define_method(ap, "sort", 1, [](Args args) {
        JsValue array = receiver("sort", true);
        const JsValue& comparator = js_arg(args, 0);
        if (!comparator.is_undefined() && !comparator.is_function()) {
            js_throw_type_error("The comparison function must be either a function or undefined");
        }
        // Sort a copy: the comparator is script and may touch the array.
        std::vector<JsValue> copy(elements_of(array).begin(), elements_of(array).end());
        std::vector<bool> holes;
        for (size_t i = 0; i < copy.size(); ++i) holes.push_back(array.is_array_hole(i));
        sort_values(copy, holes, comparator);
        elements_of(array).assign(copy.begin(), copy.end());
        for (size_t i = 0; i < holes.size(); ++i) {
            if (holes[i]) array.set_array_hole(i);
            else array.clear_array_hole(i);
        }
        return array;
    });
    js_define_method(ap, "toSorted", 1, [](Args args) {
        JsValue array = read_through(receiver("toSorted"));
        std::vector<JsValue> copy(elements_of(array).begin(), elements_of(array).end());
        std::vector<bool> holes;
        for (size_t i = 0; i < copy.size(); ++i) holes.push_back(array.is_array_hole(i));
        sort_values(copy, holes, js_arg(args, 0));
        JsValue result = JsValue::make_array(copy);
        for (size_t i = 0; i < holes.size(); ++i) if (holes[i]) result.set_array_hole(i);
        return result;
    });
    js_define_method(ap, "with", 2, [](Args args) {
        // The length, then the index, then the places: the standard's order,
        // and the index that is replaced is not read, so a getter on it is
        // not called. Turning the index into a number can run a script that
        // changes the array, and the answer still has the length taken first.
        JsValue source = receiver("with");
        const size_t length = elements_of(source).size();
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0) index += static_cast<double>(length);
        if (index < 0 || index >= static_cast<double>(length)) js_throw_range_error("Invalid index");
        const size_t at = static_cast<size_t>(index);
        JsValue array = read_through(source, at, at + 1, length);
        std::vector<JsValue> copy(elements_of(array).begin(), elements_of(array).end());
        copy.resize(length, JsValue::make_undefined());
        copy[at] = js_arg(args, 1);
        return JsValue::make_array(copy);
    });
    js_define_method(ap, "fill", 1, [](Args args) {
        JsValue array = receiver("fill", true);
        auto& elements = elements_of(array);
        size_t begin = relative_index(js_arg(args, 1), elements.size(), 0);
        size_t end = relative_index(js_arg(args, 2), elements.size(), elements.size());
        for (size_t i = begin; i < end; ++i) {
            elements[i] = js_arg(args, 0);
            array.clear_array_hole(i);           // fill writes every place it covers
        }
        return array;
    });
    js_define_method(ap, "copyWithin", 2, [](Args args) {
        JsValue array = receiver("copyWithin", true);
        auto& elements = elements_of(array);
        size_t target = relative_index(js_arg(args, 0), elements.size(), 0);
        size_t begin = relative_index(js_arg(args, 1), elements.size(), 0);
        size_t end = relative_index(js_arg(args, 2), elements.size(), elements.size());
        if (begin >= end || target >= elements.size()) return array;
        std::vector<JsValue> chunk(elements.begin() + static_cast<std::ptrdiff_t>(begin),
                                   elements.begin() + static_cast<std::ptrdiff_t>(end));
        std::vector<bool> chunk_holes;
        for (size_t i = begin; i < end; ++i) chunk_holes.push_back(array.is_array_hole(i));
        for (size_t i = 0; i < chunk.size() && target + i < elements.size(); ++i) {
            elements[target + i] = chunk[i];
            // Copying a hole leaves a hole at the target (the standard calls
            // DeletePropertyOrThrow for it).
            if (chunk_holes[i]) array.set_array_hole(target + i);
            else array.clear_array_hole(target + i);
        }
        return array;
    });
    js_define_method(ap, "flat", 0, [](Args args) {
        JsValue array = read_through(receiver("flat"));
        double depth = js_arg(args, 0).is_undefined() ? 1 : js_to_integer(js_to_number(js_arg(args, 0)));
        std::vector<JsValue> result;
        std::vector<bool> holes;
        flatten_into(result, holes, array, depth);
        JsValue flat = JsValue::make_array(result);
        for (size_t i = 0; i < holes.size(); ++i) if (holes[i]) flat.set_array_hole(i);
        return flat;
    });
    js_define_method(ap, "at", 1, [](Args args) {
        JsValue array = receiver("at");
        const auto& elements = elements_of(array);
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0) index += static_cast<double>(elements.size());
        if (index < 0 || index >= static_cast<double>(elements.size())) return JsValue::make_undefined();
        return js_array_get(array, static_cast<size_t>(index));
    });

    // ---- Searching --------------------------------------------------------
    js_define_method(ap, "indexOf", 1, [](Args args) {
        JsValue array = receiver("indexOf");
        const auto& elements = elements_of(array);
        // The length is taken once: reading an index can call a getter now,
        // and an element it adds is not one indexOf looks at.
        const size_t length = elements.size();
        for (size_t i = relative_index(js_arg(args, 1), length, 0); i < length; ++i) {
            if (!has_index(array, i)) continue;            // indexOf asks HasProperty first
            if (strict_equals(index_value(array, i), js_arg(args, 0))) return JsValue::make_number(static_cast<double>(i));
        }
        return JsValue::make_number(-1);
    });
    js_define_method(ap, "lastIndexOf", 1, [](Args args) {
        JsValue array = receiver("lastIndexOf");
        const auto& elements = elements_of(array);
        if (elements.empty()) return JsValue::make_number(-1);
        double from = args.size() > 1 ? js_to_integer(js_to_number(args[1])) : static_cast<double>(elements.size()) - 1;
        if (from < 0) from += static_cast<double>(elements.size());
        if (from < 0) return JsValue::make_number(-1);
        size_t start = static_cast<size_t>(std::min(from, static_cast<double>(elements.size()) - 1));
        for (size_t i = start + 1; i-- > 0;) {
            if (!has_index(array, i)) continue;
            if (strict_equals(index_value(array, i), js_arg(args, 0))) return JsValue::make_number(static_cast<double>(i));
        }
        return JsValue::make_number(-1);
    });
    js_define_method(ap, "includes", 1, [](Args args) {
        JsValue array = receiver("includes");
        const auto& elements = elements_of(array);
        // Unlike indexOf, includes does not ask for the property: it reads
        // the place as `a[i]` does. A hole is `undefined` to it, unless the
        // index has an accessor of its own or the prototype has the index,
        // which is asked once: nothing in the loop runs a script if no hole
        // can. It is asked after the index to start from, because turning
        // that into a number can run one; the length is taken before it, as
        // the standard takes it, and a place that is gone reads `undefined`.
        const size_t length = elements.size();
        const size_t start = relative_index(js_arg(args, 1), length, 0);
        const bool plain = js_array_holes_read_undefined(array);
        for (size_t i = start; i < length; ++i) {
            if (js_same_value_zero(plain ? array.array_element(i) : js_array_get(array, i), js_arg(args, 0))) {
                return JsValue::make_boolean(true);
            }
        }
        return JsValue::make_boolean(false);
    });

    // ---- Iteration with a callback ----------------------------------------
    //
    // The element vector can be replaced by the callback, so every step
    // re-reads it and bounds-checks against the current length.
    enum class Find { Value, Index };
    auto make_find = [](const char* name, bool from_end, Find what) {
        return [name, from_end, what](Args args) {
            JsValue array = receiver(name);
            JsValue callback = require_callback(js_arg(args, 0), name);
            const JsValue this_arg = js_arg(args, 1);
            size_t length = elements_of(array).size();
            for (size_t step = 0; step < length; ++step) {
                size_t i = from_end ? length - 1 - step : step;
                if (i >= elements_of(array).size()) continue;
                // `find` and its relatives are the one family that does *not*
                // skip a hole: the standard reads `kValue = O[k]` for every
                // index and calls the predicate with it, so a hole is an index
                // the callback sees, holding `undefined` (or whatever the
                // prototype has, once the read reaches it).
                JsValue item = array.get_property(std::to_string(i));
                if (callback.call_with_this(this_arg, {item, JsValue::make_number(static_cast<double>(i)), array}).is_truthy()) {
                    return what == Find::Value ? item : JsValue::make_number(static_cast<double>(i));
                }
            }
            return what == Find::Value ? JsValue::make_undefined() : JsValue::make_number(-1);
        };
    };
    js_define_method(ap, "find", 1, make_find("find", false, Find::Value));
    js_define_method(ap, "findIndex", 1, make_find("findIndex", false, Find::Index));
    js_define_method(ap, "findLast", 1, make_find("findLast", true, Find::Value));
    js_define_method(ap, "findLastIndex", 1, make_find("findLastIndex", true, Find::Index));

    // Generic, like the spec's forEach: the receiver itself is the third
    // argument of the callback, and `length` and the indices are read with
    // plain property access, so it works on array-likes (strings, NodeLists,
    // arguments) and reports the receiver rather than a snapshot.
    js_define_method(ap, "forEach", 1, [](Args args) {
        JsValue me = self();
        if (me.is_nullish()) {
            js_throw_type_error("Array.prototype.forEach called on null or undefined");
        }
        const size_t length = iteration_length(me); // LengthOfArrayLike precedes IsCallable.
        JsValue callback = require_callback(js_arg(args, 0), "forEach");
        const JsValue this_arg = js_arg(args, 1);
        for (size_t i = 0; i < length; ++i) {
            const std::string key = js_number_to_string(static_cast<double>(i));
            if (!me.has_property(key)) continue;
            callback.call_with_this(this_arg, {me.get_property(key), JsValue::make_number(static_cast<double>(i)), me});
        }
        return JsValue::make_undefined();
    });
    js_define_method(ap, "map", 1, [](Args args) {
        JsValue array = receiver("map");
        JsValue callback = require_callback(js_arg(args, 0), "map");
        const JsValue this_arg = js_arg(args, 1);
        std::vector<JsValue> result;
        std::vector<bool> holes;
        size_t length = elements_of(array).size();
        result.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            if (!has_index(array, i)) {
                // map keeps the hole; the callback is never called for it.
                result.push_back(JsValue::make_undefined());
                holes.push_back(true);
                continue;
            }
            JsValue item = index_value(array, i);
            result.push_back(callback.call_with_this(this_arg, {item, JsValue::make_number(static_cast<double>(i)), array}));
            holes.push_back(false);
        }
        JsValue mapped = JsValue::make_array(result);
        for (size_t i = 0; i < holes.size(); ++i) if (holes[i]) mapped.set_array_hole(i);
        return mapped;
    });
    js_define_method(ap, "filter", 1, [](Args args) {
        JsValue array = receiver("filter");
        JsValue callback = require_callback(js_arg(args, 0), "filter");
        const JsValue this_arg = js_arg(args, 1);
        std::vector<JsValue> result;
        for (size_t i = 0; i < elements_of(array).size(); ++i) {
            if (!has_index(array, i)) continue;             // filter asks HasProperty first
            JsValue item = index_value(array, i);
            if (callback.call_with_this(this_arg, {item, JsValue::make_number(static_cast<double>(i)), array}).is_truthy()) {
                result.push_back(std::move(item));
            }
        }
        return JsValue::make_array(result);
    });
    js_define_method(ap, "flatMap", 1, [](Args args) {
        JsValue array = receiver("flatMap");
        JsValue callback = require_callback(js_arg(args, 0), "flatMap");
        const JsValue this_arg = js_arg(args, 1);
        std::vector<JsValue> result;
        std::vector<bool> holes;
        for (size_t i = 0; i < elements_of(array).size(); ++i) {
            if (!has_index(array, i)) continue;
            JsValue item = index_value(array, i);
            JsValue mapped = callback.call_with_this(this_arg, {item, JsValue::make_number(static_cast<double>(i)), array});
            if (mapped.is_array()) {
                const auto& inner = elements_of(mapped);
                result.insert(result.end(), inner.begin(), inner.end());
                for (size_t k = 0; k < inner.size(); ++k) holes.push_back(mapped.is_array_hole(k));
            } else {
                result.push_back(std::move(mapped));
                holes.push_back(false);
            }
        }
        JsValue flattened = JsValue::make_array(result);
        for (size_t i = 0; i < holes.size(); ++i) if (holes[i]) flattened.set_array_hole(i);
        return flattened;
    });
    js_define_method(ap, "some", 1, [](Args args) {
        JsValue array = receiver("some");
        JsValue callback = require_callback(js_arg(args, 0), "some");
        const JsValue this_arg = js_arg(args, 1);
        for (size_t i = 0; i < elements_of(array).size(); ++i) {
            if (!has_index(array, i)) continue;
            JsValue item = index_value(array, i);
            if (callback.call_with_this(this_arg, {item, JsValue::make_number(static_cast<double>(i)), array}).is_truthy()) {
                return JsValue::make_boolean(true);
            }
        }
        return JsValue::make_boolean(false);
    });
    js_define_method(ap, "every", 1, [](Args args) {
        JsValue array = receiver("every");
        JsValue callback = require_callback(js_arg(args, 0), "every");
        const JsValue this_arg = js_arg(args, 1);
        for (size_t i = 0; i < elements_of(array).size(); ++i) {
            if (!has_index(array, i)) continue;             // every skips ones that are nowhere
            JsValue item = index_value(array, i);
            if (!callback.call_with_this(this_arg, {item, JsValue::make_number(static_cast<double>(i)), array}).is_truthy()) {
                return JsValue::make_boolean(false);
            }
        }
        return JsValue::make_boolean(true);
    });
    auto make_reduce = [](const char* name, bool from_end) {
        return [name, from_end](Args args) {
            JsValue array = receiver(name);
            JsValue callback = require_callback(js_arg(args, 0), name);
            size_t length = elements_of(array).size();
            size_t step = 0;
            JsValue accumulator;
            if (args.size() >= 2) {
                accumulator = args[1];
            } else {
                // The seed is the first element there is, which a hole is not.
                bool seeded = false;
                for (; step < length; ++step) {
                    size_t i = from_end ? length - 1 - step : step;
                    if (i >= elements_of(array).size() || !has_index(array, i)) continue;
                    accumulator = index_value(array, i);
                    seeded = true;
                    ++step;
                    break;
                }
                if (!seeded) js_throw_type_error("Reduce of empty array with no initial value");
            }
            for (; step < length; ++step) {
                size_t i = from_end ? length - 1 - step : step;
                if (i >= elements_of(array).size()) continue;
                if (!has_index(array, i)) continue;
                JsValue item = index_value(array, i);
                accumulator = callback.call({accumulator, item, JsValue::make_number(static_cast<double>(i)), array});
            }
            return accumulator;
        };
    };
    js_define_method(ap, "reduce", 1, make_reduce("reduce", false));
    js_define_method(ap, "reduceRight", 1, make_reduce("reduceRight", true));

    // ---- Iterators --------------------------------------------------------
    enum class Kind { Keys, Values, Entries };
    // %Array.prototype.keys/values/entries% are generic: they read `length`
    // and the indices off the receiver each step, so they work on any
    // array-like, `this` stays the object the iterator was made from, and a
    // shrinking array-like shortens the iteration — as the spec has it.
    auto make_array_iterator = [](Kind kind) {
        return [kind](Args) {
            JsValue me = self();
            if (me.is_nullish()) {
                js_throw_type_error(std::string("Array.prototype.") + (kind == Kind::Keys ? "keys" : kind == Kind::Values ? "values" : "entries") + " called on null or undefined");
            }
            auto index = std::make_shared<size_t>(0);
            return js_make_iterator([me, index, kind](JsValue& value) {
                if (*index >= iteration_length(me)) return false;
                size_t i = (*index)++;
                switch (kind) {
                    case Kind::Keys: value = JsValue::make_number(static_cast<double>(i)); break;
                    case Kind::Values: value = me.get_property(js_number_to_string(static_cast<double>(i))); break;
                    case Kind::Entries:
                        value = JsValue::make_array({JsValue::make_number(static_cast<double>(i)), me.get_property(js_number_to_string(static_cast<double>(i)))});
                        break;
                }
                return true;
            }, {me}, JsIntrinsics::get().array_iterator_proto);
        };
    };
    js_define_method(ap, "keys", 0, make_array_iterator(Kind::Keys));
    js_define_method(ap, "entries", 0, make_array_iterator(Kind::Entries));
    // %Array.prototype.values% is one and the same function object under all
    // three keys (`Array.prototype[Symbol.iterator] === Array.prototype.values`),
    // which is what platform object prototypes alias when Web IDL says so.
    JsValue values_iterator = js_make_native("values", 0, make_array_iterator(Kind::Values));
    js_define_hidden(ap, "values", values_iterator);
    js_define_hidden(ap, kIteratorKey, values_iterator);
    js_define_hidden(ap, "Symbol.iterator", values_iterator);
}

} // namespace litebird
