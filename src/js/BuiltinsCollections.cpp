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
#include <limits>
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

    // A cursor is registered only while its iterator can still produce a
    // value. Its slot makes unregistering a finished iterator constant-time:
    // removing it must not turn a run of short, completed loops into a scan
    // of every cursor that came before it.
    struct Cursor {
        static constexpr size_t kUnregistered = static_cast<size_t>(-1);
        size_t position{0};
        size_t slot{kUnregistered};
    };

    std::vector<Entry> entries;
    std::unordered_map<std::string, size_t> index;      // hash key -> position in `entries`
    size_t live{0};
    std::vector<std::weak_ptr<Cursor>> cursors;         // positions held by iterators that may resume
    static constexpr size_t kCursorPruneMinimum = 64;
    size_t cursor_prune_at{kCursorPruneMinimum};

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

    void schedule_cursor_prune() {
        if (cursors.size() < kCursorPruneMinimum) {
            cursor_prune_at = kCursorPruneMinimum;
        } else if (cursors.size() > std::numeric_limits<size_t>::max() / 2) {
            cursor_prune_at = std::numeric_limits<size_t>::max();
        } else {
            cursor_prune_at = cursors.size() * 2;
        }
    }

    /// Drop abandoned cursors and repair each live cursor's vector slot.
    /// This is called only at geometrically growing sizes, so a collection
    /// whose abandoned iterators have not been collected yet costs O(n) work
    /// in total over n new cursors, not a growing scan on every new cursor.
    void prune_expired_cursors() {
        std::vector<std::weak_ptr<Cursor>> still_alive;
        still_alive.reserve(cursors.size());
        for (auto& weak : cursors) {
            if (auto cursor = weak.lock()) {
                cursor->slot = still_alive.size();
                still_alive.push_back(std::move(weak));
            }
        }
        cursors = std::move(still_alive);
        schedule_cursor_prune();
    }

    /// A completed iterator can never resume: js_make_iterator latches its
    /// first done result. Remove its cursor now instead of waiting for a GC
    /// cycle to turn its weak pointer into an expired one.
    void release_cursor(const std::shared_ptr<Cursor>& cursor) {
        if (cursor->slot == Cursor::kUnregistered) return;
        const size_t slot = cursor->slot;
        const size_t last = cursors.size() - 1;
        if (slot != last) {
            cursors[slot] = std::move(cursors[last]);
            if (auto moved = cursors[slot].lock()) moved->slot = slot;
        }
        cursors.pop_back();
        cursor->slot = Cursor::kUnregistered;
        schedule_cursor_prune();
    }

    void clear() {
        // Iterators positioned anywhere are now past the old entries. Retain
        // only cursors that can still be asked for a next result, and give
        // each one a slot in the compacted registry.
        std::vector<std::weak_ptr<Cursor>> still_alive;
        still_alive.reserve(cursors.size());
        for (auto& weak : cursors) {
            if (auto cursor = weak.lock()) {
                cursor->position = 0;
                cursor->slot = still_alive.size();
                still_alive.push_back(std::move(weak));
            }
        }
        cursors = std::move(still_alive);
        schedule_cursor_prune();
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

        std::vector<std::weak_ptr<Cursor>> still_alive;
        still_alive.reserve(cursors.size());
        for (auto& weak : cursors) {
            if (auto cursor = weak.lock()) {
                cursor->position = new_position[std::min(cursor->position, entries.size())];
                cursor->slot = still_alive.size();
                still_alive.push_back(std::move(weak));
            }
        }
        cursors = std::move(still_alive);
        schedule_cursor_prune();

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

    std::shared_ptr<Cursor> new_cursor() {
        if (cursors.size() >= cursor_prune_at) prune_expired_cursors();
        auto cursor = std::make_shared<Cursor>();
        cursor->slot = cursors.size();
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

JsValue make_collection(ObjectClass klass, JsObject* proto, const JsValue* derived = nullptr) {
    JsObject* cell = (derived && derived->object_val) ? derived->object_val.get()
                                                       : Heap::default_heap().allocate_object();
    // A derived constructor already received an object with new.target's
    // prototype. Keep it; a plain native construction starts at its intrinsic
    // prototype (or the one Reflect.construct supplied).
    if (!derived || !derived->object_val) cell->prototype = proto;
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
                                 IterKind kind, JsObject* proto) {
    auto cursor = data->new_cursor();
    return js_make_iterator([data, cursor, kind](JsValue& value) {
        while (cursor->position < data->entries.size()) {
            const auto& entry = data->entries[(cursor->position)++];
            if (entry.deleted) continue;
            switch (kind) {
                case IterKind::Keys: value = entry.key; break;
                case IterKind::Values: value = entry.value; break;
                case IterKind::Entries: value = JsValue::make_array({entry.key, entry.value}); break;
            }
            return true;
        }
        data->release_cursor(cursor);
        return false;
    }, {collection}, proto);
}

} // namespace

void install_collection_builtins(JsIntrinsics& realm) {
    // ---- Map --------------------------------------------------------------
    JsObject* mp = realm.map_proto;
    JsValue map_ctor = js_make_native("Map", 0, [](Args args) {
        // In a derived construction this is the object with new.target's
        // prototype. Under Reflect.construct it also performs that one
        // prototype lookup, so do it before any user code runs.
        const JsValue* derived_ptr = JsValue::current_is_construct() ? JsValue::current_derived_this() : nullptr;
        JsValue derived_holder;
        if (derived_ptr) derived_holder = *derived_ptr;
        JsValue map = make_collection(ObjectClass::Map, JsIntrinsics::get().map_proto,
                                      derived_ptr ? &derived_holder : nullptr);
        const JsValue& source = js_arg(args, 0);
        if (!source.is_nullish()) {
            // Get once, before asking the source for its iterator. It is a
            // method of the new instance, so a subclass override is observed.
            JsValue adder = map.get_property("set");
            if (!adder.is_function()) js_throw_type_error("Map constructor: set is not a function");
            js_iterate(source, [&](const JsValue& entry) {
                if (!entry.is_object_like()) js_throw_type_error("Iterator value is not an entry object");
                js_call(adder, map, {entry.get_property("0"), entry.get_property("1")});
                return true;
            });
        }
        return map;
    });
    map_ctor.define_property("prototype", js_object_value(mp), 0);
    js_define_hidden(mp, "constructor", map_ctor);
    js_define_hidden(mp, "@@toStringTag", JsValue::make_string("Map"));
    {
        // Map_ctor[Symbol.species] (§24.1.2.5, §24.2.2.5): the constructor itself.
        JsValue species_getter = js_make_native("get [Symbol.species]", 0, [](Args) { return JsValue::current_this(); });
        species_getter.function_cell()->not_constructor = true;
        map_ctor.define_accessor(kSymbolSpeciesKey, species_getter, JsValue::make_undefined(),
                           PropertyMap::Configurable);
    }


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
        try {
            while (cursor->position < data->entries.size()) {
                // Copy: the callback may delete or add entries.
                auto entry = data->entries[(cursor->position)++];
                if (entry.deleted) continue;
                callback.call_with_this(this_arg, {entry.value, entry.key, map});
            }
        } catch (...) {
            data->release_cursor(cursor);
            throw;
        }
        data->release_cursor(cursor);
        return JsValue::make_undefined();
    });
    auto map_iter = [](IterKind kind, const char* name) {
        return [kind, name](Args) {
            JsValue map = self();
            return make_collection_iterator(map, data_of(map, ObjectClass::Map, name), kind,
                                            JsIntrinsics::get().map_iterator_proto);
        };
    };
    js_define_method(mp, "keys", 0, map_iter(IterKind::Keys, "Map.prototype.keys"));
    js_define_method(mp, "values", 0, map_iter(IterKind::Values, "Map.prototype.values"));
    // `Map.prototype[Symbol.iterator]` is the same function object as
    // `Map.prototype.entries`, and its name is "entries": `Map.prototype.entries`
    // and the @@iterator method are one function in the language, not two that
    // iterate alike.
    JsValue map_entries = js_make_native("entries", 0, map_iter(IterKind::Entries, "Map.prototype.entries"));
    js_define_hidden(mp, "entries", map_entries);
    js_define_hidden(mp, kIteratorKey, map_entries);
    js_define_hidden(mp, "Symbol.iterator", map_entries);
    js_install_iterator_prototype(JsIntrinsics::get().map_iterator_proto, "Map Iterator");
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
        // As for Map, let current_derived_this() make (and retain) the
        // new.target-prototype object before a source or override can run.
        const JsValue* derived_ptr = JsValue::current_is_construct() ? JsValue::current_derived_this() : nullptr;
        JsValue derived_holder;
        if (derived_ptr) derived_holder = *derived_ptr;
        JsValue set = make_collection(ObjectClass::Set, JsIntrinsics::get().set_proto,
                                      derived_ptr ? &derived_holder : nullptr);
        const JsValue& source = js_arg(args, 0);
        if (!source.is_nullish()) {
            JsValue adder = set.get_property("add");
            if (!adder.is_function()) js_throw_type_error("Set constructor: add is not a function");
            js_iterate(source, [&](const JsValue& item) {
                js_call(adder, set, {item});
                return true;
            });
        }
        return set;
    });
    set_ctor.define_property("prototype", js_object_value(sp), 0);
    js_define_hidden(sp, "constructor", set_ctor);
    js_define_hidden(sp, "@@toStringTag", JsValue::make_string("Set"));
    {
        // Set_ctor[Symbol.species] (§24.1.2.5, §24.2.2.5): the constructor itself.
        JsValue species_getter = js_make_native("get [Symbol.species]", 0, [](Args) { return JsValue::current_this(); });
        species_getter.function_cell()->not_constructor = true;
        set_ctor.define_accessor(kSymbolSpeciesKey, species_getter, JsValue::make_undefined(),
                           PropertyMap::Configurable);
    }


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
        try {
            while (cursor->position < data->entries.size()) {
                auto entry = data->entries[(cursor->position)++];
                if (entry.deleted) continue;
                callback.call_with_this(this_arg, {entry.key, entry.key, set});
            }
        } catch (...) {
            data->release_cursor(cursor);
            throw;
        }
        data->release_cursor(cursor);
        return JsValue::make_undefined();
    });
    auto set_iter = [](IterKind kind, const char* name) {
        return [kind, name](Args) {
            JsValue set = self();
            return make_collection_iterator(set, data_of(set, ObjectClass::Set, name), kind,
                                            JsIntrinsics::get().set_iterator_proto);
        };
    };
    // `values`, `keys` and `Symbol.iterator` are one function object, named
    // "values"; only `entries` is another.
    JsValue set_values = js_make_native("values", 0, set_iter(IterKind::Keys, "Set.prototype.values"));
    js_define_hidden(sp, "values", set_values);
    js_define_hidden(sp, "keys", set_values);
    js_define_hidden(sp, kIteratorKey, set_values);
    js_define_hidden(sp, "Symbol.iterator", set_values);
    js_define_method(sp, "entries", 0, set_iter(IterKind::Entries, "Set.prototype.entries"));
    js_install_iterator_prototype(JsIntrinsics::get().set_iterator_proto, "Set Iterator");
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
