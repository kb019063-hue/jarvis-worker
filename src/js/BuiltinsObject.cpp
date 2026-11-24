// Object, Function.prototype, Boolean, Symbol, Reflect, the Error family and a
// few global functions.

#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/DomBindings.hpp"
#include "litebird/js/JsInterpreter.hpp"
#include "litebird/js/TypedArray.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

/// `this` for the running native. Copied, because any nested call replaces
/// the thread's call context.
JsValue self() { return JsValue::current_this(); }

JsValue require_object(const JsValue& value, const char* where) {
    if (!value.is_object_like()) js_throw_type_error(std::string(where) + " called on non-object");
    return value;
}

std::vector<JsValue> array_like_to_vector(const JsValue& value) {
    if (value.is_nullish()) return {};
    if (value.is_array()) {
        // CreateListFromArrayLike reads every place, holes included, as undefined.
        return value.array_snapshot();
    }
    if (!value.is_object_like()) js_throw_type_error("CreateListFromArrayLike called on non-object");
    std::vector<JsValue> out;
    double length = js_to_integer(js_to_number(value.get_property("length")));
    for (double i = 0; i < length; ++i) out.push_back(value.get_property(js_number_to_string(i)));
    return out;
}

JsValue make_string_array(const std::vector<std::string>& items) {
    std::vector<JsValue> values;
    values.reserve(items.size());
    for (const auto& item : items) values.push_back(JsValue::make_string(item));
    return JsValue::make_array(values);
}

bool parse_index(const std::string& key, size_t& out) {
    if (key.empty() || key.size() > 10 || (key[0] == '0' && key.size() > 1)) return false;
    size_t value = 0;
    for (char c : key) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<size_t>(c - '0');
    }
    out = value;
    return true;
}

/// `object.own_keys()` with the cross-origin Window/Location rule applied:
/// when the object is a cross-origin WindowProxy or Location (HTML §7.2.3),
/// only its exposed keys are reported, in the spec's order — the indices,
/// then (for reflection that asks for the non-enumerable keys too) the
/// table names, the names of the child frames and "then". Same-origin or
/// anything else, the ordinary answer.
std::vector<std::string> reflected_own_keys(const JsValue& object, bool include_non_enumerable) {
    std::vector<std::string> keys;
    if (litebird::dom::cross_origin_own_keys(object, include_non_enumerable, keys)) return keys;
    return object.own_keys(include_non_enumerable);
}

/// The own keys `[[OwnPropertyKeys]]` reports for `object`: the string keys in
/// `own_keys`' order (indices ascending, then creation order), and after every
/// one of them the symbols, in the order they were made.
///
/// `own_keys` answers with strings only — the same answer a property table
/// gives `Object.keys`, `for...in` and JSON — because most callers either
/// cannot see a symbol key or are the very thing that must not. The ones that
/// copy every key (`Object.assign`, spread, `Object.getOwnPropertyDescriptors`)
/// have to add the symbols themselves; this is that list.
std::vector<std::string> assign_keys(const JsValue& object, bool enumerable_only = true) {
    std::vector<std::string> keys = reflected_own_keys(object, !enumerable_only);
    if (JsObject* cell = object.property_cell()) {
        // The table is walked twice — once by `own_keys` above and once here —
        // rather than counted first: the count a symbol key would have to be
        // compared against is not `keys.size()`, because an array's indices
        // are elements and not table entries.
        for (const auto& entry : cell->properties) {
            // `Object.assign` copies only the enumerable symbols; the
            // `[[OwnPropertyKeys]]` a caller like `Reflect.ownKeys` asks for
            // reports every one of them.
            if (enumerable_only && !(entry.flags & PropertyMap::Enumerable)) continue;
            JsValue symbol;
            if (!js_symbol_from_key(entry.first, symbol)) continue;
            keys.push_back(entry.first);
        }
    }
    return keys;
}

/// The table that holds `value`'s own named properties, if it has one.
PropertyMap* own_table(const JsValue& value, bool create) {
    if (JsObject* cell = value.property_cell(create)) return &cell->properties;
    return value.loose_properties(create);
}

JsValue descriptor_for(const JsValue& object, const std::string& key) {
    {
        // A cross-origin WindowProxy or Location describes exactly its
        // exposed keys, each configurable, and nothing else (HTML §7.2.3.5,
        // §7.2.4.5). Indices are enumerable; everything else is not.
        //
        // The shape depends on the entry: an attribute of the table is an
        // accessor descriptor whose get is a native calling-sequence function
        // that reads the property (the spec's crossOriginGet), and whose set
        // exists only when the entry has NeedsSetter (location, href);
        // a method entry is a data descriptor whose value is the
        // child's own function (the engine has no caller-realm anonymous
        // built-in to put there, the properties are otherwise as specified).
        // `then` and the indices are data descriptors.
        std::vector<std::string> exposed;
        if (litebird::dom::cross_origin_own_keys(object, /*include_non_enumerable=*/true, exposed)) {
            struct CrossOriginEntry { const char* key; bool needs_get; bool needs_set; };
            static const CrossOriginEntry kWindow[] = {
                {"window", true, false}, {"self", true, false}, {"location", true, true},
                {"close", false, false}, {"closed", true, false}, {"focus", false, false},
                {"blur", false, false}, {"frames", true, false}, {"length", true, false},
                {"top", true, false}, {"opener", true, false}, {"parent", true, false},
                {"postMessage", false, false},
            };
            static const CrossOriginEntry kLocation[] = {
                {"href", false, true}, {"replace", false, false},
            };
            const bool is_location =
                std::find(exposed.begin(), exposed.end(), "replace") != exposed.end() &&
                std::find(exposed.begin(), exposed.end(), "postMessage") == exposed.end();
            const CrossOriginEntry* table = is_location ? kLocation : kWindow;
            const size_t table_size = is_location ? sizeof(kLocation) / sizeof(kLocation[0])
                                                     : sizeof(kWindow) / sizeof(kWindow[0]);

            auto data_wrap = [&](const JsValue& value, bool enumerable) {
                JsValue d = JsValue::make_object();
                d.set_property("value", value);
                d.set_property("writable", JsValue::make_boolean(false));
                d.set_property("enumerable", JsValue::make_boolean(enumerable));
                d.set_property("configurable", JsValue::make_boolean(true));
                return d;
            };
            const bool listed =
                std::find(exposed.begin(), exposed.end(), key) != exposed.end();
            if (!listed) return JsValue::make_undefined();
            if (key == "then") return data_wrap(JsValue::make_undefined(), false);
            size_t index = 0;
            if (parse_index(key, index)) return data_wrap(object.get_property(key), true);
            for (size_t i = 0; i < table_size; ++i) {
                const CrossOriginEntry& entry = table[i];
                if (key != entry.key) continue;
                if (!entry.needs_get && !entry.needs_set) {
                    return data_wrap(object.get_property(key), false);
                }
                JsValue d = JsValue::make_object();
                JsValue get = entry.needs_get
                    ? js_make_native(("get " + key).c_str(), 0,
                                     [object, key](Args) { return object.get_property(key); })
                    : JsValue::make_undefined();
                const std::string property = key;
                JsValue set = entry.needs_set
                    ? js_make_native(("set " + key).c_str(), 1,
                                     [target = object, property](Args args) mutable -> JsValue {
                                         target.set_property(
                                             property,
                                             args.empty() ? JsValue::make_undefined() : args[0]);
                                         return JsValue::make_undefined();
                                     })
                    : JsValue::make_undefined();
                d.set_property("get", get);
                d.set_property("set", set);
                d.set_property("enumerable", JsValue::make_boolean(false));
                d.set_property("configurable", JsValue::make_boolean(true));
                return d;
            }
            return JsValue::make_undefined();
        }
    }
    // The `prototype` of a constructor function is made on first use, and
    // describing it is a use.
    if (object.type == JsType::Function && key == "prototype") (void)object.get_property(key);
    JsObject* cell = object.property_cell();
    const bool frozen = cell && cell->frozen;
    const bool sealed = cell && (cell->sealed || cell->frozen);

    auto data = [&](const JsValue& value, bool writable, bool enumerable, bool configurable) {
        JsValue d = JsValue::make_object();
        d.set_property("value", value);
        d.set_property("writable", JsValue::make_boolean(writable && !frozen));
        d.set_property("enumerable", JsValue::make_boolean(enumerable));
        d.set_property("configurable", JsValue::make_boolean(configurable && !sealed));
        return d;
    };

    size_t index = 0;
    if (object.type == JsType::String) {
        if (key == "length") return data(object.get_property("length"), false, false, false);
        if (parse_index(key, index) && index < object.string_val.length()) {
            return data(object.get_property(key), false, true, false);
        }
        return JsValue::make_undefined();
    }
    // String wrapper objects have the same own, read-only character
    // properties as the primitive's ToObject result. They are exotic rather
    // than entries in the ordinary property map, so ask the wrapped string
    // before that map just as the array path asks its elements first.
    if (cell && cell->klass == ObjectClass::String) {
        const JsValue* wrapped = js_wrapped_primitive(object);
        if (wrapped && wrapped->type == JsType::String &&
            parse_index(key, index) && index < wrapped->string_val.length()) {
            return data(object.get_property(key), false, true, false);
        }
    }
    if (cell && cell->is_array) {
        if (key == "length") {
            // The count, with the attributes the descriptor gave it: a
            // `{ writable: false }` definition is in the map, and the map is
            // what has the answer (finding 253).
            auto length_entry = cell->properties.find("length");
            if (length_entry != cell->properties.end()) {
                return data(JsValue::make_number(static_cast<double>(cell->array_elements.size())),
                            (length_entry->flags & PropertyMap::Writable) != 0,
                            (length_entry->flags & PropertyMap::Enumerable) != 0,
                            (length_entry->flags & PropertyMap::Configurable) != 0);
            }
            return data(JsValue::make_number(static_cast<double>(cell->array_elements.size())), true, false, false);
        }
        if (parse_index(key, index)) {
            if (index >= cell->array_elements.size()) return JsValue::make_undefined();
            if (!object.is_array_hole(index)) return data(cell->array_elements[index], true, true, true);
            // A hole has no descriptor: getOwnPropertyDescriptor(a, '1') is
            // undefined. Unless `Object.defineProperty` has made something of
            // the index, which is then in the property map and described
            // below like any other property (JsValue::has_own_property).
            if (!cell->has_accessors) return JsValue::make_undefined();
        }
    }
    PropertyMap* table = own_table(object, false);
    if (table) {
        auto it = table->find(key);
        if (it != table->end()) {
            if (it->flags & PropertyMap::Accessor) {
                JsValue d = JsValue::make_object();
                d.set_property("get", it->second.is_function() ? it->second : JsValue::make_undefined());
                d.set_property("set", it->setter ? *it->setter : JsValue::make_undefined());
                d.set_property("enumerable", JsValue::make_boolean(it->flags & PropertyMap::Enumerable));
                d.set_property("configurable", JsValue::make_boolean((it->flags & PropertyMap::Configurable) && !sealed));
                return d;
            }
            return data(it->second, it->flags & PropertyMap::Writable, it->flags & PropertyMap::Enumerable,
                        it->flags & PropertyMap::Configurable);
        }
    }
    if (object.is_function() && object.function_data() && (key == "name" || key == "length")) {
        return data(object.get_property(key), false, false, true);
    }
    // The global object owns the global declarations: §9.1.1.4 makes them
    // non-configurable data properties of it. They live in the global
    // environment rather than in the object's property table -- the object
    // finds them through its `lookup_fallback` -- so the descriptor is
    // derived from the binding, or `Object.getOwnPropertyDescriptor(this,
    // "Object")` would answer `undefined` for a name that
    // `hasOwnProperty(this, "Object")` reports as present. It is asked
    // before the exotic below, which claims these names through
    // `exotic->has` and then describes none of them, and would otherwise
    // answer `undefined` on the way out.
    //
    // Only a *variable* declaration is one of these. What `let`, `const` and
    // `class` declare is not a property of the global object at all, which is
    // what `is_lexical` marks (`Object.getOwnPropertyDescriptor(this, "x")`
    // is undefined after `let x`), and a non-writable global such as
    // `undefined` is the one kind the standard does make non-configurable.
    if (object.is_object_like() && object.object_val) {
        dom::Realm& realm = dom::realm_of(JsIntrinsics::get());
        if (object.object_val == realm.window.object_val) {
            if (auto scope = realm.global_scope.lock()) {
                const JsEnvironment::Binding* binding = scope->find_own(key);
                if (binding && !binding->is_lexical) {
                    return data(binding->value, !binding->is_readonly, false, !binding->is_readonly);
                }
            }
        }
    }

    // A property the object computes instead of storing: the indices and
    // names of a list, of a NamedNodeMap or of an HTMLCollection. The value
    // is what reading it gives, and it is configurable as every such
    // property is.
    if (cell && cell->exotic && cell->exotic->has(cell, key)) {
        bool enumerable = true;
        bool writable = false;
        if (cell->exotic->computed_property(cell, key, enumerable, writable)) {
            return data(object.get_property(key), writable, enumerable, true);
        }
        return JsValue::make_undefined();
    }
    return JsValue::make_undefined();
}

/// ECMA-262 §7.3.15 TestIntegrityLevel, which `Object.isFrozen` (§20.1.2.13)
/// and `Object.isSealed` (§20.1.2.15) are defined by. The answer is not a bit
/// the engine keeps: it is `[[IsExtensible]]` false **and** every own property
/// asked, one at a time. An object nobody froze but nobody can extend either,
/// with no properties or with non-writable non-configurable ones, is sealed and
/// frozen exactly as much as one that was passed to `freeze()`.
///
/// The two levels differ in one clause: at the frozen level a writable **data**
/// property fails it, an accessor does not (an accessor has no write of its
/// own to prevent), and a non-configurable property fails both.
bool test_integrity_level(const JsValue& object, bool frozen) {
    // `[[IsExtensible]]`, asked the same way `Object.isExtensible` asks it: an
    // object with no cell of its own has nothing to prevent.
    const JsObject* cell = object.property_cell();
    const bool extensible = !cell || cell->extensible;
    if (object.is_object_like() && !extensible) {
        for (const std::string& key : reflected_own_keys(object, true)) {
            // `descriptor_for` is the same account `getOwnPropertyDescriptor`
            // gives, so a property this cannot describe is not one the level
            // can fail on.
            const JsValue descriptor = descriptor_for(object, key);
            if (descriptor.is_undefined()) continue;
            if (frozen) {
                const JsValue writable = descriptor.get_property("writable");
                // An accessor has no `writable` at all, which reads as
                // undefined and is not a failure.
                if (writable.type == JsType::Boolean && writable.bool_val) return false;
            }
            const JsValue configurable = descriptor.get_property("configurable");
            if (configurable.type == JsType::Boolean && configurable.bool_val) return false;
        }
        return true;
    }
    return false;
}


/// `Object.defineProperty(object, key, descriptor)`. Returns false, or throws
/// when `should_throw` is set, where the property that is there does not
/// allow the change (`Reflect.defineProperty` reports, the others throw).
bool define_from_descriptor(JsValue object, const std::string& key, const JsValue& descriptor,
                            bool should_throw = true) {
    // A cross-origin WindowProxy or Location refuses every definition with a
    // SecurityError (HTML §7.2.3.6, §7.2.4.6). CrossOriginOwnPropertyKeys
    // reporting the object as cross-origin is what tells us which case this
    // is; same-origin ones fall through to the ordinary path.
    {
        std::vector<std::string> exposed;
        if (litebird::dom::cross_origin_own_keys(object, /*include_non_enumerable=*/true, exposed)) {
            litebird::dom::throw_dom(litebird::dom::current_realm(), "SecurityError",
                                     "Blocked a frame from defining a property on a cross-origin Window.");
        }
    }
    if (!descriptor.is_object_like()) js_throw_type_error("Property description must be an object");

    // The descriptor is read first, field by field: reading one can run a
    // getter, and nothing of the object is held across that.
    struct Field {
        bool present{false};
        JsValue value;
    };
    auto read = [&](const char* name) {
        Field field;
        field.present = descriptor.has_property(name);
        if (field.present) field.value = descriptor.get_property(name);
        return field;
    };
    const Field enumerable = read("enumerable");
    const Field configurable = read("configurable");
    const Field value = read("value");
    const Field writable = read("writable");
    const Field get = read("get");
    const Field set = read("set");
    const bool wants_accessor = get.present || set.present;
    const bool wants_data = value.present || writable.present;
    if (wants_accessor && wants_data) {
        js_throw_type_error("Invalid property descriptor. Cannot both specify accessors and a value or writable attribute");
    }
    if (get.present && !get.value.is_undefined() && !get.value.is_function()) js_throw_type_error("Getter must be a function");
    if (set.present && !set.value.is_undefined() && !set.value.is_function()) js_throw_type_error("Setter must be a function");

    // The `prototype` of a constructor function is there before anybody has
    // read it, though it is only made on first use: make it now, or this
    // would define a second, empty one in its place.
    if (object.type == JsType::Function && key == "prototype") (void)object.get_property(key);
    JsObject* cell = object.property_cell(true);

    // Array elements are stored densely; attributes on them are not tracked.
    // An *accessor* already defined on the index is a different matter: it
    // lives in the property map, and this shortcut would leave it there and
    // write the element beside it, so redefining it as a data property would
    // be lost (`Array.prototype[0]` gets this: a setter-only accessor, then a
    // value — `[, ][0]` read `undefined`). Only take the shortcut when the
    // map has no accessor on the key.
    //
    // The same holds for a data property that was written over such an
    // accessor: it is in the map too, with attributes, and a new descriptor
    // for it is checked against them below rather than written through.
    //
    // What the map has under an index is the index's property only where the
    // place is a hole (JsValue::define_accessor makes it one). Under an
    // element or past the end it is not looked at.
    size_t index = 0;
    const bool array_index = cell && cell->is_array && parse_index(key, index);
    const bool in_range = array_index && index < cell->array_elements.size();
    const bool element_there = in_range && !object.is_array_hole(index);
    const bool mapped_hole = in_range && !element_there && cell->properties.find(key) != cell->properties.end();
    // The shortcut only holds while the attributes asked for are the ones a
    // stored element has anyway: writable, enumerable and configurable, and
    // for `length` not configurable. A descriptor that wants any of them
    // *off* cannot be stored in the dense representation at all — it has no
    // attribute bits — so the write goes to the property map, which is where
    // `Object.defineProperty(a, "0", { writable: false })` has to end up or
    // `push` on an array whose `length` was frozen goes on growing it
    // (finding 253). A place that is already there becomes a hole in the
    // elements so that the map's entry is what a read finds, the way
    // JsValue::define_accessor leaves it; one that is not there grows the
    // elements to cover it, for the same reason.
    const bool plain_attributes =
        (!writable.present || writable.value.is_truthy()) &&
        (!enumerable.present || enumerable.value.is_truthy()) &&
        (!configurable.present || (key == "length" ? !configurable.value.is_truthy()
                                                   : configurable.value.is_truthy()));
    if (cell && cell->is_array && !wants_accessor && (array_index || key == "length") && !mapped_hole &&
        plain_attributes) {
        if (value.present) object.set_property(key, value.value);
        return true;
    }
    // What the place holds now: an element keeps its value and its three
    // attributes when a descriptor is written over it, so they are read
    // before the place is made a hole below.
    const JsValue element_value = element_there ? object.array_element(index) : JsValue::make_undefined();
    // An index whose attributes are not the plain ones, and `length` when its
    // writability was asked about: the place stays a hole (or, for `length`,
    // stays the count) and the attributes live in the property map, which is
    // where a read asks first once `has_accessors` says the map has
    // something the elements do not (finding 253).
    if (cell && cell->is_array && !wants_accessor && array_index && !mapped_hole && !plain_attributes) {
        // A place the elements already cover becomes a hole so that the map's
        // entry is what a read finds; one past the end has to be *covered*
        // first, because an index the elements do not reach is not a place the
        // map can answer for — the read never asks. Both halves are needed:
        // with only the first, `Object.defineProperty(Array.prototype, '0',
        // { writable: false })` left the value in the elements and the
        // attributes in the map, so `hasOwnProperty` said `false` while `[0]`
        // read `'p'` (finding 269).
        if (in_range && element_there) {
            object.set_array_hole(index);
        } else if (cell->extensible) {
            const size_t previous = cell->array_elements.size();
            cell->array_elements.resize(index + 1, JsValue::make_undefined());
            if (!cell->array_holes) cell->array_holes = std::make_unique<JsObject::Holes>();
            for (size_t i = previous; i <= index; ++i) cell->array_holes->set(i);
        }
        cell->has_accessors = true;
    }
    // ArraySetLength: a `length` given as a value must be a valid array
    // length, and a negative one is a RangeError before any attribute is
    // looked at (finding 253).
    if (cell && cell->is_array && key == "length" && value.present) {
        const double asked = value.value.to_number();
        if (!(asked >= 0) || asked != std::floor(asked) || asked > 4294967295.0) {
            js_throw_range_error("Invalid array length");
        }
    }
    if (cell && cell->is_array && !wants_accessor && key == "length" && writable.present &&
        !writable.value.is_truthy()) {
        cell->has_accessors = true;
        // The count is still the value; only the attributes are new.
        object.define_property("length",
                               JsValue::make_number(static_cast<double>(cell->array_elements.size())),
                               static_cast<uint8_t>(PropertyMap::Enumerable | PropertyMap::Configurable));
    }
    // An accessor asked for on an index that is an element. The element is a
    // property that exists, though the map does not have it: writable,
    // enumerable and configurable, unless the array is sealed or frozen. So
    // it may become an accessor on an array that takes no new properties,
    // and it keeps being enumerable, as a data property that is turned into
    // an accessor keeps the two attributes they share.

    auto refuse = [&](const std::string& message) {
        if (should_throw) js_throw_type_error(message);
        return false;
    };

    PropertyMap* table = own_table(object, true);
    if (!table) js_throw_type_error("Object.defineProperty called on non-object");
    auto existing = table->find(key);
    const bool exists = existing != table->end() && (!array_index || mapped_hole);
    if (element_there && (cell->sealed || cell->frozen)) return refuse("Cannot redefine property: " + key);
    if (!exists && !element_there && cell && !cell->extensible) {
        return refuse("Cannot define property " + key + ", object is not extensible");
    }
    if (exists) {
        // What a property that cannot be configured still allows (the
        // standard's ValidateAndApplyPropertyDescriptor): a descriptor that
        // changes nothing, a new value while it is writable, and writable
        // going from true to false. Compiled classes rely on the last one:
        // `Object.defineProperty(Constructor, "prototype", { writable: false })`.
        // A sealed or frozen object makes every property of it such a one.
        const bool is_accessor = existing->flags & PropertyMap::Accessor;
        const bool can_configure = (existing->flags & PropertyMap::Configurable) && !(cell && (cell->sealed || cell->frozen));
        const bool can_write = !is_accessor && (existing->flags & PropertyMap::Writable) && !(cell && cell->frozen);
        if (!can_configure) {
            bool allowed = true;
            if (configurable.present && configurable.value.is_truthy()) allowed = false;
            if (enumerable.present && enumerable.value.is_truthy() != static_cast<bool>(existing->flags & PropertyMap::Enumerable)) {
                allowed = false;
            }
            if (is_accessor) {
                const JsValue current_get = existing->second.is_function() ? existing->second : JsValue::make_undefined();
                const JsValue current_set = existing->setter && existing->setter->is_function() ? *existing->setter
                                                                                                 : JsValue::make_undefined();
                if (wants_data) allowed = false;
                if (get.present && !js_same_value(get.value, current_get)) allowed = false;
                if (set.present && !js_same_value(set.value, current_set)) allowed = false;
            } else {
                if (wants_accessor) allowed = false;
                if (!can_write) {
                    if (writable.present && writable.value.is_truthy()) allowed = false;
                    if (value.present && !js_same_value(value.value, existing->second)) allowed = false;
                }
            }
            if (!allowed) return refuse("Cannot redefine property: " + key);
        }
    }

    uint8_t flags = exists ? existing->flags
                  : element_there ? static_cast<uint8_t>(PropertyMap::Enumerable | PropertyMap::Writable |
                                                         PropertyMap::Configurable) : 0;
    auto apply = [&](const Field& field, uint8_t bit) {
        if (!field.present) return;
        if (field.value.is_truthy()) flags = static_cast<uint8_t>(flags | bit);
        else flags = static_cast<uint8_t>(flags & ~bit);
    };
    apply(enumerable, PropertyMap::Enumerable);
    apply(configurable, PropertyMap::Configurable);

    if (wants_accessor) {
        JsValue getter = get.present ? get.value : JsValue::make_undefined();
        JsValue setter = set.present ? set.value : JsValue::make_undefined();
        if (exists && (existing->flags & PropertyMap::Accessor)) {
            if (!get.present) getter = existing->second;
            if (!set.present && existing->setter) setter = *existing->setter;
        }
        object.define_accessor(key, getter, setter, flags);
        return true;
    }

    // A descriptor with nothing but `enumerable` or `configurable` leaves an
    // accessor an accessor.
    if (exists && (existing->flags & PropertyMap::Accessor) && !wants_data) {
        existing->flags = static_cast<uint8_t>((flags | PropertyMap::Accessor) & ~PropertyMap::Writable);
        return true;
    }

    apply(writable, PropertyMap::Writable);
    JsValue new_value = value.present ? value.value
                      : (exists && !(existing->flags & PropertyMap::Accessor)) ? existing->second
                      : element_there ? element_value
                      : JsValue::make_undefined();
    object.define_property(key, new_value, flags);
    return true;
}

std::string object_tag(const JsValue& value) {
    switch (value.type) {
        case JsType::Undefined: return "Undefined";
        case JsType::Null: return "Null";
        case JsType::Boolean: return "Boolean";
        case JsType::Number: return "Number";
        case JsType::String: return "String";
        case JsType::Symbol: return "Symbol";
        case JsType::Function:
        case JsType::NativeFunction: return "Function";
        default: break;
    }
    if (value.is_object_like()) {
        JsValue tag = value.get_property("@@toStringTag");
        if (tag.type == JsType::String) return tag.string_val.str();
    }
    if (JsObject* cell = value.object_val) {
        switch (cell->klass) {
            case ObjectClass::Array: return "Array";
            case ObjectClass::Error: return "Error";
            case ObjectClass::Date: return "Date";
            case ObjectClass::RegExp: return "RegExp";
            case ObjectClass::Arguments: return "Arguments";
            case ObjectClass::Map: return "Map";
            case ObjectClass::Set: return "Set";
            case ObjectClass::WeakMap: return "WeakMap";
            case ObjectClass::WeakSet: return "WeakSet";
            case ObjectClass::Promise: return "Promise";
            case ObjectClass::Generator: return "Generator";
            case ObjectClass::String: return "String";
            case ObjectClass::Number: return "Number";
            case ObjectClass::Boolean: return "Boolean";
            default: break;
        }
        if (cell->is_array) return "Array";
    }
    return "Object";
}

// ---------------------------------------------------------------------------
// structuredClone
// ---------------------------------------------------------------------------
//
// The HTML structured-clone algorithm, for the types this engine has. A clone
// keeps the type of what it copies — a Map comes back a Map with its entries,
// a RegExp a RegExp with its pattern and flags — and keeps the shape of the
// graph: two references to one object are one object afterwards, and a cycle
// is a cycle.
//
// What is here and what is not is the standard's split. Functions and host
// objects are refused with a DataCloneError (§2.7.3 of the HTML standard's
// "StructuredSerializeInternal"), a Symbol throws a TypeError because it has
// no representation at all, and WeakMap and WeakSet are refused because their
// entries are exactly what a script may not read back.

struct CloneState {
    std::unordered_map<const void*, JsValue> seen;
    /// The buffers a transfer list moved out of the object graph, by identity:
    /// the clone of one is the buffer it was transferred to, not a copy.
    std::unordered_map<const void*, JsValue> transferred;
};

// The exception the standard names: a DOMException called DataCloneError, so
// `e.name` is that rather than a message that begins with it.
[[noreturn]] void throw_data_clone(const char* what) {
    dom::throw_dom(dom::current_realm(), "DataCloneError",
                   std::string(what) + " could not be cloned.");
}

JsValue structured_clone(const JsValue& value, CloneState& state);

JsValue structured_clone(const JsValue& value, CloneState& state) {
    js_check_stack();
    if (value.type == JsType::Symbol) {
        js_throw_type_error("structuredClone: a Symbol cannot be cloned");
    }
    if (!value.is_object_like()) return value;
    if (value.is_function() || value.dom_node_value()) {
        throw_data_clone(value.is_function() ? "a function" : "a host object");
    }
    auto it = state.seen.find(value.identity());
    if (it != state.seen.end()) return it->second;

    JsObject* cell = value.object_val;

    // A transferred buffer was already moved; every reference to it in the
    // graph — a view's `buffer` among them — answers the same new buffer.
    if (const auto moved = state.transferred.find(value.identity()); moved != state.transferred.end()) {
        return moved->second;
    }

    // --- the keyed collections -------------------------------------------
    {
        bool entries = false;
        if (is_keyed_collection(value, entries)) {
            // An empty collection first, registered before its entries are
            // cloned, so a map that holds itself clones like anything else.
            // The entries are added to that same object: an identity the
            // graph already points at must not be swapped afterwards.
            JsValue copy = make_collection_copy(entries, {});
            state.seen.emplace(value.identity(), copy);
            std::vector<std::pair<JsValue, JsValue>> items;
            collection_entries(value, entries, items);
            for (const auto& [key, item] : items)
                add_collection_copy_entry(copy, structured_clone(key, state),
                                          structured_clone(item, state));
            return copy;
        }
    }

    // --- a regular expression --------------------------------------------
    if (cell->klass == ObjectClass::RegExp) {
        std::string source, flags;
        if (!js_regexp_source_and_flags(value, source, flags)) throw_data_clone("a RegExp");
        JsValue copy = js_regexp_create(source, flags);
        state.seen.emplace(value.identity(), copy);
        return copy;
    }

    // --- the byte-carrying types ------------------------------------------
    BufferViewInfo info;
    if (buffer_view_info(value, info)) {
        std::string bytes;
        if (!typed_array_bytes(value, bytes)) throw_data_clone("a detached ArrayBuffer");
        if (info.kind == 1) {
            JsValue copy = array_buffer_from_bytes(bytes);
            state.seen.emplace(value.identity(), copy);
            return copy;
        }
        // A view is cloned with the buffer it is a window on: `clone.buffer` is
        // the same object the source's `buffer` cloned to, which the seen map
        // is what remembers. So the view is built over the clone of the
        // buffer, at the same offset and with the same length.
        const JsValue source_buffer = value.get_property("buffer");
        JsValue cloned_buffer = structured_clone(source_buffer, state);
        JsValue copy = (info.kind == 2)
            ? typed_array_view_over(info.element_type, cloned_buffer, info.byte_offset,
                                    info.byte_length, info.length_tracking)
            : data_view_over(cloned_buffer, info.byte_offset, info.byte_length);
        state.seen.emplace(value.identity(), copy);
        return copy;
    }

    // --- arrays ------------------------------------------------------------
    if (cell->is_array) {
        JsValue copy = JsValue::make_array();
        state.seen.emplace(value.identity(), copy);
        for (size_t i = 0; i < cell->array_elements.size(); ++i) {
            // Structured clone keeps a hole a hole (the standard skips the
            // index rather than writing undefined into it).
            if (value.is_array_hole(i)) copy.object_val->array_elements.push_back(JsValue::make_undefined());
            else copy.object_val->array_elements.push_back(structured_clone(cell->array_elements[i], state));
            if (value.is_array_hole(i)) copy.set_array_hole(i);
        }
        return copy;
    }

    // --- ordinary objects, Date, Error -------------------------------------
    JsValue copy = JsValue::make_object();
    state.seen.emplace(value.identity(), copy);
    if (cell->klass == ObjectClass::Date || cell->klass == ObjectClass::Error) {
        copy.object_val->prototype = cell->prototype;
        copy.object_val->klass = cell->klass;
        copy.object_val->internal = cell->internal;
    } else if (cell->klass != ObjectClass::Plain || cell->exotic || cell->internal) {
        // A Promise, a generator, an iterator, a WeakMap, a host object with
        // computed properties: no clone of them is defined, and a plain object
        // that answered one of their methods would be worse than a refusal.
        throw_data_clone("a host or exotic object");
    }
    for (const auto& key : reflected_own_keys(value, cell->klass == ObjectClass::Error)) {
        copy.set_property(key, structured_clone(value.get_property(key), state));
    }
    return copy;
}

// The transfer list, read the way §2.7.6 (StructuredSerializeWithTransfer)
// reads it: every entry must be an ArrayBuffer, no buffer may appear twice,
// and the buffers leave the source graph detached.
void transfer_buffers(const JsValue& list, CloneState& state) {
    if (list.is_undefined()) return;
    const std::vector<JsValue> items = array_like_to_vector(list);
    std::vector<const void*> seen;
    for (const JsValue& item : items) {
        BufferViewInfo info;
        if (!buffer_view_info(item, info) || info.kind != 1) {
            throw_data_clone("a transferable that is not an ArrayBuffer");
        }
        if (std::find(seen.begin(), seen.end(), item.identity()) != seen.end()) {
            throw_data_clone("a transferable that is listed twice");
        }
        seen.push_back(item.identity());
        JsValue moved;
        if (!array_buffer_transfer(item, moved)) throw_data_clone("a detached ArrayBuffer");
        state.transferred.emplace(item.identity(), moved);
    }
}

const char* kErrorNames[7] = {"Error", "TypeError", "RangeError", "SyntaxError",
                              "ReferenceError", "EvalError", "URIError"};

} // namespace

// ---------------------------------------------------------------------------
// Object, Function.prototype, Boolean, Symbol, Reflect
// ---------------------------------------------------------------------------

void install_object_builtins(JsIntrinsics& realm) {
    // ---- Object.prototype -------------------------------------------------
    JsObject* op = realm.object_proto;

    // Annex B.3.3's legacy accessor methods use ordinary property descriptors.
    // Keep the setter when a getter is added to an existing accessor (and
    // vice versa) by routing the update through the same validation path as
    // Object.defineProperty.
    auto define_legacy_accessor = [](Args args, bool is_getter) {
        JsValue object = js_to_object(self());
        const JsValue accessor = js_arg(args, 1);
        if (!accessor.is_function()) {
            js_throw_type_error(is_getter ? "Getter must be a function" : "Setter must be a function");
        }
        const std::string key = js_to_property_key(js_arg(args, 0));
        JsValue descriptor = JsValue::make_object();
        descriptor.set_property(is_getter ? "get" : "set", accessor);
        descriptor.set_property("enumerable", JsValue::make_boolean(true));
        descriptor.set_property("configurable", JsValue::make_boolean(true));
        define_from_descriptor(object, key, descriptor);
        return JsValue::make_undefined();
    };
    auto lookup_legacy_accessor = [](Args args, bool is_getter) {
        JsValue current = js_to_object(self());
        const std::string key = js_to_property_key(js_arg(args, 0));
        for (int depth = 0; current.is_object_like() && depth < kMaxPrototypeChain; ++depth) {
            const JsValue descriptor = descriptor_for(current, key);
            if (!descriptor.is_undefined()) {
                return descriptor.get_property(is_getter ? "get" : "set");
            }
            // HTML's cross-origin Window/Location exotic objects report a
            // null prototype; do not walk the engine's internal prototype
            // past the keys their cross-origin descriptor exposed.
            std::vector<std::string> exposed;
            if (litebird::dom::cross_origin_own_keys(current, true, exposed)) break;
            JsObject* prototype = current.prototype_object();
            if (!prototype) break;
            current = JsValue::make_object(prototype);
        }
        return JsValue::make_undefined();
    };

    js_define_method(op, "__defineGetter__", 2, [define_legacy_accessor](Args args) {
        return define_legacy_accessor(args, true);
    });
    js_define_method(op, "__defineSetter__", 2, [define_legacy_accessor](Args args) {
        return define_legacy_accessor(args, false);
    });
    js_define_method(op, "__lookupGetter__", 1, [lookup_legacy_accessor](Args args) {
        return lookup_legacy_accessor(args, true);
    });
    js_define_method(op, "__lookupSetter__", 1, [lookup_legacy_accessor](Args args) {
        return lookup_legacy_accessor(args, false);
    });

    js_define_method(op, "hasOwnProperty", 1, [](Args args) {
        return JsValue::make_boolean(self().has_own_property(js_to_property_key(js_arg(args, 0))));
    });
    js_define_method(op, "isPrototypeOf", 1, [](Args args) {
        JsValue me = self();
        const JsValue& candidate = js_arg(args, 0);
        // `Base.isPrototypeOf(Derived)`: a function is in a chain as its cell.
        const JsObject* mine = me.property_cell();
        if (!candidate.is_object_like() || !mine) return JsValue::make_boolean(false);
        int guard = 0;
        for (JsObject* obj = candidate.prototype_object(); obj && guard < kMaxPrototypeChain; obj = obj->prototype, ++guard) {
            if (obj == mine) return JsValue::make_boolean(true);
        }
        return JsValue::make_boolean(false);
    });
    js_define_method(op, "propertyIsEnumerable", 1, [](Args args) {
        JsValue me = self();
        std::string key = js_to_property_key(js_arg(args, 0));
        JsValue d = descriptor_for(me, key);
        return JsValue::make_boolean(d.is_object_like() && d.get_property("enumerable").is_truthy());
    });
    js_define_method(op, "toString", 0, [](Args) {
        return JsValue::make_string("[object " + object_tag(self()) + "]");
    });
    // §20.1.3.5: `Invoke(this, "toString")` — the method is looked up on the
    // receiver and called, so a `toString` an element's own prototype has is
    // the one asked, and `Boolean.prototype.toString = function () { return
    // typeof this; }` is visible through it. This was `js_to_string(self())`,
    // which for every object is "[object Object]" and asks nothing: test262's
    // `Array.prototype.toLocaleString/primitive_this_value` reached the
    // element's `toLocaleString` here and got that text (finding 196).
    js_define_method(op, "toLocaleString", 0, [](Args) {
        JsValue me = self();
        if (me.is_nullish()) {
            js_throw_type_error("Object.prototype.toLocaleString called on null or undefined");
        }
        JsValue to_string = me.get_property("toString");
        if (!to_string.is_function()) {
            js_throw_type_error("Object.prototype.toLocaleString: toString is not a function");
        }
        return to_string.call_with_this(me, {});
    });
    js_define_method(op, "valueOf", 0, [](Args) { return self(); });

    // ---- Object -----------------------------------------------------------
    JsValue object_ctor = js_make_native("Object", 1, [](Args args) {
        const JsValue& value = js_arg(args, 0);
        if (value.is_nullish()) return JsValue::make_object();
        return js_to_object(value);     // Object("a") is a String object
    });
    object_ctor.define_property("prototype", js_object_value(op), 0);
    js_define_hidden(op, "constructor", object_ctor);

    js_define_method(object_ctor, "keys", 1, [](Args args) {
        if (js_arg(args, 0).is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
        return make_string_array(reflected_own_keys(js_arg(args, 0), false));
    });
    js_define_method(object_ctor, "values", 1, [](Args args) {
        const JsValue& object = js_arg(args, 0);
        if (object.is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
        std::vector<JsValue> values;
        for (const auto& key : reflected_own_keys(object, false)) values.push_back(object.get_property(key));
        return JsValue::make_array(values);
    });
    js_define_method(object_ctor, "entries", 1, [](Args args) {
        const JsValue& object = js_arg(args, 0);
        if (object.is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
        std::vector<JsValue> entries;
        for (const auto& key : reflected_own_keys(object, false)) {
            entries.push_back(JsValue::make_array({JsValue::make_string(key), object.get_property(key)}));
        }
        return JsValue::make_array(entries);
    });
    js_define_method(object_ctor, "getOwnPropertyNames", 1, [](Args args) {
        if (js_arg(args, 0).is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
        return make_string_array(reflected_own_keys(js_arg(args, 0), true));
    });
    js_define_method(object_ctor, "getOwnPropertySymbols", 1, [](Args args) {
        std::vector<JsValue> symbols;
        if (PropertyMap* table = own_table(js_arg(args, 0), false)) {
            for (const auto& entry : *table) {
                JsValue symbol;
                if (js_symbol_from_key(entry.first, symbol)) symbols.push_back(symbol);
            }
        }
        return JsValue::make_array(symbols);
    });
    /// §20.1.2.1 `Object.assign`. Four things the signature makes look optional
    /// are not: the target is boxed with `ToObject` (so `Object.assign(12, "ab")`
    /// returns a Number object carrying `0` and `1`, and hands back the *box*,
    /// never the primitive); each copy is a strict `Set` — `[[Set]]`'s `false`
    /// throws a `TypeError` instead of dropping the value; the keys of a source
    /// are the *own* keys `[[OwnPropertyKeys]]` gives, so the symbols come too,
    /// after every string key and in creation order; and a source whose own key
    /// has gone missing between the key list and the read (`Proxy`) skips it
    /// rather than copying `undefined`.
    js_define_method(object_ctor, "assign", 2, [](Args args) {
        JsValue target = js_to_object(js_arg(args, 0));
        for (size_t i = 1; i < args.size(); ++i) {
            const JsValue& source = args[i];
            if (source.is_nullish()) continue;
            // `from` is the boxed source: a string source is read through its
            // characters, which only a String object answers for.
            JsValue from = js_to_object(source);
            for (const auto& key : assign_keys(from)) {
                // A key that was there for `[[OwnPropertyKeys]]` and is gone
                // now — a Proxy that answers differently twice — is not copied.
                if (!from.has_own_property(key)) continue;
                JsValue value = from.get_property(key);
                // `Set(target, key, value, true)`: no silent failures.
                if (!target.set_property(key, value)) {
                    js_throw_type_error("Cannot assign to read only property '" + key + "' of object");
                }
            }
        }
        return target;
    });
    /// A cross-origin WindowProxy or Location refuses [[PreventExtensions]]
    /// (HTML §7.2.3.4, §7.2.4.4); the Object methods report that as a
    /// TypeError, as ordinary non-configurable failures do.
    auto refuse_frozen_exotic = [&](const JsValue& object) {
        std::vector<std::string> exposed;
        if (litebird::dom::cross_origin_own_keys(object, true, exposed))
            js_throw_type_error("Cannot change the integrity level of a cross-origin Window or Location.");
    };
    js_define_method(object_ctor, "freeze", 1, [&](Args args) {
        const JsValue& object = js_arg(args, 0);
        refuse_frozen_exotic(object);
        if (JsObject* cell = object.property_cell(object.is_function())) {
            cell->frozen = true;
            cell->sealed = true;
            cell->extensible = false;
        }
        return object;
    });
    js_define_method(object_ctor, "isFrozen", 1, [](Args args) {
        const JsValue& object = js_arg(args, 0);
        if (!object.is_object_like()) return JsValue::make_boolean(true);
        return JsValue::make_boolean(test_integrity_level(object, /*frozen=*/true));
    });
    js_define_method(object_ctor, "seal", 1, [&](Args args) {
        const JsValue& object = js_arg(args, 0);
        refuse_frozen_exotic(object);
        if (JsObject* cell = object.property_cell(object.is_function())) {
            cell->sealed = true;
            cell->extensible = false;
        }
        return object;
    });
    js_define_method(object_ctor, "isSealed", 1, [](Args args) {
        const JsValue& object = js_arg(args, 0);
        if (!object.is_object_like()) return JsValue::make_boolean(true);
        return JsValue::make_boolean(test_integrity_level(object, /*frozen=*/false));
    });
    js_define_method(object_ctor, "preventExtensions", 1, [&](Args args) {
        const JsValue& object = js_arg(args, 0);
        refuse_frozen_exotic(object);
        if (JsObject* cell = object.property_cell(object.is_function())) cell->extensible = false;
        return object;
    });
    js_define_method(object_ctor, "isExtensible", 1, [](Args args) {
        const JsValue& object = js_arg(args, 0);
        if (!object.is_object_like()) return JsValue::make_boolean(false);
        JsObject* cell = object.property_cell();
        return JsValue::make_boolean(!cell || cell->extensible);
    });
    js_define_method(object_ctor, "getPrototypeOf", 1, [](Args args) {
        const JsValue& object = js_arg(args, 0);
        if (object.is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
        {
            // A cross-origin WindowProxy or Location has no prototype (HTML
            // §7.2.3.1, §7.2.4.1).
            std::vector<std::string> exposed;
            if (litebird::dom::cross_origin_own_keys(object, true, exposed)) return JsValue::make_null();
        }
        const auto& intrinsics = JsIntrinsics::get();
        JsObject* proto = nullptr;
        switch (object.type) {
            case JsType::String: proto = intrinsics.string_proto; break;
            case JsType::Number: proto = intrinsics.number_proto; break;
            case JsType::Boolean: proto = intrinsics.boolean_proto; break;
            case JsType::Symbol: proto = intrinsics.symbol_proto; break;
            default: proto = object.prototype_object(); break;
        }
        return proto ? JsValue::make_object(proto) : JsValue::make_null();
    });
    js_define_method(object_ctor, "setPrototypeOf", 2, [](Args args) {
        JsValue object = js_arg(args, 0);
        const JsValue& proto = js_arg(args, 1);
        if (object.type == JsType::Undefined || object.type == JsType::Null) {
            js_throw_type_error("Object.setPrototypeOf called on null or undefined");
        }
        {
            // A cross-origin WindowProxy or Location has an immutable
            // prototype (HTML §7.2.3.2, §7.2.4.2): it is never changed here.
            std::vector<std::string> exposed;
            if (litebird::dom::cross_origin_own_keys(object, true, exposed)) {
                if (proto.type != JsType::Null && !proto.is_object_like()) {
                    js_throw_type_error("Object prototype may only be an Object or null");
                }
                js_throw_type_error("Cannot set the prototype of a cross-origin Window or Location.");
            }
        }
        if (!proto.is_object_like() && proto.type != JsType::Null) {
            js_throw_type_error("Object prototype may only be an Object or null");
        }
        // A function of either kind gets a cell here: that is where its
        // prototype is kept once it is not Function.prototype.
        if (JsObject* cell = object.is_function() ? object.function_cell() : object.property_cell()) {
            JsObject* target = proto.type == JsType::Null ? nullptr : proto.as_prototype();
            if (!js_set_prototype(cell, target)) {
                js_throw_type_error(cell->extensible ? "Cyclic __proto__ value" : "Object is not extensible");
            }
        }
        return object;
    });
    js_define_method(object_ctor, "create", 2, [](Args args) {
        const JsValue& proto = js_arg(args, 0);
        if (!proto.is_object_like() && proto.type != JsType::Null) {
            js_throw_type_error("Object prototype may only be an Object or null");
        }
        JsValue object = JsValue::make_object();
        object.object_val->prototype = proto.type == JsType::Null ? nullptr : proto.as_prototype();
        const JsValue& props = js_arg(args, 1);
        if (props.is_object_like()) {
            for (const auto& key : reflected_own_keys(props, false)) define_from_descriptor(object, key, props.get_property(key));
        }
        return object;
    });
    js_define_method(object_ctor, "defineProperty", 3, [](Args args) {
        JsValue object = require_object(js_arg(args, 0), "Object.defineProperty");
        define_from_descriptor(object, js_to_property_key(js_arg(args, 1)), js_arg(args, 2));
        return object;
    });
    js_define_method(object_ctor, "defineProperties", 2, [](Args args) {
        JsValue object = require_object(js_arg(args, 0), "Object.defineProperties");
        const JsValue& props = js_arg(args, 1);
        if (props.is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
        for (const auto& key : reflected_own_keys(props, false)) define_from_descriptor(object, key, props.get_property(key));
        return object;
    });
    js_define_method(object_ctor, "getOwnPropertyDescriptor", 2, [](Args args) {
        if (js_arg(args, 0).is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
        return descriptor_for(js_arg(args, 0), js_to_property_key(js_arg(args, 1)));
    });
    js_define_method(object_ctor, "getOwnPropertyDescriptors", 1, [](Args args) {
        const JsValue& object = js_arg(args, 0);
        JsValue result = JsValue::make_object();
        // §20.1.2.9 walks `[[OwnPropertyKeys]]`, so a symbol key gets a
        // symbol-keyed descriptor entry: the same list `Reflect.ownKeys`
        // reports, strings and then symbols, and every key rather than the
        // enumerable ones `Object.assign` copies.
        for (const auto& key : assign_keys(object, false)) {
            JsValue descriptor = descriptor_for(object, key);
            // `assign_keys` answers the internal key a symbol is stored under;
            // `js_to_property_key` is what turns a symbol back into it, so the
            // descriptor lands under the symbol rather than under its text.
            JsValue symbol;
            const std::string& property_key =
                js_symbol_from_key(key, symbol) ? js_to_property_key(symbol) : key;
            result.set_property(property_key, descriptor);
        }
        return result;
    });
    js_define_method(object_ctor, "fromEntries", 1, [](Args args) {
        JsValue result = JsValue::make_object();
        js_iterate(js_arg(args, 0), [&](const JsValue& entry) {
            if (!entry.is_object_like()) js_throw_type_error("Iterator value is not an entry object");
            result.set_property(js_to_property_key(entry.get_property("0")), entry.get_property("1"));
            return true;
        });
        return result;
    });
    js_define_method(object_ctor, "is", 2, [](Args args) {
        return JsValue::make_boolean(js_same_value(js_arg(args, 0), js_arg(args, 1)));
    });
    js_define_method(object_ctor, "hasOwn", 2, [](Args args) {
        return JsValue::make_boolean(js_arg(args, 0).has_own_property(js_to_property_key(js_arg(args, 1))));
    });
    js_define_method(object_ctor, "groupBy", 2, [](Args args) {
        JsValue result = JsValue::make_object();
        result.object_val->prototype = nullptr;
        const JsValue callback = js_arg(args, 1);
        double index = 0;
        js_iterate(js_arg(args, 0), [&](const JsValue& item) {
            std::string key = js_to_property_key(js_call(callback, JsValue(), {item, JsValue::make_number(index++)}));
            JsValue bucket = result.get_property(key);
            if (!bucket.is_array()) {
                bucket = JsValue::make_array();
                result.set_property(key, bucket);
            }
            bucket.object_val->array_elements.push_back(item);
            return true;
        });
        return result;
    });
    realm.add_global("Object", object_ctor);

    // ---- Function.prototype -----------------------------------------------
    JsObject* fp = realm.function_proto;
    fp->klass = ObjectClass::Function;

    js_define_method(fp, "call", 1, [](Args args) {
        JsValue fn = self();
        if (!fn.is_function()) js_throw_type_error("Function.prototype.call called on a non-function");
        std::vector<JsValue> rest(args.size() > 1 ? args.begin() + 1 : args.end(), args.end());
        return fn.call_with_this(js_arg(args, 0), rest);
    });
    js_define_method(fp, "apply", 2, [](Args args) {
        JsValue fn = self();
        if (!fn.is_function()) js_throw_type_error("Function.prototype.apply called on a non-function");
        return fn.call_with_this(js_arg(args, 0), array_like_to_vector(js_arg(args, 1)));
    });
    js_define_method(fp, "bind", 1, [](Args args) {
        JsValue target = self();
        if (!target.is_function()) js_throw_type_error("Bind must be called on a function");
        JsValue bound_this = js_arg(args, 0);
        std::vector<JsValue> bound_args(args.size() > 1 ? args.begin() + 1 : args.end(), args.end());
        // What the entry point needs is kept where the collector can see it
        // (see JsValue::Extra::captured). A std::function's captures are not
        // traced: a target that lived only inside one made every copy of this
        // bound function look like the payload's one owner, the payload became
        // a root, and the target -- with its closure, and the variables the
        // closure holds -- was never collected.
        //
        // The captures are the target, the bound `this`, then the arguments
        // already bound, in that order.
        JsValue bound = JsValue::make_native_function([](Args call_args) {
            const std::vector<JsValue>& kept = JsValue::current_captured();
            if (kept.size() < 2) return JsValue::make_undefined();
            std::vector<JsValue> all(kept.begin() + 2, kept.end());
            all.insert(all.end(), call_args.begin(), call_args.end());
            // `new boundFn()` constructs the target and ignores the bound
            // `this`. Inside, new.target is the target, or whatever function
            // Reflect.construct or a subclass's super() named instead.
            if (JsValue::current_is_construct()) {
                return js_construct(kept[0], all, JsValue::current_new_target());
            }
            return kept[0].call_with_this(kept[1], all);
        });
        std::vector<JsValue> captures;
        captures.reserve(2 + bound_args.size());
        captures.push_back(target);
        captures.push_back(bound_this);
        captures.insert(captures.end(), bound_args.begin(), bound_args.end());
        bound.set_payload_captured(std::move(captures));
        bound.set_forwards_construct();
        // `instanceof` has no `prototype` to walk to on a bound function: the
        // language asks the target, and this is where the target is kept.
        bound.set_bound_target(target);
        double length = js_to_number(target.get_property("length")) - static_cast<double>(bound_args.size());
        bound.define_property("name", JsValue::make_string("bound " + js_to_string(target.get_property("name"))),
                              PropertyMap::Configurable);
        bound.define_property("length", JsValue::make_number(std::max(0.0, length)), PropertyMap::Configurable);
        // A bound function has the prototype of its target: a bound class
        // still finds the static members of the class's parent.
        JsObject* target_proto = target.prototype_object();
        if (target_proto != JsIntrinsics::get().function_proto) bound.function_cell()->prototype = target_proto;
        return bound;
    });
    js_define_method(fp, "toString", 0, [](Args) {
        JsValue fn = self();
        if (!fn.is_function()) js_throw_type_error("Function.prototype.toString requires that 'this' be a Function");
        return JsValue::make_string(fn.to_string());
    });
    // `Function.prototype[Symbol.hasInstance]`: the walk `instanceof` makes,
    // as a method a script can call and a class can inherit. It is what every
    // function answers with, so the operator recognizes it by identity and
    // does not go through the property at all for the ordinary case. The
    // language makes it non-writable and non-configurable: a script cannot
    // take it away from every function at once.
    JsValue has_instance = js_make_native("[Symbol.hasInstance]", 1, [](Args args) {
        const JsValue& fn = JsValue::current_this();
        if (!fn.is_function()) return JsValue::make_boolean(false);
        return JsValue::make_boolean(js_ordinary_has_instance(fn, js_arg(args, 0)));
    });
    js_object_value(fp).define_property("@@hasInstance", has_instance, 0);
    realm.function_has_instance_method = has_instance;

    // ---- Boolean ----------------------------------------------------------
    JsValue boolean_ctor = js_make_native("Boolean", 1, [](Args args) {
        JsValue flag = JsValue::make_boolean(js_arg(args, 0).is_truthy());
        // `new Boolean(false)` is an object, and so truthy; `Boolean(0)` converts.
        return JsValue::current_is_construct() ? js_to_object(flag) : flag;
    });
    boolean_ctor.define_property("prototype", js_object_value(realm.boolean_proto), 0);
    js_define_hidden(realm.boolean_proto, "constructor", boolean_ctor);
    // The boolean `this` stands for: itself, or the one inside a Boolean object.
    auto this_boolean = [](const char* method) {
        const JsValue& me = JsValue::current_this();
        if (me.type == JsType::Boolean) return me.bool_val;
        if (const JsValue* inner = js_wrapped_primitive(me); inner && inner->type == JsType::Boolean) {
            return inner->bool_val;
        }
        js_throw_type_error(std::string("Boolean.prototype.") + method + " requires that 'this' be a Boolean");
    };
    js_define_method(realm.boolean_proto, "toString", 0, [this_boolean](Args) {
        return JsValue::make_string(this_boolean("toString") ? "true" : "false");
    });
    js_define_method(realm.boolean_proto, "valueOf", 0, [this_boolean](Args) {
        return JsValue::make_boolean(this_boolean("valueOf"));
    });
    realm.add_global("Boolean", boolean_ctor);

    // ---- Symbol -----------------------------------------------------------
    JsValue symbol_ctor = js_make_native("Symbol", 0, [](Args args) {
        if (JsValue::current_is_construct()) js_throw_type_error("Symbol is not a constructor");
        return js_new_symbol(js_arg(args, 0).is_undefined() ? std::string() : js_to_string(js_arg(args, 0)));
    });
    symbol_ctor.define_property("prototype", js_object_value(realm.symbol_proto), 0);
    js_define_hidden(realm.symbol_proto, "constructor", symbol_ctor);
    for (const auto& [name, key] : {std::pair<const char*, const char*>{"iterator", kIteratorKey},
                                    {"asyncIterator", kAsyncIteratorKey},
                                    {"hasInstance", "@@hasInstance"},
                                    {"toPrimitive", "@@toPrimitive"},
                                    {"toStringTag", "@@toStringTag"},
                                    {"unscopables", "@@unscopables"}}) {
        symbol_ctor.define_property(name, js_symbol_for_key(key, std::string("Symbol.") + name), 0);
    }
    js_define_method(symbol_ctor, "for", 1, [](Args args) {
        std::string key = js_to_string(js_arg(args, 0));
        return js_symbol_for_key("@@for:" + key, key);
    });
    js_define_method(symbol_ctor, "keyFor", 1, [](Args args) {
        const JsValue& symbol = js_arg(args, 0);
        if (symbol.type != JsType::Symbol) js_throw_type_error("Symbol.keyFor requires a symbol");
        const std::string_view key = symbol.string_val.view();
        if (key.substr(0, 6) == "@@for:") return JsValue::make_string(key.substr(6));
        return JsValue::make_undefined();
    });
    // The symbol `this` stands for: itself, or the one inside a Symbol object.
    auto this_symbol = []() -> JsValue {
        const JsValue& me = JsValue::current_this();
        if (me.type == JsType::Symbol) return me;
        if (const JsValue* inner = js_wrapped_primitive(me); inner && inner->type == JsType::Symbol) return *inner;
        js_throw_type_error("Symbol.prototype method called on a value that is not a symbol");
    };
    js_define_method(realm.symbol_proto, "toString", 0, [this_symbol](Args) {
        return JsValue::make_string(this_symbol().to_string());
    });
    js_define_method(realm.symbol_proto, "valueOf", 0, [this_symbol](Args) { return this_symbol(); });
    js_define_getter(realm.symbol_proto, "description", [this_symbol](Args) {
        JsValue symbol = this_symbol();
        if (!symbol.properties) return JsValue::make_undefined();
        return (*symbol.properties)["description"];
    });
    realm.add_global("Symbol", symbol_ctor);

    // ---- Reflect ----------------------------------------------------------
    JsValue reflect = JsValue::make_object();
    js_define_method(reflect, "ownKeys", 1, [](Args args) {
        // §28.1.11 is [[OwnPropertyKeys]], which includes the symbols; the array
        // it answers with holds the symbols themselves, not their descriptions.
        // `assign_keys` is the list that knows the order (every string key, then
        // the symbols in creation order) and `reflected_own_keys` is the one that
        // knows the cross-origin Window/Location rule, so this takes the
        // cross-origin answer first and falls back to the copy list.
        JsValue object = require_object(js_arg(args, 0), "Reflect.ownKeys");
        std::vector<std::string> keys;
        if (!litebird::dom::cross_origin_own_keys(object, true, keys)) keys = assign_keys(object, false);
        std::vector<JsValue> values;
        values.reserve(keys.size());
        for (const std::string& key : keys) {
            // A symbol key's internal text resolves back to the symbol itself.
            JsValue symbol;
            if (js_symbol_from_key(key, symbol)) values.push_back(symbol);
            else values.push_back(JsValue::make_string(key));
        }
        return JsValue::make_array(values);
    });
    js_define_method(reflect, "has", 2, [](Args args) {
        return JsValue::make_boolean(require_object(js_arg(args, 0), "Reflect.has")
                                         .has_property(js_to_property_key(js_arg(args, 1))));
    });
    js_define_method(reflect, "get", 2, [](Args args) {
        return require_object(js_arg(args, 0), "Reflect.get").get_property(js_to_property_key(js_arg(args, 1)));
    });
    js_define_method(reflect, "set", 3, [](Args args) {
        JsValue target = require_object(js_arg(args, 0), "Reflect.set");
        target.set_property(js_to_property_key(js_arg(args, 1)), js_arg(args, 2));
        return JsValue::make_boolean(true);
    });
    js_define_method(reflect, "deleteProperty", 2, [](Args args) {
        JsValue target = require_object(js_arg(args, 0), "Reflect.deleteProperty");
        return JsValue::make_boolean(target.delete_property(js_to_property_key(js_arg(args, 1))));
    });
    js_define_method(reflect, "setPrototypeOf", 2, [](Args args) {
        {
            const JsValue& target = require_object(js_arg(args, 0), "Reflect.setPrototypeOf");
            std::vector<std::string> exposed;
            if (litebird::dom::cross_origin_own_keys(target, true, exposed)) return JsValue::make_boolean(false);
        }
        JsValue object = require_object(js_arg(args, 0), "Reflect.setPrototypeOf");
        const JsValue& proto = js_arg(args, 1);
        if (!proto.is_object_like() && proto.type != JsType::Null) {
            js_throw_type_error("Object prototype may only be an Object or null");
        }
        JsObject* cell = object.is_function() ? object.function_cell() : object.property_cell();
        if (!cell) return JsValue::make_boolean(false);
        JsObject* target = proto.type == JsType::Null ? nullptr : proto.as_prototype();
        // Unlike Object.setPrototypeOf, a refused change (a loop, an object
        // that is not extensible) is reported, not thrown.
        return JsValue::make_boolean(js_set_prototype(cell, target));
    });
    js_define_method(reflect, "getPrototypeOf", 1, [](Args args) {
        const JsValue& reflected = require_object(js_arg(args, 0), "Reflect.getPrototypeOf");
        {
            std::vector<std::string> exposed;
            if (litebird::dom::cross_origin_own_keys(reflected, true, exposed)) return JsValue::make_null();
        }
        JsObject* proto = reflected.prototype_object();
        return proto ? JsValue::make_object(proto) : JsValue::make_null();
    });
    js_define_method(reflect, "defineProperty", 3, [](Args args) {
        // Reports a refusal where Object.defineProperty throws.
        return JsValue::make_boolean(define_from_descriptor(require_object(js_arg(args, 0), "Reflect.defineProperty"),
                                                            js_to_property_key(js_arg(args, 1)), js_arg(args, 2), false));
    });
    /// Reflect's three integrity calls are the strict half of the Object
    /// ones: a primitive has no [[GetOwnProperty]] or [[IsExtensible]] to
    /// ask, and the language throws a TypeError rather than wrapping it.
    /// (`Object.getOwnPropertyDescriptor("ab", "1")` answers, because it
    /// ToObjects first; `Reflect.getOwnPropertyDescriptor("ab", "1")` throws.)
    auto require_reflect_target = [](const JsValue& value, const char* where) -> const JsValue& {
        if (!value.is_object_like()) {
            js_throw_type_error(std::string(where) + " called on non-object");
        }
        return value;
    };
    js_define_method(reflect, "getOwnPropertyDescriptor", 2, [require_reflect_target](Args args) {
        // ECMA-262 §28.1.6. The descriptor is the receiver's own, by the same
        // descriptor_for Object.getOwnPropertyDescriptor uses, so an array's
        // indices and `length`, a string object's indices, a function's
        // `prototype` and an accessor all describe themselves here as there.
        const JsValue& object = require_reflect_target(js_arg(args, 0), "Reflect.getOwnPropertyDescriptor");
        return descriptor_for(object, js_to_property_key(js_arg(args, 1)));
    });
    js_define_method(reflect, "isExtensible", 1, [require_reflect_target](Args args) {
        // §28.1.7. A cross-origin Window or Location is never extensible
        // (HTML §7.2.3.4, §7.2.4.4): Object.isExtensible reports false on it,
        // and so does this, rather than throwing as the integrity-level
        // Object calls do.
        const JsValue& object = require_reflect_target(js_arg(args, 0), "Reflect.isExtensible");
        {
            std::vector<std::string> exposed;
            if (litebird::dom::cross_origin_own_keys(object, true, exposed)) return JsValue::make_boolean(false);
        }
        // A script function with no cell yet has one made for it here: it is
        // extensible until something asks, and the answer must not depend on
        // whether a cell happened to exist.
        JsObject* cell = object.property_cell(object.is_function());
        return JsValue::make_boolean(cell && cell->extensible);
    });
    js_define_method(reflect, "preventExtensions", 1, [require_reflect_target](Args args) {
        // §28.1.8. Reports where Object.preventExtensions throws: `true` when
        // the object became non-extensible (or already was — the call is
        // idempotent), `false` on a cross-origin Window or Location, whose
        // [[PreventExtensions]] refuses and which changes nothing.
        const JsValue& object = require_reflect_target(js_arg(args, 0), "Reflect.preventExtensions");
        {
            std::vector<std::string> exposed;
            if (litebird::dom::cross_origin_own_keys(object, true, exposed)) return JsValue::make_boolean(false);
        }
        // A native function with no cell keeps its properties in a table; a
        // cell is what has the extensible bit, so one is made for it. (The
        // properties it had are carried over by function_cell().)
        JsObject* cell = object.property_cell(object.is_function());
        if (cell) cell->extensible = false;
        return JsValue::make_boolean(true);
    });
    js_define_method(reflect, "apply", 3, [](Args args) {
        return js_call(js_arg(args, 0), js_arg(args, 1), array_like_to_vector(js_arg(args, 2)));
    });
    js_define_method(reflect, "construct", 2, [](Args args) {
        // The third argument is new.target: whose `prototype` the new object
        // gets. Compiled classes are built on it (Babel's `_createSuper`
        // calls `Reflect.construct(Super, arguments, NewTarget)`).
        const JsValue target = js_arg(args, 0);
        const JsValue* new_target = args.size() > 2 ? &args[2] : nullptr;
        // The list of arguments has to be an object, and it is looked at
        // only once both functions are known to be constructors.
        std::vector<JsValue> list;
        if (js_is_constructor(target) && (!new_target || js_is_constructor(*new_target))) {
            if (!js_arg(args, 1).is_object_like()) js_throw_type_error("CreateListFromArrayLike called on non-object");
            list = array_like_to_vector(js_arg(args, 1));
        }
        return js_construct(target, list, new_target);      // refuses what is not a constructor, in its own words
    });
    realm.add_global("Reflect", reflect);

    // ---- Miscellaneous globals --------------------------------------------
    // structuredClone(value, { transfer: [...] }) — HTML §2.7.7. The options
    // bag is read before the value is, as the standard orders it, so a bad
    // transfer list throws before any getter in the graph runs.
    realm.add_global("structuredClone", js_make_native("structuredClone", 1, [](Args args) {
        CloneState state;
        const JsValue& options = js_arg(args, 1);
        if (options.is_object_like()) transfer_buffers(options.get_property("transfer"), state);
        return structured_clone(js_arg(args, 0), state);
    }));
}

// ---------------------------------------------------------------------------
// Error and its subclasses
// ---------------------------------------------------------------------------

JsObject* JsIntrinsics::aggregate_error_prototype() {
    if (aggregate_error_proto) return aggregate_error_proto;
    JsObject* proto = Heap::default_heap().allocate_object();
    // `[[Prototype]]` is `Error.prototype`, and it is filled in by
    // install_error_builtins(): that is where `Error.prototype` is complete,
    // and §20.5.7.3 leaves `name` and `message` to the constructor's install.
    proto->prototype = error_protos[static_cast<int>(JsErrorKind::Error)];
    m_roots.push_back(JsValue::make_object(proto));
    aggregate_error_proto = proto;
    return proto;
}

void install_error_builtins(JsIntrinsics& realm) {
    JsValue error_ctor;     // `Error`, the prototype of the other six constructors
    for (int i = 0; i < 7; ++i) {
        const JsErrorKind kind = static_cast<JsErrorKind>(i);
        JsObject* proto = realm.error_protos[i];
        const char* name = kErrorNames[i];

        // Callable with or without `new`, as the built-in error types are.
        JsValue ctor = js_make_native(name, 1, [kind](Args args) {
            const JsValue& message = js_arg(args, 0);
            JsValue error = js_make_error(kind, message.is_undefined() ? std::string() : js_to_string(message));
            const JsValue& options = js_arg(args, 1);
            if (options.is_object_like() && options.has_property("cause")) {
                error.define_property("cause", options.get_property("cause"), PropertyMap::Hidden);
            }
            return error;
        });
        ctor.define_property("prototype", js_object_value(proto), 0);
        js_define_hidden(proto, "constructor", ctor);
        js_define_hidden(proto, "name", JsValue::make_string(name));
        js_define_hidden(proto, "message", JsValue::make_string(""));
        if (i == 0) {
            js_define_method(proto, "toString", 0, [](Args) {
                JsValue error = self();
                if (!error.is_object_like()) js_throw_type_error("Error.prototype.toString called on non-object");
                JsValue name_value = error.get_property("name");
                JsValue message_value = error.get_property("message");
                std::string error_name = name_value.is_undefined() ? "Error" : js_to_string(name_value);
                std::string message = message_value.is_undefined() ? "" : js_to_string(message_value);
                if (error_name.empty()) return JsValue::make_string(message);
                if (message.empty()) return JsValue::make_string(error_name);
                return JsValue::make_string(error_name + ": " + message);
            });
            // V8-specific but called unconditionally by a lot of library code.
            js_define_method(ctor, "captureStackTrace", 1, [](Args args) {
                JsValue target = js_arg(args, 0);
                // Nothing to install: `stack` is the inherited accessor on
                // Error.prototype, reported from the [[ErrorData]] slot, so a
                // target that has the slot already answers the same text. A
                // target that is not an error gets an ordinary own property
                // through the same setter an assignment would use.
                if (target.is_object_like() && !target.has_own_property("stack")) {
                    JsValue text = JsValue::make_string(js_describe_thrown(target) + "\n    at <anonymous>");
                    target.set_property("stack", text);
                }
                return JsValue::make_undefined();
            });
        }
        // `Object.getPrototypeOf(TypeError) === Error`. Code that asks whether
        // a constructor is an error type walks this chain (the
        // assert_throws_js of web-platform-tests does), and so does a lookup
        // of `TypeError.captureStackTrace`.
        if (i == 0) error_ctor = ctor;
        else js_set_prototype(ctor.function_cell(), error_ctor.as_prototype());
        realm.add_global(name, ctor);
    }

    // ---- AggregateError -----------------------------------------------------
    // §20.5.7.1. The eighth error type, and the only one that is not a kind
    // in `JsErrorKind`: its own value is a list, which the others have no
    // place for, and it is made by this file rather than by the realm's
    // constructor (see JsIntrinsics::aggregate_error_prototype).
    JsObject* proto = realm.aggregate_error_prototype();
    JsValue aggregate_ctor = js_make_native("AggregateError", 2, [](Args args) {
        // 2. Let message be ? ToString(message), when it is present: before
        //    the errors are iterated, as the standard orders the steps.
        const JsValue& message = js_arg(args, 1);
        const bool has_message = !message.is_undefined();
        std::string message_text;
        if (has_message) message_text = js_to_string(message);
        // 3. IterableToList(errors): a TypeError when it has no iterator, and
        //    the iterator is closed if reading it throws.
        std::vector<JsValue> list = js_iterable_to_vector(js_arg(args, 0));
        // 4-5. The object, with [[AggregateErrors]] and [[ErrorData]]. The
        //      prototype is the constructor's, so `new sub()` is an instance
        //      of sub and of AggregateError.
        // The cell is made here rather than by js_make_error, which always
        // defines `message`: §20.5.7.1 step 4 gives one only when the
        // argument was not undefined, and the harness of test262 checks it.
        JsObject* cell = Heap::default_heap().allocate_object();
        cell->klass = ObjectClass::Error;
        cell->prototype = JsIntrinsics::get().aggregate_error_prototype();
        const JsValue* target = JsValue::current_new_target();
        if (target && target->is_object_like()) {
            JsValue prototype = target->get_property("prototype");
            if (prototype.is_object_like()) cell->prototype = prototype.object_val.get();
        }
        JsValue error = JsValue::make_object(cell);
        if (has_message) error.define_property("message", JsValue::make_string(message_text), PropertyMap::Hidden);
        // 6. InstallErrorCause(options).
        const JsValue& options = js_arg(args, 2);
        if (options.is_object_like() && options.has_property("cause")) {
            error.define_property("cause", options.get_property("cause"), PropertyMap::Hidden);
        }
        // 7. CreateNonEnumerableDataPropertyOrThrow(O, "errors", errorsList).
        error.define_property("errors", JsValue::make_array(std::move(list)), PropertyMap::Hidden);
        return error;
    });
    aggregate_ctor.define_property("prototype", js_object_value(proto), 0);
    js_define_hidden(proto, "constructor", aggregate_ctor);
    js_define_hidden(proto, "name", JsValue::make_string("AggregateError"));
    js_define_hidden(proto, "message", JsValue::make_string(""));
    // `Object.getPrototypeOf(AggregateError) === Error`, as for the other six.
    js_set_prototype(aggregate_ctor.function_cell(), error_ctor.as_prototype());
    realm.add_global("AggregateError", aggregate_ctor);
}

} // namespace litebird
