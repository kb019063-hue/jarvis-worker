// ES2025 Iterator constructor, Iterator.from, %IteratorPrototype% helpers,
// %WrapForValidIteratorPrototype%, and %IteratorHelperPrototype% (ECMA-262 §27.1).

#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Heap.hpp"
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

JsValue self() { return JsValue::current_this(); }

// ---------------------------------------------------------------------------
// Per-cell internal slot registration
// ---------------------------------------------------------------------------
// Wrapper and helper instances are ObjectClass::Plain so they never collide with
// built-in IteratorState (ObjectClass::Iterator in BuiltinsArray.cpp) or other
// Plain objects that carry `internal` payloads (such as JSON.rawJSON). Registering
// the payload pointer in a typed table lets every method validate the exact
// internal slot without altering HeapObject layout.

struct IteratorSlotRegistry {
    enum class Kind : uint8_t { Wrap, Helper };
    std::mutex mutex;
    std::unordered_map<const void*, Kind> map;
};

IteratorSlotRegistry& slot_registry() {
    // Intentionally leaked like Heap::default_heap() so destructors running at
    // process teardown never touch an already-destroyed registry.
    static auto* registry = new IteratorSlotRegistry();
    return *registry;
}

struct RegisteredSlot {
    explicit RegisteredSlot(IteratorSlotRegistry::Kind kind) {
        auto& reg = slot_registry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        reg.map.emplace(this, kind);
    }
    virtual ~RegisteredSlot() {
        auto& reg = slot_registry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        reg.map.erase(this);
    }
    RegisteredSlot(const RegisteredSlot&) = delete;
    RegisteredSlot& operator=(const RegisteredSlot&) = delete;
};

struct IteratorRecord {
    JsValue iterator;
    JsValue next_method;
};

struct WrapIteratorData : RegisteredSlot {
    IteratorRecord iterated;
    explicit WrapIteratorData(IteratorRecord rec)
        : RegisteredSlot(IteratorSlotRegistry::Kind::Wrap), iterated(std::move(rec)) {}
};

struct IteratorHelperData : RegisteredSlot {
    enum class State : uint8_t { SuspendedStart, SuspendedYield, Executing, Completed };
    enum class Kind : uint8_t { Map, Filter, Take, Drop, FlatMap };

    State state{State::SuspendedStart};
    Kind kind{Kind::Map};
    IteratorRecord outer;
    IteratorRecord inner;
    bool inner_active{false};
    JsValue callback;
    double counter{0.0};
    double remaining{0.0};

    explicit IteratorHelperData(Kind k)
        : RegisteredSlot(IteratorSlotRegistry::Kind::Helper), kind(k) {}

    void clear() {
        outer = {};
        inner = {};
        inner_active = false;
        callback = JsValue::make_undefined();
    }
};

struct IteratorRealmData {
    JsValue iterator_ctor;
    JsValue wrap_proto;
    JsValue helper_proto;
};

std::shared_ptr<IteratorRealmData> realm_iterator_data() {
    JsObject* ip = JsIntrinsics::get().iterator_proto;
    if (!ip || !ip->internal) return nullptr;
    return std::static_pointer_cast<IteratorRealmData>(ip->internal);
}

template <typename SlotType, IteratorSlotRegistry::Kind ExpectedKind>
std::shared_ptr<SlotType> require_slot(const JsValue& value, const char* method_name) {
    if (value.type != JsType::Object || !value.object_val ||
        value.object_val->klass != ObjectClass::Plain || !value.object_val->internal) {
        js_throw_type_error(std::string(method_name) + " called on incompatible receiver");
    }
    const void* ptr = value.object_val->internal.get();
    {
        auto& reg = slot_registry();
        std::lock_guard<std::mutex> lock(reg.mutex);
        auto it = reg.map.find(ptr);
        if (it == reg.map.end() || it->second != ExpectedKind) {
            js_throw_type_error(std::string(method_name) + " called on incompatible receiver");
        }
    }
    return std::static_pointer_cast<SlotType>(value.object_val->internal);
}

// ---------------------------------------------------------------------------
// ECMA-262 §7.4 abstract operations
// ---------------------------------------------------------------------------

JsValue create_iter_result(const JsValue& value, bool done) {
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = JsIntrinsics::get().object_proto;
    auto& v = cell->properties.entry("value");
    v.second = value;
    v.flags = PropertyMap::Default;
    auto& d = cell->properties.entry("done");
    d.second = JsValue::make_boolean(done);
    d.flags = PropertyMap::Default;
    return JsValue::make_object(cell);
}

void require_object(const JsValue& value, const char* method_name) {
    if (!value.is_object_like()) {
        js_throw_type_error(std::string(method_name) + " called on non-object");
    }
}

IteratorRecord get_iterator_direct(const JsValue& obj) {
    // §7.4.3 GetIteratorDirect: reads `next` once; callability is checked on step.
    return IteratorRecord{obj, obj.get_property("next")};
}

bool iterator_step_value(const IteratorRecord& rec, JsValue& out_value) {
    // §7.4.7 IteratorStepValue: failures here do NOT close `rec.iterator`.
    if (!rec.next_method.is_function()) {
        js_throw_type_error("Iterator 'next' is not a function");
    }
    JsValue result = rec.next_method.call_with_this(rec.iterator, {});
    if (!result.is_object_like()) {
        js_throw_type_error("Iterator result is not an object");
    }
    if (result.get_property("done").is_truthy()) {
        return false;
    }
    out_value = result.get_property("value");
    return true;
}

void iterator_close_normal(const JsValue& iterator) {
    if (!iterator.is_object_like()) return;
    JsValue ret = iterator.get_property("return");
    if (ret.is_nullish()) return;
    if (!ret.is_function()) {
        js_throw_type_error("Iterator 'return' is not a function");
    }
    JsValue inner = ret.call_with_this(iterator, {});
    if (!inner.is_object_like()) {
        js_throw_type_error("Iterator result is not an object");
    }
}

void iterator_close_abrupt(const JsValue& iterator) {
    // §7.4.11 step 5: when returning an existing throw completion, any error
    // from GetMethod("return") / Call(return) / non-object result is ignored
    // and the original abrupt throw completion wins.
    if (!iterator.is_object_like()) return;
    try {
        JsValue ret = iterator.get_property("return");
        if (ret.is_nullish() || !ret.is_function()) return;
        ret.call_with_this(iterator, {});
    } catch (const std::exception& ex) {
        if (js_is_internal_unwind(ex)) throw;
    }
}

IteratorRecord get_iterator_flattenable(const JsValue& obj, bool reject_primitives) {
    // §7.4.4 GetIteratorFlattenable ( obj, primitiveHandling )
    if (!obj.is_object_like()) {
        if (reject_primitives || obj.type != JsType::String) {
            js_throw_type_error(js_describe_thrown(obj) + " is not an object");
        }
    }
    JsValue method;
    bool has_method = js_iterator_method(obj, method);
    JsValue iterator;
    if (!has_method) {
        if (!obj.is_object_like()) {
            js_throw_type_error(js_describe_thrown(obj) + " is not iterable");
        }
        iterator = obj;
    } else {
        iterator = method.call_with_this(obj, {});
    }
    if (!iterator.is_object_like()) {
        js_throw_type_error("Iterator is not an object");
    }
    return get_iterator_direct(iterator);
}

double validate_limit_or_close(const JsValue& underlying, const JsValue& limit_val) {
    double num_limit = 0.0;
    try {
        num_limit = js_to_number(limit_val);
    } catch (const std::exception& ex) {
        if (js_is_internal_unwind(ex)) throw;
        iterator_close_abrupt(underlying);
        throw;
    }
    if (std::isnan(num_limit)) {
        iterator_close_abrupt(underlying);
        js_throw_range_error("Iterator limit must not be NaN");
    }
    double int_limit = js_to_integer(num_limit);
    if (int_limit == 0.0) int_limit = 0.0;
    if (int_limit < 0.0) {
        iterator_close_abrupt(underlying);
        js_throw_range_error("Iterator limit must not be negative");
    }
    return int_limit;
}

JsValue validate_callable_or_close(const JsValue& underlying, const JsValue& fn, const char* msg) {
    if (!fn.is_function()) {
        iterator_close_abrupt(underlying);
        js_throw_type_error(msg);
    }
    return fn;
}

bool ordinary_has_iterator_instance(const JsValue& value, JsObject* iterator_proto) {
    if (!value.is_object_like() || !iterator_proto) return false;
    JsObject* cell = value.is_function() ? value.function_cell() : value.object_val.get();
    if (!cell) return false;
    constexpr int kMaxChain = 1024;
    int depth = 0;
    for (JsObject* p = cell->prototype; p && depth < kMaxChain; p = p->prototype, ++depth) {
        if (p == iterator_proto) return true;
    }
    return false;
}

// §7.3.35 SetterThatIgnoresPrototypeProperties ( this, home, p, v )
JsValue setter_ignoring_prototype_properties(const JsValue& receiver, JsObject* home,
                                             const std::string& key, const std::string& label,
                                             const JsValue& value) {
    if (!receiver.is_object_like()) {
        js_throw_type_error("Cannot set property '" + label + "' on non-object");
    }
    if (receiver.object_val && receiver.object_val == home) {
        js_throw_type_error("Cannot assign to read only property '" + label + "' of Iterator.prototype");
    }
    if (JsObject* cell = receiver.property_cell(true)) {
        if (cell->exotic) {
            if (!receiver.has_own_property(key)) {
                JsValue copy = receiver;
                copy.define_property(key, value, PropertyMap::Default);
                return JsValue::make_undefined();
            }
            JsValue copy = receiver;
            if (!copy.set_property(key, value)) {
                js_throw_type_error("Cannot assign to property '" + label + "'");
            }
            return JsValue::make_undefined();
        }
        auto it = cell->properties.find(key);
        if (it == cell->properties.end()) {
            if (!cell->extensible || cell->frozen) {
                js_throw_type_error("Cannot add property " + label + ", object is not extensible");
            }
            auto& entry = cell->properties.entry(key);
            entry.second = value;
            entry.flags = PropertyMap::Default;
            return JsValue::make_undefined();
        }
        if (it->flags & PropertyMap::Accessor) {
            if (!it->setter || !it->setter->is_function()) {
                js_throw_type_error("Cannot set property " + label + " which has only a getter");
            }
            it->setter->call_with_this(receiver, {value});
            return JsValue::make_undefined();
        }
        if (cell->frozen || !(it->flags & PropertyMap::Writable)) {
            js_throw_type_error("Cannot assign to read only property '" + label + "'");
        }
        it->second = value;
        return JsValue::make_undefined();
    }
    JsValue copy = receiver;
    if (!copy.set_property(key, value)) {
        js_throw_type_error("Cannot assign to property '" + label + "'");
    }
    return JsValue::make_undefined();
}

JsValue make_helper_object(std::shared_ptr<IteratorHelperData> data) {
    auto realm_data = realm_iterator_data();
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = (realm_data && realm_data->helper_proto.is_object())
                          ? realm_data->helper_proto.object_val.get()
                          : JsIntrinsics::get().iterator_proto;
    cell->klass = ObjectClass::Plain;
    cell->internal = data;
    std::weak_ptr<IteratorHelperData> weak = data;
    cell->trace_extra = [weak](Heap& heap) {
        if (auto d = weak.lock()) {
            if (!heap.first_visit(d.get())) return;
            heap.mark_value(d->outer.iterator);
            heap.mark_value(d->outer.next_method);
            heap.mark_value(d->inner.iterator);
            heap.mark_value(d->inner.next_method);
            heap.mark_value(d->callback);
        }
    };
    return JsValue::make_object(cell);
}

void helper_close_all_normal(IteratorHelperData& data) {
    if (data.kind == IteratorHelperData::Kind::FlatMap && data.inner_active) {
        data.inner_active = false;
        JsValue inner_iter = std::move(data.inner.iterator);
        JsValue outer_iter = std::move(data.outer.iterator);
        data.clear();
        try {
            iterator_close_normal(inner_iter);
        } catch (const std::exception& ex) {
            if (js_is_internal_unwind(ex)) throw;
            iterator_close_abrupt(outer_iter);
            throw;
        }
        iterator_close_normal(outer_iter);
        return;
    }
    JsValue outer_iter = std::move(data.outer.iterator);
    data.clear();
    iterator_close_normal(outer_iter);
}

JsValue run_helper_next(IteratorHelperData& data) {
    using Kind = IteratorHelperData::Kind;
    using State = IteratorHelperData::State;

    auto finish = [&]() -> JsValue {
        data.clear();
        data.state = State::Completed;
        return create_iter_result(JsValue::make_undefined(), true);
    };

    switch (data.kind) {
        case Kind::Map: {
            JsValue value;
            if (!iterator_step_value(data.outer, value)) {
                return finish();
            }
            JsValue mapped;
            try {
                mapped = data.callback.call_with_this(
                    JsValue::make_undefined(), {value, JsValue::make_number(data.counter++)});
            } catch (const std::exception& ex) {
                if (js_is_internal_unwind(ex)) throw;
                iterator_close_abrupt(data.outer.iterator);
                throw;
            }
            data.state = State::SuspendedYield;
            return create_iter_result(mapped, false);
        }

        case Kind::Filter: {
            while (true) {
                JsValue value;
                if (!iterator_step_value(data.outer, value)) {
                    return finish();
                }
                bool selected = false;
                try {
                    selected = data.callback.call_with_this(
                                       JsValue::make_undefined(),
                                       {value, JsValue::make_number(data.counter++)})
                                   .is_truthy();
                } catch (const std::exception& ex) {
                    if (js_is_internal_unwind(ex)) throw;
                    iterator_close_abrupt(data.outer.iterator);
                    throw;
                }
                if (selected) {
                    data.state = State::SuspendedYield;
                    return create_iter_result(value, false);
                }
            }
        }

        case Kind::Take: {
            if (data.remaining == 0.0) {
                JsValue outer_iter = std::move(data.outer.iterator);
                data.clear();
                data.state = State::Completed;
                iterator_close_normal(outer_iter);
                return create_iter_result(JsValue::make_undefined(), true);
            }
            if (std::isfinite(data.remaining)) {
                data.remaining -= 1.0;
            }
            JsValue value;
            if (!iterator_step_value(data.outer, value)) {
                return finish();
            }
            data.state = State::SuspendedYield;
            return create_iter_result(value, false);
        }

        case Kind::Drop: {
            while (data.remaining > 0.0) {
                if (std::isfinite(data.remaining)) {
                    data.remaining -= 1.0;
                }
                JsValue discarded;
                if (!iterator_step_value(data.outer, discarded)) {
                    return finish();
                }
            }
            JsValue value;
            if (!iterator_step_value(data.outer, value)) {
                return finish();
            }
            data.state = State::SuspendedYield;
            return create_iter_result(value, false);
        }

        case Kind::FlatMap: {
            while (true) {
                if (!data.inner_active) {
                    JsValue value;
                    if (!iterator_step_value(data.outer, value)) {
                        return finish();
                    }
                    try {
                        JsValue mapped = data.callback.call_with_this(
                            JsValue::make_undefined(), {value, JsValue::make_number(data.counter++)});
                        data.inner = get_iterator_flattenable(mapped, /*reject_primitives=*/true);
                        data.inner_active = true;
                    } catch (const std::exception& ex) {
                        if (js_is_internal_unwind(ex)) throw;
                        iterator_close_abrupt(data.outer.iterator);
                        throw;
                    }
                }
                JsValue inner_value;
                bool stepped = false;
                try {
                    stepped = iterator_step_value(data.inner, inner_value);
                } catch (const std::exception& ex) {
                    if (js_is_internal_unwind(ex)) throw;
                    // Abrupt completion from stepping inner closes only the
                    // outer iterator, never the inner iterator itself.
                    data.inner_active = false;
                    data.inner = {};
                    iterator_close_abrupt(data.outer.iterator);
                    throw;
                }
                if (!stepped) {
                    data.inner_active = false;
                    data.inner = {};
                    continue;
                }
                data.state = State::SuspendedYield;
                return create_iter_result(inner_value, false);
            }
        }
    }
    return finish();
}

} // namespace

void install_iterator_builtins(JsIntrinsics& realm) {
    JsObject* ip = realm.iterator_proto;

    // %IteratorPrototype%[@@iterator] ( ) (§27.1.2.1): name must be "[Symbol.iterator]".
    // Keep the legacy "Symbol.iterator" string alias pointing to the same function
    // so both key lookups share one identity.
    JsValue self_iter_fn = js_make_method("[Symbol.iterator]", 0, [](Args) { return self(); });
    js_define_hidden(ip, kIteratorKey, self_iter_fn);
    js_define_hidden(ip, "Symbol.iterator", self_iter_fn);

    // ---- Iterator constructor (§27.1.1.1) ---------------------------------
    JsValue iterator_ctor = js_make_native("Iterator", 0, [](Args) {
        if (JsValue::current_requires_new() || !JsValue::current_new_target()) {
            js_throw_type_error("Abstract class Iterator cannot be directly constructed");
        }
        if (const JsValue* derived = JsValue::current_derived_this()) {
            if (derived->is_object()) return *derived;
        }
        JsObject* cell = Heap::default_heap().allocate_object();
        cell->prototype = JsIntrinsics::get().iterator_proto;
        return JsValue::make_object(cell);
    });
    iterator_ctor.define_property("prototype", js_object_value(ip), 0);

    // ---- %WrapForValidIteratorPrototype% (§27.1.2.1.1) --------------------
    JsObject* wrap_proto = Heap::default_heap().allocate_object();
    wrap_proto->prototype = ip;
    js_define_method(wrap_proto, "next", 0, [](Args) {
        auto data = require_slot<WrapIteratorData, IteratorSlotRegistry::Kind::Wrap>(
            self(), "%WrapForValidIteratorPrototype%.next");
        return js_call(data->iterated.next_method, data->iterated.iterator, {});
    });
    js_define_method(wrap_proto, "return", 0, [](Args) {
        auto data = require_slot<WrapIteratorData, IteratorSlotRegistry::Kind::Wrap>(
            self(), "%WrapForValidIteratorPrototype%.return");
        JsValue ret = data->iterated.iterator.get_property("return");
        if (ret.is_nullish()) {
            return create_iter_result(JsValue::make_undefined(), true);
        }
        if (!ret.is_function()) {
            js_throw_type_error("Iterator 'return' is not a function");
        }
        return ret.call_with_this(data->iterated.iterator, {});
    });

    // ---- %IteratorHelperPrototype% (§27.1.3.1) ----------------------------
    JsObject* helper_proto = Heap::default_heap().allocate_object();
    helper_proto->prototype = ip;
    {
        auto& tag_entry = helper_proto->properties.entry("@@toStringTag");
        tag_entry.second = JsValue::make_string("Iterator Helper");
        tag_entry.flags = PropertyMap::Configurable;
    }
    js_define_method(helper_proto, "next", 0, [](Args) {
        auto data = require_slot<IteratorHelperData, IteratorSlotRegistry::Kind::Helper>(
            self(), "%IteratorHelperPrototype%.next");
        if (data->state == IteratorHelperData::State::Executing) {
            js_throw_type_error("Iterator helper is already executing");
        }
        if (data->state == IteratorHelperData::State::Completed) {
            return create_iter_result(JsValue::make_undefined(), true);
        }
        data->state = IteratorHelperData::State::Executing;
        try {
            return run_helper_next(*data);
        } catch (...) {
            data->clear();
            data->state = IteratorHelperData::State::Completed;
            throw;
        }
    });
    js_define_method(helper_proto, "return", 0, [](Args) {
        auto data = require_slot<IteratorHelperData, IteratorSlotRegistry::Kind::Helper>(
            self(), "%IteratorHelperPrototype%.return");
        if (data->state == IteratorHelperData::State::Executing) {
            js_throw_type_error("Iterator helper is already executing");
        }
        if (data->state == IteratorHelperData::State::Completed) {
            return create_iter_result(JsValue::make_undefined(), true);
        }
        data->state = IteratorHelperData::State::Executing;
        try {
            helper_close_all_normal(*data);
        } catch (...) {
            data->clear();
            data->state = IteratorHelperData::State::Completed;
            throw;
        }
        data->state = IteratorHelperData::State::Completed;
        return create_iter_result(JsValue::make_undefined(), true);
    });

    // Store per-realm iterator intrinsics on ip (already traced by JsIntrinsics).
    auto realm_data = std::make_shared<IteratorRealmData>();
    realm_data->iterator_ctor = iterator_ctor;
    realm_data->wrap_proto = JsValue::make_object(wrap_proto);
    realm_data->helper_proto = JsValue::make_object(helper_proto);
    ip->internal = realm_data;
    std::weak_ptr<IteratorRealmData> weak_realm = realm_data;
    ip->trace_extra = [weak_realm](Heap& heap) {
        if (auto s = weak_realm.lock()) {
            if (!heap.first_visit(s.get())) return;
            heap.mark_value(s->iterator_ctor);
            heap.mark_value(s->wrap_proto);
            heap.mark_value(s->helper_proto);
        }
    };

    // ---- Iterator.from ( O ) (§27.1.2.1) ----------------------------------
    js_define_method(iterator_ctor, "from", 1, [](Args args) {
        const JsValue& obj = js_arg(args, 0);
        IteratorRecord rec = get_iterator_flattenable(obj, /*reject_primitives=*/false);
        JsObject* iter_proto = JsIntrinsics::get().iterator_proto;
        if (ordinary_has_iterator_instance(rec.iterator, iter_proto)) {
            return rec.iterator;
        }
        auto rd = realm_iterator_data();
        auto data = std::make_shared<WrapIteratorData>(std::move(rec));
        JsObject* cell = Heap::default_heap().allocate_object();
        cell->prototype = (rd && rd->wrap_proto.is_object())
                              ? rd->wrap_proto.object_val.get()
                              : iter_proto;
        cell->klass = ObjectClass::Plain;
        cell->internal = data;
        std::weak_ptr<WrapIteratorData> weak = data;
        cell->trace_extra = [weak](Heap& heap) {
            if (auto d = weak.lock()) {
                if (!heap.first_visit(d.get())) return;
                heap.mark_value(d->iterated.iterator);
                heap.mark_value(d->iterated.next_method);
            }
        };
        return JsValue::make_object(cell);
    });

    // ---- Iterator.prototype.constructor & [Symbol.toStringTag] ------------
    {
        JsValue ctor_get = js_make_method("get constructor", 0, [](Args) {
            auto rd = realm_iterator_data();
            return rd ? rd->iterator_ctor : JsValue::make_undefined();
        });
        JsValue ctor_set = js_make_method("set constructor", 1, [](Args args) {
            return setter_ignoring_prototype_properties(
                self(), JsIntrinsics::get().iterator_proto, "constructor", "constructor", js_arg(args, 0));
        });
        js_object_value(ip).define_accessor("constructor", ctor_get, ctor_set, PropertyMap::Configurable);
    }
    {
        JsValue tag_get = js_make_method("get [Symbol.toStringTag]", 0, [](Args) {
            return JsValue::make_string("Iterator");
        });
        JsValue tag_set = js_make_method("set [Symbol.toStringTag]", 1, [](Args args) {
            return setter_ignoring_prototype_properties(
                self(), JsIntrinsics::get().iterator_proto, "@@toStringTag", "Symbol.toStringTag", js_arg(args, 0));
        });
        js_object_value(ip).define_accessor("@@toStringTag", tag_get, tag_set, PropertyMap::Configurable);
    }

    // ---- Lazy Iterator.prototype helpers ----------------------------------
    js_define_method(ip, "map", 1, [](Args args) {
        JsValue obj = self();
        require_object(obj, "Iterator.prototype.map");
        JsValue mapper = validate_callable_or_close(obj, js_arg(args, 0), "Iterator.prototype.map: mapper is not a function");
        IteratorRecord iterated = get_iterator_direct(obj);
        auto data = std::make_shared<IteratorHelperData>(IteratorHelperData::Kind::Map);
        data->outer = std::move(iterated);
        data->callback = std::move(mapper);
        return make_helper_object(std::move(data));
    });

    js_define_method(ip, "filter", 1, [](Args args) {
        JsValue obj = self();
        require_object(obj, "Iterator.prototype.filter");
        JsValue predicate = validate_callable_or_close(obj, js_arg(args, 0), "Iterator.prototype.filter: predicate is not a function");
        IteratorRecord iterated = get_iterator_direct(obj);
        auto data = std::make_shared<IteratorHelperData>(IteratorHelperData::Kind::Filter);
        data->outer = std::move(iterated);
        data->callback = std::move(predicate);
        return make_helper_object(std::move(data));
    });

    js_define_method(ip, "take", 1, [](Args args) {
        JsValue obj = self();
        require_object(obj, "Iterator.prototype.take");
        double int_limit = validate_limit_or_close(obj, js_arg(args, 0));
        IteratorRecord iterated = get_iterator_direct(obj);
        auto data = std::make_shared<IteratorHelperData>(IteratorHelperData::Kind::Take);
        data->outer = std::move(iterated);
        data->remaining = int_limit;
        return make_helper_object(std::move(data));
    });

    js_define_method(ip, "drop", 1, [](Args args) {
        JsValue obj = self();
        require_object(obj, "Iterator.prototype.drop");
        double int_limit = validate_limit_or_close(obj, js_arg(args, 0));
        IteratorRecord iterated = get_iterator_direct(obj);
        auto data = std::make_shared<IteratorHelperData>(IteratorHelperData::Kind::Drop);
        data->outer = std::move(iterated);
        data->remaining = int_limit;
        return make_helper_object(std::move(data));
    });

    js_define_method(ip, "flatMap", 1, [](Args args) {
        JsValue obj = self();
        require_object(obj, "Iterator.prototype.flatMap");
        JsValue mapper = validate_callable_or_close(obj, js_arg(args, 0), "Iterator.prototype.flatMap: mapper is not a function");
        IteratorRecord iterated = get_iterator_direct(obj);
        auto data = std::make_shared<IteratorHelperData>(IteratorHelperData::Kind::FlatMap);
        data->outer = std::move(iterated);
        data->callback = std::move(mapper);
        return make_helper_object(std::move(data));
    });

    // ---- Eager Iterator.prototype consumers -------------------------------
    js_define_method(ip, "toArray", 0, [](Args) {
        JsValue obj = self();
        require_object(obj, "Iterator.prototype.toArray");
        IteratorRecord iterated = get_iterator_direct(obj);
        std::vector<JsValue> items;
        JsValue value;
        while (iterator_step_value(iterated, value)) {
            items.push_back(std::move(value));
        }
        return JsValue::make_array(std::move(items));
    });

    js_define_method(ip, "forEach", 1, [](Args args) {
        JsValue obj = self();
        require_object(obj, "Iterator.prototype.forEach");
        JsValue fn = validate_callable_or_close(obj, js_arg(args, 0), "Iterator.prototype.forEach: callback is not a function");
        IteratorRecord iterated = get_iterator_direct(obj);
        double counter = 0.0;
        JsValue value;
        while (iterator_step_value(iterated, value)) {
            try {
                fn.call_with_this(JsValue::make_undefined(), {value, JsValue::make_number(counter++)});
            } catch (const std::exception& ex) {
                if (js_is_internal_unwind(ex)) throw;
                iterator_close_abrupt(iterated.iterator);
                throw;
            }
        }
        return JsValue::make_undefined();
    });

    js_define_method(ip, "some", 1, [](Args args) {
        JsValue obj = self();
        require_object(obj, "Iterator.prototype.some");
        JsValue predicate = validate_callable_or_close(obj, js_arg(args, 0), "Iterator.prototype.some: predicate is not a function");
        IteratorRecord iterated = get_iterator_direct(obj);
        double counter = 0.0;
        JsValue value;
        while (iterator_step_value(iterated, value)) {
            bool result = false;
            try {
                result = predicate.call_with_this(JsValue::make_undefined(),
                                                  {value, JsValue::make_number(counter++)})
                             .is_truthy();
            } catch (const std::exception& ex) {
                if (js_is_internal_unwind(ex)) throw;
                iterator_close_abrupt(iterated.iterator);
                throw;
            }
            if (result) {
                iterator_close_normal(iterated.iterator);
                return JsValue::make_boolean(true);
            }
        }
        return JsValue::make_boolean(false);
    });

    js_define_method(ip, "every", 1, [](Args args) {
        JsValue obj = self();
        require_object(obj, "Iterator.prototype.every");
        JsValue predicate = validate_callable_or_close(obj, js_arg(args, 0), "Iterator.prototype.every: predicate is not a function");
        IteratorRecord iterated = get_iterator_direct(obj);
        double counter = 0.0;
        JsValue value;
        while (iterator_step_value(iterated, value)) {
            bool result = false;
            try {
                result = predicate.call_with_this(JsValue::make_undefined(),
                                                  {value, JsValue::make_number(counter++)})
                             .is_truthy();
            } catch (const std::exception& ex) {
                if (js_is_internal_unwind(ex)) throw;
                iterator_close_abrupt(iterated.iterator);
                throw;
            }
            if (!result) {
                iterator_close_normal(iterated.iterator);
                return JsValue::make_boolean(false);
            }
        }
        return JsValue::make_boolean(true);
    });

    js_define_method(ip, "find", 1, [](Args args) {
        JsValue obj = self();
        require_object(obj, "Iterator.prototype.find");
        JsValue predicate = validate_callable_or_close(obj, js_arg(args, 0), "Iterator.prototype.find: predicate is not a function");
        IteratorRecord iterated = get_iterator_direct(obj);
        double counter = 0.0;
        JsValue value;
        while (iterator_step_value(iterated, value)) {
            bool result = false;
            try {
                result = predicate.call_with_this(JsValue::make_undefined(),
                                                  {value, JsValue::make_number(counter++)})
                             .is_truthy();
            } catch (const std::exception& ex) {
                if (js_is_internal_unwind(ex)) throw;
                iterator_close_abrupt(iterated.iterator);
                throw;
            }
            if (result) {
                iterator_close_normal(iterated.iterator);
                return value;
            }
        }
        return JsValue::make_undefined();
    });

    js_define_method(ip, "reduce", 1, [](Args args) {
        JsValue obj = self();
        require_object(obj, "Iterator.prototype.reduce");
        JsValue reducer = validate_callable_or_close(obj, js_arg(args, 0), "Iterator.prototype.reduce: reducer is not a function");
        IteratorRecord iterated = get_iterator_direct(obj);
        JsValue accumulator;
        double counter = 0.0;
        if (args.size() < 2) {
            if (!iterator_step_value(iterated, accumulator)) {
                js_throw_type_error("Reduce of empty iterator with no initial value");
            }
            counter = 1.0;
        } else {
            accumulator = args[1];
        }
        JsValue value;
        while (iterator_step_value(iterated, value)) {
            try {
                accumulator = reducer.call_with_this(
                    JsValue::make_undefined(), {accumulator, value, JsValue::make_number(counter++)});
            } catch (const std::exception& ex) {
                if (js_is_internal_unwind(ex)) throw;
                iterator_close_abrupt(iterated.iterator);
                throw;
            }
        }
        return accumulator;
    });

    realm.add_global("Iterator", iterator_ctor);
}

} // namespace litebird
