// Object, Function.prototype, Boolean, Symbol, Reflect, the Error family and a
// few global functions.

#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Heap.hpp"
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
        const auto& elements = value.object_val->array_elements;
        return std::vector<JsValue>(elements.begin(), elements.end());
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

/// The table that holds `value`'s own named properties, if it has one.
PropertyMap* own_table(const JsValue& value, bool create) {
    if (JsObject* cell = value.property_cell(create)) return &cell->properties;
    return value.loose_properties(create);
}

JsValue descriptor_for(const JsValue& object, const std::string& key) {
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
    if (cell && cell->is_array) {
        if (key == "length") return data(JsValue::make_number(static_cast<double>(cell->array_elements.size())), true, false, false);
        if (parse_index(key, index)) {
            if (index < cell->array_elements.size()) return data(cell->array_elements[index], true, true, true);
            return JsValue::make_undefined();
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
    return JsValue::make_undefined();
}

/// `Object.defineProperty(object, key, descriptor)`. Returns false, or throws
/// when `should_throw` is set, where the property that is there does not
/// allow the change (`Reflect.defineProperty` reports, the others throw).
bool define_from_descriptor(JsValue object, const std::string& key, const JsValue& descriptor,
                            bool should_throw = true) {
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
    size_t index = 0;
    if (cell && cell->is_array && !wants_accessor && (parse_index(key, index) || key == "length")) {
        if (value.present) object.set_property(key, value.value);
        return true;
    }

    auto refuse = [&](const std::string& message) {
        if (should_throw) js_throw_type_error(message);
        return false;
    };

    PropertyMap* table = own_table(object, true);
    if (!table) js_throw_type_error("Object.defineProperty called on non-object");
    auto existing = table->find(key);
    const bool exists = existing != table->end();
    if (!exists && cell && !cell->extensible) {
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

    uint8_t flags = exists ? existing->flags : 0;
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
    if (value.promise_value()) return "Promise";
    return "Object";
}

JsValue structured_clone(const JsValue& value, std::unordered_map<const void*, JsValue>& seen) {
    js_check_stack();
    if (!value.is_object_like()) return value;
    if (value.is_function() || value.dom_node_value()) {
        js_throw_error(JsErrorKind::Error, "DataCloneError: value could not be cloned");
    }
    auto it = seen.find(value.identity());
    if (it != seen.end()) return it->second;

    JsObject* cell = value.object_val;
    if (cell->is_array) {
        JsValue copy = JsValue::make_array();
        seen.emplace(value.identity(), copy);
        for (const auto& item : cell->array_elements) {
            copy.object_val->array_elements.push_back(structured_clone(item, seen));
        }
        return copy;
    }
    JsValue copy = JsValue::make_object();
    seen.emplace(value.identity(), copy);
    if (cell->klass == ObjectClass::Date || cell->klass == ObjectClass::Error) {
        copy.object_val->prototype = cell->prototype;
        copy.object_val->klass = cell->klass;
        copy.object_val->internal = cell->internal;
    }
    for (const auto& key : value.own_keys(cell->klass == ObjectClass::Error)) {
        copy.set_property(key, structured_clone(value.get_property(key), seen));
    }
    return copy;
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
    js_define_method(op, "toLocaleString", 0, [](Args) {
        JsValue me = self();
        return JsValue::make_string(js_to_string(me));
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
        return make_string_array(js_arg(args, 0).own_keys());
    });
    js_define_method(object_ctor, "values", 1, [](Args args) {
        const JsValue& object = js_arg(args, 0);
        if (object.is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
        std::vector<JsValue> values;
        for (const auto& key : object.own_keys()) values.push_back(object.get_property(key));
        return JsValue::make_array(values);
    });
    js_define_method(object_ctor, "entries", 1, [](Args args) {
        const JsValue& object = js_arg(args, 0);
        if (object.is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
        std::vector<JsValue> entries;
        for (const auto& key : object.own_keys()) {
            entries.push_back(JsValue::make_array({JsValue::make_string(key), object.get_property(key)}));
        }
        return JsValue::make_array(entries);
    });
    js_define_method(object_ctor, "getOwnPropertyNames", 1, [](Args args) {
        if (js_arg(args, 0).is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
        return make_string_array(js_arg(args, 0).own_keys(true));
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
    js_define_method(object_ctor, "assign", 2, [](Args args) {
        JsValue target = js_arg(args, 0);
        if (target.is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i].is_nullish()) continue;
            for (const auto& key : args[i].own_keys()) target.set_property(key, args[i].get_property(key));
        }
        return target;
    });
    js_define_method(object_ctor, "freeze", 1, [](Args args) {
        const JsValue& object = js_arg(args, 0);
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
        JsObject* cell = object.property_cell();
        return JsValue::make_boolean(cell && cell->frozen);
    });
    js_define_method(object_ctor, "seal", 1, [](Args args) {
        const JsValue& object = js_arg(args, 0);
        if (JsObject* cell = object.property_cell(object.is_function())) {
            cell->sealed = true;
            cell->extensible = false;
        }
        return object;
    });
    js_define_method(object_ctor, "isSealed", 1, [](Args args) {
        const JsValue& object = js_arg(args, 0);
        if (!object.is_object_like()) return JsValue::make_boolean(true);
        JsObject* cell = object.property_cell();
        return JsValue::make_boolean(cell && (cell->sealed || cell->frozen));
    });
    js_define_method(object_ctor, "preventExtensions", 1, [](Args args) {
        const JsValue& object = js_arg(args, 0);
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
            for (const auto& key : props.own_keys()) define_from_descriptor(object, key, props.get_property(key));
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
        for (const auto& key : props.own_keys()) define_from_descriptor(object, key, props.get_property(key));
        return object;
    });
    js_define_method(object_ctor, "getOwnPropertyDescriptor", 2, [](Args args) {
        if (js_arg(args, 0).is_nullish()) js_throw_type_error("Cannot convert undefined or null to object");
        return descriptor_for(js_arg(args, 0), js_to_property_key(js_arg(args, 1)));
    });
    js_define_method(object_ctor, "getOwnPropertyDescriptors", 1, [](Args args) {
        const JsValue& object = js_arg(args, 0);
        JsValue result = JsValue::make_object();
        for (const auto& key : object.own_keys(true)) result.set_property(key, descriptor_for(object, key));
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
        JsValue bound = JsValue::make_native_function([target, bound_this, bound_args](Args call_args) {
            std::vector<JsValue> all = bound_args;
            all.insert(all.end(), call_args.begin(), call_args.end());
            // `new boundFn()` constructs the target and ignores the bound `this`.
            if (JsValue::current_is_construct()) return js_construct(target, all);
            return target.call_with_this(bound_this, all);
        });
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
                                    {"toStringTag", "@@toStringTag"}}) {
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
        return make_string_array(require_object(js_arg(args, 0), "Reflect.ownKeys").own_keys(true));
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
        JsObject* proto = require_object(js_arg(args, 0), "Reflect.getPrototypeOf").prototype_object();
        return proto ? JsValue::make_object(proto) : JsValue::make_null();
    });
    js_define_method(reflect, "defineProperty", 3, [](Args args) {
        // Reports a refusal where Object.defineProperty throws.
        return JsValue::make_boolean(define_from_descriptor(require_object(js_arg(args, 0), "Reflect.defineProperty"),
                                                            js_to_property_key(js_arg(args, 1)), js_arg(args, 2), false));
    });
    js_define_method(reflect, "apply", 3, [](Args args) {
        return js_call(js_arg(args, 0), js_arg(args, 1), array_like_to_vector(js_arg(args, 2)));
    });
    js_define_method(reflect, "construct", 2, [](Args args) {
        // The third argument is new.target: whose `prototype` the new object
        // gets. Compiled classes are built on it (Babel's `_createSuper`
        // calls `Reflect.construct(Super, arguments, NewTarget)`).
        return js_construct(js_arg(args, 0), array_like_to_vector(js_arg(args, 1)),
                            args.size() > 2 ? &args[2] : nullptr);
    });
    realm.add_global("Reflect", reflect);

    // ---- Miscellaneous globals --------------------------------------------
    realm.add_global("structuredClone", js_make_native("structuredClone", 1, [](Args args) {
        std::unordered_map<const void*, JsValue> seen;
        return structured_clone(js_arg(args, 0), seen);
    }));
}

// ---------------------------------------------------------------------------
// Error and its subclasses
// ---------------------------------------------------------------------------

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
                if (target.is_object_like() && !target.has_own_property("stack")) {
                    target.define_property("stack", JsValue::make_string(js_describe_thrown(target) + "\n    at <anonymous>"),
                                           PropertyMap::Hidden);
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
}

} // namespace litebird
