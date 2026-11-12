// Map, Set, WeakMap and WeakSet.
//
// Keys compare with SameValueZero: 1 and "1" are different keys, NaN equals
// NaN, and objects are keyed by identity. Entries keep insertion order, and an
// iterator stays valid while entries are added or deleted under it.

#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Heap.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <unordered_map>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

JsValue self() { return JsValue::current_this(); }

struct CollectionData {
    struct Entry {
        JsValue key;
        JsValue value;
        bool deleted{false};
    };

    std::vector<Entry> entries;
    std::unordered_map<std::string, size_t> index;      // hash key -> position in `entries`
    size_t live{0};
    std::vector<std::weak_ptr<size_t>> cursors;         // positions held by live iterators

    /// A string that is equal for two keys exactly when SameValueZero says so.
    static std::string hash_key(const JsValue& key) {
        switch (key.type) {
            case JsType::Undefined: return "u";
            case JsType::Null: return "l";
            case JsType::Boolean: return key.bool_val ? "b1" : "b0";
            case JsType::Number:
                if (std::isnan(key.number_val)) return "nNaN";
                if (key.number_val == 0.0) return "n0";         // +0 and -0 are the same key
                return "n" + js_number_to_string(key.number_val);
            case JsType::String: return "s" + key.string_val.str();
            case JsType::Symbol: return "y" + key.string_val.str();
            default: {
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "o%p", key.identity());
                return buffer;
            }
        }
    }

    Entry* find(const JsValue& key) {
        auto it = index.find(hash_key(key));
        return it == index.end() ? nullptr : &entries[it->second];
    }

    void set(const JsValue& key, const JsValue& value) {
        std::string hash = hash_key(key);
        auto it = index.find(hash);
        if (it != index.end()) {
            entries[it->second].value = value;
            return;
        }
        Entry entry;
        // -0 is stored as +0, as the specification requires.
        entry.key = (key.type == JsType::Number && key.number_val == 0.0) ? JsValue::make_number(0) : key;
        entry.value = value;
        index.emplace(std::move(hash), entries.size());
        entries.push_back(std::move(entry));
        ++live;
    }

    bool remove(const JsValue& key) {
        auto it = index.find(hash_key(key));
        if (it == index.end()) return false;
        Entry& entry = entries[it->second];
        entry.deleted = true;
        entry.key = JsValue::make_undefined();
        entry.value = JsValue::make_undefined();
        index.erase(it);
        --live;
        compact_if_worthwhile();
        return true;
    }

    void clear() {
        // Iterators positioned anywhere are now past the end.
        for (auto& weak : cursors) {
            if (auto cursor = weak.lock()) *cursor = 0;
        }
        entries.clear();
        index.clear();
        live = 0;
    }

    /// Drops tombstones once they dominate, remapping every live iterator so
    /// it still points at the same logical position.
    void compact_if_worthwhile() {
        const size_t dead = entries.size() - live;
        if (dead < 32 || dead < live) return;

        std::vector<size_t> new_position(entries.size() + 1, 0);
        size_t next = 0;
        for (size_t i = 0; i < entries.size(); ++i) {
            new_position[i] = next;
            if (!entries[i].deleted) ++next;
        }
        new_position[entries.size()] = next;

        std::vector<std::weak_ptr<size_t>> still_alive;
        for (auto& weak : cursors) {
            if (auto cursor = weak.lock()) {
                *cursor = new_position[std::min(*cursor, entries.size())];
                still_alive.push_back(weak);
            }
        }
        cursors = std::move(still_alive);

        std::vector<Entry> packed;
        packed.reserve(live);
        index.clear();
        for (auto& entry : entries) {
            if (entry.deleted) continue;
            index.emplace(hash_key(entry.key), packed.size());
            packed.push_back(std::move(entry));
        }
        entries = std::move(packed);
    }

    std::shared_ptr<size_t> new_cursor() {
        if (cursors.size() >= 64) {
            // Forget iterators that have gone away.
            std::vector<std::weak_ptr<size_t>> still_alive;
            for (auto& weak : cursors) {
                if (!weak.expired()) still_alive.push_back(weak);
            }
            cursors = std::move(still_alive);
        }
        auto cursor = std::make_shared<size_t>(0);
        cursors.push_back(cursor);
        return cursor;
    }
};

std::shared_ptr<CollectionData> data_of(const JsValue& value, ObjectClass klass, const char* name) {
    if (!value.object_val || value.object_val->klass != klass || !value.object_val->internal) {
        js_throw_type_error(std::string("Method ") + name + " called on incompatible receiver " +
                            js_describe_thrown(value));
    }
    return std::static_pointer_cast<CollectionData>(value.object_val->internal);
}

JsValue make_collection(ObjectClass klass, JsObject* proto) {
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = proto;
    cell->klass = klass;
    auto data = std::make_shared<CollectionData>();
    cell->internal = data;
    // The entries live outside the property table; tell the collector about them.
    std::weak_ptr<CollectionData> weak = data;
    cell->trace_extra = [weak](Heap& heap) {
        if (auto d = weak.lock()) {
            // After `class X extends Map` two cells share these entries.
            if (!heap.first_visit(d.get())) return;
            for (const auto& entry : d->entries) {
                heap.mark_value(entry.key);
                heap.mark_value(entry.value);
            }
        }
    };
    return JsValue::make_object(cell);
}

void require_object_key(const JsValue& key, const char* what) {
    if (!key.is_object_like()) js_throw_type_error(std::string("Invalid value used ") + what);
}

enum class IterKind { Keys, Values, Entries };

JsValue make_collection_iterator(const JsValue& collection, const std::shared_ptr<CollectionData>& data,
                                 IterKind kind) {
    auto cursor = data->new_cursor();
    return js_make_iterator([data, cursor, kind](JsValue& value) {
        while (*cursor < data->entries.size()) {
            const auto& entry = data->entries[(*cursor)++];
            if (entry.deleted) continue;
            switch (kind) {
                case IterKind::Keys: value = entry.key; break;
                case IterKind::Values: value = entry.value; break;
                case IterKind::Entries: value = JsValue::make_array({entry.key, entry.value}); break;
            }
            return true;
        }
        return false;
    }, {collection});
}

} // namespace

void install_collection_builtins(JsIntrinsics& realm) {
    // ---- Map --------------------------------------------------------------
    JsObject* mp = realm.map_proto;
    JsValue map_ctor = js_make_native("Map", 0, [](Args args) {
        JsValue map = make_collection(ObjectClass::Map, JsIntrinsics::get().map_proto);
        const JsValue& source = js_arg(args, 0);
        if (!source.is_nullish()) {
            auto data = data_of(map, ObjectClass::Map, "Map");
            js_iterate(source, [&](const JsValue& entry) {
                if (!entry.is_object_like()) js_throw_type_error("Iterator value is not an entry object");
                data->set(entry.get_property("0"), entry.get_property("1"));
                return true;
            });
        }
        return map;
    });
    map_ctor.define_property("prototype", js_object_value(mp), 0);
    js_define_hidden(mp, "constructor", map_ctor);
    js_define_hidden(mp, "@@toStringTag", JsValue::make_string("Map"));

    js_define_method(mp, "get", 1, [](Args args) {
        auto* entry = data_of(self(), ObjectClass::Map, "Map.prototype.get")->find(js_arg(args, 0));
        return entry ? entry->value : JsValue::make_undefined();
    });
    js_define_method(mp, "set", 2, [](Args args) {
        JsValue map = self();
        data_of(map, ObjectClass::Map, "Map.prototype.set")->set(js_arg(args, 0), js_arg(args, 1));
        return map;
    });
    js_define_method(mp, "has", 1, [](Args args) {
        return JsValue::make_boolean(data_of(self(), ObjectClass::Map, "Map.prototype.has")->find(js_arg(args, 0)) != nullptr);
    });
    js_define_method(mp, "delete", 1, [](Args args) {
        return JsValue::make_boolean(data_of(self(), ObjectClass::Map, "Map.prototype.delete")->remove(js_arg(args, 0)));
    });
    js_define_method(mp, "clear", 0, [](Args) {
        data_of(self(), ObjectClass::Map, "Map.prototype.clear")->clear();
        return JsValue::make_undefined();
    });
    js_define_getter(mp, "size", [](Args) {
        return JsValue::make_number(static_cast<double>(data_of(self(), ObjectClass::Map, "get Map.prototype.size")->live));
    });
    js_define_method(mp, "forEach", 1, [](Args args) {
        JsValue map = self();
        auto data = data_of(map, ObjectClass::Map, "Map.prototype.forEach");
        JsValue callback = js_arg(args, 0);
        if (!callback.is_function()) js_throw_type_error("Map.prototype.forEach: callback is not a function");
        const JsValue this_arg = js_arg(args, 1);
        auto cursor = data->new_cursor();
        while (*cursor < data->entries.size()) {
            // Copy: the callback may delete or add entries.
            auto entry = data->entries[(*cursor)++];
            if (entry.deleted) continue;
            callback.call_with_this(this_arg, {entry.value, entry.key, map});
        }
        return JsValue::make_undefined();
    });
    auto map_iter = [](IterKind kind, const char* name) {
        return [kind, name](Args) {
            JsValue map = self();
            return make_collection_iterator(map, data_of(map, ObjectClass::Map, name), kind);
        };
    };
    js_define_method(mp, "keys", 0, map_iter(IterKind::Keys, "Map.prototype.keys"));
    js_define_method(mp, "values", 0, map_iter(IterKind::Values, "Map.prototype.values"));
    js_define_method(mp, "entries", 0, map_iter(IterKind::Entries, "Map.prototype.entries"));
    js_define_method(mp, kIteratorKey, 0, map_iter(IterKind::Entries, "Map.prototype[Symbol.iterator]"));
    js_define_method(mp, "Symbol.iterator", 0, map_iter(IterKind::Entries, "Map.prototype[Symbol.iterator]"));
    js_define_method(map_ctor, "groupBy", 2, [](Args args) {
        JsValue map = make_collection(ObjectClass::Map, JsIntrinsics::get().map_proto);
        auto data = data_of(map, ObjectClass::Map, "Map.groupBy");
        const JsValue callback = js_arg(args, 1);
        double index = 0;
        js_iterate(js_arg(args, 0), [&](const JsValue& item) {
            JsValue key = js_call(callback, JsValue(), {item, JsValue::make_number(index++)});
            auto* entry = data->find(key);
            if (!entry) {
                data->set(key, JsValue::make_array());
                entry = data->find(key);
            }
            entry->value.object_val->array_elements.push_back(item);
            return true;
        });
        return map;
    });
    realm.add_global("Map", map_ctor);

    // ---- Set --------------------------------------------------------------
    JsObject* sp = realm.set_proto;
    JsValue set_ctor = js_make_native("Set", 0, [](Args args) {
        JsValue set = make_collection(ObjectClass::Set, JsIntrinsics::get().set_proto);
        const JsValue& source = js_arg(args, 0);
        if (!source.is_nullish()) {
            auto data = data_of(set, ObjectClass::Set, "Set");
            js_iterate(source, [&](const JsValue& item) {
                data->set(item, item);
                return true;
            });
        }
        return set;
    });
    set_ctor.define_property("prototype", js_object_value(sp), 0);
    js_define_hidden(sp, "constructor", set_ctor);
    js_define_hidden(sp, "@@toStringTag", JsValue::make_string("Set"));

    js_define_method(sp, "add", 1, [](Args args) {
        JsValue set = self();
        data_of(set, ObjectClass::Set, "Set.prototype.add")->set(js_arg(args, 0), js_arg(args, 0));
        return set;
    });
    js_define_method(sp, "has", 1, [](Args args) {
        return JsValue::make_boolean(data_of(self(), ObjectClass::Set, "Set.prototype.has")->find(js_arg(args, 0)) != nullptr);
    });
    js_define_method(sp, "delete", 1, [](Args args) {
        return JsValue::make_boolean(data_of(self(), ObjectClass::Set, "Set.prototype.delete")->remove(js_arg(args, 0)));
    });
    js_define_method(sp, "clear", 0, [](Args) {
        data_of(self(), ObjectClass::Set, "Set.prototype.clear")->clear();
        return JsValue::make_undefined();
    });
    js_define_getter(sp, "size", [](Args) {
        return JsValue::make_number(static_cast<double>(data_of(self(), ObjectClass::Set, "get Set.prototype.size")->live));
    });
    js_define_method(sp, "forEach", 1, [](Args args) {
        JsValue set = self();
        auto data = data_of(set, ObjectClass::Set, "Set.prototype.forEach");
        JsValue callback = js_arg(args, 0);
        if (!callback.is_function()) js_throw_type_error("Set.prototype.forEach: callback is not a function");
        const JsValue this_arg = js_arg(args, 1);
        auto cursor = data->new_cursor();
        while (*cursor < data->entries.size()) {
            auto entry = data->entries[(*cursor)++];
            if (entry.deleted) continue;
            callback.call_with_this(this_arg, {entry.key, entry.key, set});
        }
        return JsValue::make_undefined();
    });
    auto set_iter = [](IterKind kind, const char* name) {
        return [kind, name](Args) {
            JsValue set = self();
            return make_collection_iterator(set, data_of(set, ObjectClass::Set, name), kind);
        };
    };
    js_define_method(sp, "values", 0, set_iter(IterKind::Keys, "Set.prototype.values"));
    js_define_method(sp, "keys", 0, set_iter(IterKind::Keys, "Set.prototype.keys"));
    js_define_method(sp, "entries", 0, set_iter(IterKind::Entries, "Set.prototype.entries"));
    js_define_method(sp, kIteratorKey, 0, set_iter(IterKind::Keys, "Set.prototype[Symbol.iterator]"));
    js_define_method(sp, "Symbol.iterator", 0, set_iter(IterKind::Keys, "Set.prototype[Symbol.iterator]"));
    realm.add_global("Set", set_ctor);

    // ---- WeakMap and WeakSet ----------------------------------------------
    //
    // Keyed by object identity. Entries are held strongly: nothing is reclaimed
    // early, which is safe, just not memory-optimal.
    JsObject* wmp = realm.weakmap_proto;
    JsValue weakmap_ctor = js_make_native("WeakMap", 0, [](Args args) {
        JsValue map = make_collection(ObjectClass::WeakMap, JsIntrinsics::get().weakmap_proto);
        const JsValue& source = js_arg(args, 0);
        if (!source.is_nullish()) {
            auto data = data_of(map, ObjectClass::WeakMap, "WeakMap");
            js_iterate(source, [&](const JsValue& entry) {
                if (!entry.is_object_like()) js_throw_type_error("Iterator value is not an entry object");
                JsValue key = entry.get_property("0");
                require_object_key(key, "as weak map key");
                data->set(key, entry.get_property("1"));
                return true;
            });
        }
        return map;
    });
    weakmap_ctor.define_property("prototype", js_object_value(wmp), 0);
    js_define_hidden(wmp, "constructor", weakmap_ctor);
    js_define_hidden(wmp, "@@toStringTag", JsValue::make_string("WeakMap"));
    js_define_method(wmp, "get", 1, [](Args args) {
        auto* entry = data_of(self(), ObjectClass::WeakMap, "WeakMap.prototype.get")->find(js_arg(args, 0));
        return entry ? entry->value : JsValue::make_undefined();
    });
    js_define_method(wmp, "set", 2, [](Args args) {
        JsValue map = self();
        require_object_key(js_arg(args, 0), "as weak map key");
        data_of(map, ObjectClass::WeakMap, "WeakMap.prototype.set")->set(js_arg(args, 0), js_arg(args, 1));
        return map;
    });
    js_define_method(wmp, "has", 1, [](Args args) {
        return JsValue::make_boolean(data_of(self(), ObjectClass::WeakMap, "WeakMap.prototype.has")->find(js_arg(args, 0)) != nullptr);
    });
    js_define_method(wmp, "delete", 1, [](Args args) {
        return JsValue::make_boolean(data_of(self(), ObjectClass::WeakMap, "WeakMap.prototype.delete")->remove(js_arg(args, 0)));
    });
    realm.add_global("WeakMap", weakmap_ctor);

    JsObject* wsp = realm.weakset_proto;
    JsValue weakset_ctor = js_make_native("WeakSet", 0, [](Args args) {
        JsValue set = make_collection(ObjectClass::WeakSet, JsIntrinsics::get().weakset_proto);
        const JsValue& source = js_arg(args, 0);
        if (!source.is_nullish()) {
            auto data = data_of(set, ObjectClass::WeakSet, "WeakSet");
            js_iterate(source, [&](const JsValue& item) {
                require_object_key(item, "in weak set");
                data->set(item, item);
                return true;
            });
        }
        return set;
    });
    weakset_ctor.define_property("prototype", js_object_value(wsp), 0);
    js_define_hidden(wsp, "constructor", weakset_ctor);
    js_define_hidden(wsp, "@@toStringTag", JsValue::make_string("WeakSet"));
    js_define_method(wsp, "add", 1, [](Args args) {
        JsValue set = self();
        require_object_key(js_arg(args, 0), "in weak set");
        data_of(set, ObjectClass::WeakSet, "WeakSet.prototype.add")->set(js_arg(args, 0), js_arg(args, 0));
        return set;
    });
    js_define_method(wsp, "has", 1, [](Args args) {
        return JsValue::make_boolean(data_of(self(), ObjectClass::WeakSet, "WeakSet.prototype.has")->find(js_arg(args, 0)) != nullptr);
    });
    js_define_method(wsp, "delete", 1, [](Args args) {
        return JsValue::make_boolean(data_of(self(), ObjectClass::WeakSet, "WeakSet.prototype.delete")->remove(js_arg(args, 0)));
    });
    realm.add_global("WeakSet", weakset_ctor);
}

} // namespace litebird
