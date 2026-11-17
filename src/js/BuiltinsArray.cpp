// Array, Array.prototype and the iterator objects the built-ins hand out.

#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Heap.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

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

// Unlike receiver(), iteration must not copy an array-like: a live NodeList
// can grow or shrink between calls to next(), and forEach passes the original
// receiver to the callback. ToLength also handles fractional/negative lengths.
size_t iteration_length(const JsValue& target) {
    if (target.is_array()) return elements_of(target).size();
    const double length = js_to_integer(js_to_number(target.get_property("length")));
    if (length <= 0) return 0;
    return static_cast<size_t>(std::min(length, 9007199254740991.0));
}

JsValue iteration_receiver(const char* method) {
    JsValue target = self();
    if (target.is_nullish()) {
        js_throw_type_error(std::string("Array.prototype.") + method + " called on null or undefined");
    }
    return target;
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
    for (size_t i = 0; i < elements_of(array).size(); ++i) {
        if (i) out += separator;
        JsValue item = elements_of(array)[i];
        if (item.type == JsType::String) out += item.string_val.view();
        else if (!item.is_nullish()) out += js_to_string(item);
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

void sort_values(std::vector<JsValue>& items, const JsValue& comparator) {
    // undefined always sorts to the end and is never passed to the comparator.
    std::vector<JsValue> defined;
    size_t undefined_count = 0;
    for (auto& item : items) {
        if (item.is_undefined()) ++undefined_count;
        else defined.push_back(std::move(item));
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
    items.resize(items.size() + undefined_count);
}

void flatten_into(std::vector<JsValue>& out, const JsValue& array, double depth) {
    js_check_stack();       // [[[[...]]]].flat(Infinity) recurses once per level
    for (size_t i = 0; i < elements_of(array).size(); ++i) {
        JsValue item = elements_of(array)[i];
        if (depth >= 1 && item.is_array()) flatten_into(out, item, depth - 1);
        else out.push_back(std::move(item));
    }
}

JsValue iteration_result(const JsValue& value, bool done) {
    JsValue result = JsValue::make_object();
    result.set_property("value", value);
    result.set_property("done", JsValue::make_boolean(done));
    return result;
}

} // namespace

JsValue js_make_iterator(std::function<bool(JsValue& value)> step, std::vector<JsValue> roots) {
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = JsIntrinsics::get().iterator_proto;
    cell->klass = ObjectClass::Iterator;
    if (!roots.empty()) {
        // `step` holds these inside a std::function, where the collector cannot see them.
        auto kept = std::make_shared<std::vector<JsValue>>(std::move(roots));
        cell->trace_extra = [kept](Heap& heap) {
            if (!heap.first_visit(kept.get())) return;
            for (const auto& value : *kept) heap.mark_value(value);
        };
    }
    auto finished = std::make_shared<bool>(false);
    js_define_method(cell, "next", 0, [step, finished](Args) {
        JsValue value;
        if (!*finished && step(value)) return iteration_result(value, false);
        *finished = true;
        return iteration_result(JsValue::make_undefined(), true);
    });
    return JsValue::make_object(cell);
}

void install_array_builtins(JsIntrinsics& realm) {
    JsObject* ap = realm.array_proto;

    // Iterators are themselves iterable.
    js_define_method(realm.iterator_proto, kIteratorKey, 0, [](Args) { return self(); });
    js_define_method(realm.iterator_proto, "Symbol.iterator", 0, [](Args) { return self(); });

    // ---- Constructor ------------------------------------------------------
    JsValue array_ctor = js_make_native("Array", 1, [](Args args) {
        if (args.size() == 1 && args[0].type == JsType::Number) {
            double length = args[0].number_val;
            if (!(length >= 0) || length != std::floor(length) || length > 4294967295.0) {
                js_throw_range_error("Invalid array length");
            }
            if (length > (1u << 26)) js_throw_range_error("Array length exceeds the supported maximum");
            return JsValue::make_array(std::vector<JsValue>(static_cast<size_t>(length)));
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
        const JsValue& source = js_arg(args, 0);
        const JsValue& map_fn = js_arg(args, 1);
        const JsValue& this_arg = js_arg(args, 2);
        if (source.is_nullish()) js_throw_type_error(js_describe_thrown(source) + " is not iterable");
        if (!map_fn.is_undefined() && !map_fn.is_function()) js_throw_type_error("Array.from: mapper is not a function");

        std::vector<JsValue> items;
        const bool iterable = source.is_array() || source.type == JsType::String ||
                              (source.is_object_like() && (source.get_property(kIteratorKey).is_function() ||
                                                           source.get_property("Symbol.iterator").is_function()));
        if (iterable) {
            items = js_iterable_to_vector(source);
        } else if (source.is_object_like()) {
            double length = js_to_integer(js_to_number(source.get_property("length")));
            if (length > (1u << 26)) js_throw_range_error("Invalid array length");
            for (double i = 0; i < length; ++i) items.push_back(source.get_property(js_number_to_string(i)));
        }
        if (map_fn.is_function()) {
            for (size_t i = 0; i < items.size(); ++i) {
                items[i] = map_fn.call_with_this(this_arg, {items[i], JsValue::make_number(static_cast<double>(i))});
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
        elements.insert(elements.end(), args.begin(), args.end());
        return JsValue::make_number(static_cast<double>(elements.size()));
    });
    js_define_method(ap, "pop", 0, [](Args) {
        JsValue array = receiver("pop", true);
        auto& elements = elements_of(array);
        if (elements.empty()) return JsValue::make_undefined();
        JsValue last = std::move(elements.back());
        elements.pop_back();
        return last;
    });
    js_define_method(ap, "shift", 0, [](Args) {
        JsValue array = receiver("shift", true);
        auto& elements = elements_of(array);
        if (elements.empty()) return JsValue::make_undefined();
        JsValue first = std::move(elements.front());
        elements.erase(elements.begin());
        return first;
    });
    js_define_method(ap, "unshift", 1, [](Args args) {
        JsValue array = receiver("unshift", true);
        auto& elements = elements_of(array);
        elements.insert(elements.begin(), args.begin(), args.end());
        return JsValue::make_number(static_cast<double>(elements.size()));
    });

    // ---- Copying and slicing ----------------------------------------------
    js_define_method(ap, "slice", 2, [](Args args) {
        JsValue array = receiver("slice");
        const auto& elements = elements_of(array);
        size_t begin = relative_index(js_arg(args, 0), elements.size(), 0);
        size_t end = relative_index(js_arg(args, 1), elements.size(), elements.size());
        if (begin >= end) return JsValue::make_array();
        return JsValue::make_array(std::vector<JsValue>(elements.begin() + static_cast<std::ptrdiff_t>(begin),
                                                        elements.begin() + static_cast<std::ptrdiff_t>(end)));
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
        return JsValue::make_array(splice_impl(elements_of(array), args));
    });
    js_define_method(ap, "toSpliced", 2, [splice_impl](Args args) {
        JsValue array = receiver("toSpliced");
        std::vector<JsValue> copy(elements_of(array).begin(), elements_of(array).end());
        if (!args.empty()) splice_impl(copy, args);
        return JsValue::make_array(copy);
    });
    js_define_method(ap, "concat", 1, [](Args args) {
        JsValue array = receiver("concat");
        std::vector<JsValue> result(elements_of(array).begin(), elements_of(array).end());
        for (const auto& arg : args) {
            if (arg.is_array()) {
                const auto& more = elements_of(arg);
                result.insert(result.end(), more.begin(), more.end());
            } else {
                result.push_back(arg);
            }
        }
        return JsValue::make_array(result);
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
        return array;
    });
    js_define_method(ap, "toReversed", 0, [](Args) {
        JsValue array = receiver("toReversed");
        std::vector<JsValue> copy(elements_of(array).begin(), elements_of(array).end());
        std::reverse(copy.begin(), copy.end());
        return JsValue::make_array(copy);
    });
    js_define_method(ap, "sort", 1, [](Args args) {
        JsValue array = receiver("sort", true);
        const JsValue& comparator = js_arg(args, 0);
        if (!comparator.is_undefined() && !comparator.is_function()) {
            js_throw_type_error("The comparison function must be either a function or undefined");
        }
        // Sort a copy: the comparator is script and may touch the array.
        std::vector<JsValue> copy(elements_of(array).begin(), elements_of(array).end());
        sort_values(copy, comparator);
        elements_of(array).assign(copy.begin(), copy.end());
        return array;
    });
    js_define_method(ap, "toSorted", 1, [](Args args) {
        JsValue array = receiver("toSorted");
        std::vector<JsValue> copy(elements_of(array).begin(), elements_of(array).end());
        sort_values(copy, js_arg(args, 0));
        return JsValue::make_array(copy);
    });
    js_define_method(ap, "with", 2, [](Args args) {
        JsValue array = receiver("with");
        std::vector<JsValue> copy(elements_of(array).begin(), elements_of(array).end());
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0) index += static_cast<double>(copy.size());
        if (index < 0 || index >= static_cast<double>(copy.size())) js_throw_range_error("Invalid index");
        copy[static_cast<size_t>(index)] = js_arg(args, 1);
        return JsValue::make_array(copy);
    });
    js_define_method(ap, "fill", 1, [](Args args) {
        JsValue array = receiver("fill", true);
        auto& elements = elements_of(array);
        size_t begin = relative_index(js_arg(args, 1), elements.size(), 0);
        size_t end = relative_index(js_arg(args, 2), elements.size(), elements.size());
        for (size_t i = begin; i < end; ++i) elements[i] = js_arg(args, 0);
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
        for (size_t i = 0; i < chunk.size() && target + i < elements.size(); ++i) elements[target + i] = chunk[i];
        return array;
    });
    js_define_method(ap, "flat", 0, [](Args args) {
        JsValue array = receiver("flat");
        double depth = js_arg(args, 0).is_undefined() ? 1 : js_to_integer(js_to_number(js_arg(args, 0)));
        std::vector<JsValue> result;
        flatten_into(result, array, depth);
        return JsValue::make_array(result);
    });
    js_define_method(ap, "at", 1, [](Args args) {
        JsValue array = receiver("at");
        const auto& elements = elements_of(array);
        double index = js_to_integer(js_to_number(js_arg(args, 0)));
        if (index < 0) index += static_cast<double>(elements.size());
        if (index < 0 || index >= static_cast<double>(elements.size())) return JsValue::make_undefined();
        return elements[static_cast<size_t>(index)];
    });

    // ---- Searching --------------------------------------------------------
    js_define_method(ap, "indexOf", 1, [](Args args) {
        JsValue array = receiver("indexOf");
        const auto& elements = elements_of(array);
        for (size_t i = relative_index(js_arg(args, 1), elements.size(), 0); i < elements.size(); ++i) {
            if (strict_equals(elements[i], js_arg(args, 0))) return JsValue::make_number(static_cast<double>(i));
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
            if (strict_equals(elements[i], js_arg(args, 0))) return JsValue::make_number(static_cast<double>(i));
        }
        return JsValue::make_number(-1);
    });
    js_define_method(ap, "includes", 1, [](Args args) {
        JsValue array = receiver("includes");
        const auto& elements = elements_of(array);
        for (size_t i = relative_index(js_arg(args, 1), elements.size(), 0); i < elements.size(); ++i) {
            if (js_same_value_zero(elements[i], js_arg(args, 0))) return JsValue::make_boolean(true);
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
                JsValue item = elements_of(array)[i];
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

    js_define_method(ap, "forEach", 1, [](Args args) {
        JsValue target = iteration_receiver("forEach");
        // forEach snapshots the length, but reads each value only when its
        // index is reached. Unlike an iterator, it never visits appended items.
        const size_t length = iteration_length(target);
        JsValue callback = require_callback(js_arg(args, 0), "forEach");
        const JsValue this_arg = js_arg(args, 1);
        for (size_t i = 0; i < length; ++i) {
            const std::string key = std::to_string(i);
            if (!target.is_array() && !target.has_property(key)) continue;
            JsValue item = target.get_property(key);
            callback.call_with_this(this_arg, {item, JsValue::make_number(static_cast<double>(i)), target});
        }
        return JsValue::make_undefined();
    });
    js_define_method(ap, "map", 1, [](Args args) {
        JsValue array = receiver("map");
        JsValue callback = require_callback(js_arg(args, 0), "map");
        const JsValue this_arg = js_arg(args, 1);
        std::vector<JsValue> result;
        size_t length = elements_of(array).size();
        result.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            JsValue item = i < elements_of(array).size() ? elements_of(array)[i] : JsValue::make_undefined();
            result.push_back(callback.call_with_this(this_arg, {item, JsValue::make_number(static_cast<double>(i)), array}));
        }
        return JsValue::make_array(result);
    });
    js_define_method(ap, "filter", 1, [](Args args) {
        JsValue array = receiver("filter");
        JsValue callback = require_callback(js_arg(args, 0), "filter");
        const JsValue this_arg = js_arg(args, 1);
        std::vector<JsValue> result;
        for (size_t i = 0; i < elements_of(array).size(); ++i) {
            JsValue item = elements_of(array)[i];
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
        for (size_t i = 0; i < elements_of(array).size(); ++i) {
            JsValue item = elements_of(array)[i];
            JsValue mapped = callback.call_with_this(this_arg, {item, JsValue::make_number(static_cast<double>(i)), array});
            if (mapped.is_array()) {
                const auto& inner = elements_of(mapped);
                result.insert(result.end(), inner.begin(), inner.end());
            } else {
                result.push_back(std::move(mapped));
            }
        }
        return JsValue::make_array(result);
    });
    js_define_method(ap, "some", 1, [](Args args) {
        JsValue array = receiver("some");
        JsValue callback = require_callback(js_arg(args, 0), "some");
        const JsValue this_arg = js_arg(args, 1);
        for (size_t i = 0; i < elements_of(array).size(); ++i) {
            JsValue item = elements_of(array)[i];
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
            JsValue item = elements_of(array)[i];
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
                if (length == 0) js_throw_type_error("Reduce of empty array with no initial value");
                accumulator = elements_of(array)[from_end ? length - 1 : 0];
                step = 1;
            }
            for (; step < length; ++step) {
                size_t i = from_end ? length - 1 - step : step;
                if (i >= elements_of(array).size()) continue;
                JsValue item = elements_of(array)[i];
                accumulator = callback.call({accumulator, item, JsValue::make_number(static_cast<double>(i)), array});
            }
            return accumulator;
        };
    };
    js_define_method(ap, "reduce", 1, make_reduce("reduce", false));
    js_define_method(ap, "reduceRight", 1, make_reduce("reduceRight", true));

    // ---- Iterators --------------------------------------------------------
    enum class Kind { Keys, Values, Entries };
    auto make_array_iterator = [](Kind kind) {
        return [kind](Args) {
            JsValue target = iteration_receiver(kind == Kind::Keys ? "keys" :
                                                kind == Kind::Entries ? "entries" : "values");
            auto index = std::make_shared<size_t>(0);
            return js_make_iterator([target, index, kind](JsValue& value) {
                // Re-read length and the indexed property on every next().
                // This is generic over arrays and live DOM indexed collections.
                if (*index >= iteration_length(target)) return false;
                size_t i = (*index)++;
                JsValue item;
                if (kind != Kind::Keys) item = target.get_property(std::to_string(i));
                switch (kind) {
                    case Kind::Keys: value = JsValue::make_number(static_cast<double>(i)); break;
                    case Kind::Values: value = item; break;
                    case Kind::Entries:
                        value = JsValue::make_array({JsValue::make_number(static_cast<double>(i)), item});
                        break;
                }
                return true;
            }, {target});
        };
    };
    js_define_method(ap, "keys", 0, make_array_iterator(Kind::Keys));
    JsValue values = js_make_native("values", 0, make_array_iterator(Kind::Values));
    js_define_hidden(ap, "values", values);
    js_define_hidden(ap, kIteratorKey, values);
    js_define_hidden(ap, "Symbol.iterator", values);
    js_define_method(ap, "entries", 0, make_array_iterator(Kind::Entries));
}

} // namespace litebird
