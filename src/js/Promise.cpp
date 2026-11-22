// Promises (ECMA-262 27.2).
//
// A promise's state lives in a PromiseData shared between the script-visible
// promise objects and the engine code that settles it. Reactions always run as
// microtasks, in the order they were registered.

#include "litebird/js/Promise.hpp"
#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/common/ReleaseQueue.hpp"
#include <algorithm>
#include <mutex>

namespace litebird {

// `p = p.then(f)` in a loop, on a promise that has not settled yet, links each
// promise to the next through its reaction (PromiseHandler::next_promise, or
// a native reaction that captured the promise it will settle). Releasing the
// first one must not release the rest from inside its own destructor.
PromiseData::~PromiseData() {
    if (handlers.empty()) return;
    using Queue = ReleaseQueue<std::vector<PromiseHandler>>;
    if (Queue::busy()) {
        Queue::defer(std::move(handlers));
        return;
    }
    Queue::Drain drain;
    handlers.clear();
}

namespace {

using Args = const std::vector<JsValue>&;

// ---------------------------------------------------------------------------
// Collector support
// ---------------------------------------------------------------------------
//
// A pending promise's reactions are closures over script values. The promise
// itself may be reachable only from native code (a queued network request, a
// suspended async function), which the collector cannot see, so every pending
// promise that has reactions is listed here and its reactions are roots.

struct PendingRegistry {
    std::mutex mutex;
    std::vector<std::weak_ptr<PromiseData>> entries;

    PendingRegistry() {
        Heap::default_heap().add_root_provider(this, [this](std::vector<JsValue*>& roots) {
            std::lock_guard<std::mutex> lock(mutex);
            size_t kept = 0;
            for (size_t i = 0; i < entries.size(); ++i) {
                auto data = entries[i].lock();
                if (!data || data->state != PromiseState::Pending || data->handlers.empty()) continue;
                for (auto& handler : data->handlers) {
                    roots.push_back(&handler.on_fulfilled);
                    roots.push_back(&handler.on_rejected);
                    if (handler.keep_alive) {
                        for (auto& value : *handler.keep_alive) roots.push_back(&value);
                    }
                }
                entries[kept++] = entries[i];
            }
            entries.resize(kept);
        });
    }

    void add(const std::shared_ptr<PromiseData>& data) {
        std::lock_guard<std::mutex> lock(mutex);
        if (entries.size() >= 256 && (entries.size() & (entries.size() - 1)) == 0) {
            // Prune whenever the list doubles, so it stays proportional to the
            // number of promises that are actually pending.
            entries.erase(std::remove_if(entries.begin(), entries.end(), [](const std::weak_ptr<PromiseData>& weak) {
                auto d = weak.lock();
                return !d || d->state != PromiseState::Pending;
            }), entries.end());
        }
        entries.push_back(data);
    }
};

PendingRegistry& pending_registry() {
    static PendingRegistry* registry = new PendingRegistry();   // outlives every static destructor
    return *registry;
}

// ---------------------------------------------------------------------------
// Settling and reactions
// ---------------------------------------------------------------------------

void settle(const std::shared_ptr<PromiseData>& data, PromiseState state, JsValue value);

void run_reaction(const PromiseHandler& handler, bool rejected, const JsValue& value) {
    if (handler.native) {
        handler.native(rejected, value);
        return;
    }
    const JsValue& callback = rejected ? handler.on_rejected : handler.on_fulfilled;
    if (!callback.is_function()) {
        // No callback for this outcome: it passes through to the derived promise.
        if (handler.next_promise) settle(handler.next_promise, rejected ? PromiseState::Rejected : PromiseState::Fulfilled, value);
        return;
    }
    JsValue result;
    JsValue thrown;
    if (js_capture_throw([&] { result = callback.call({value}); }, thrown)) {
        Promise::resolve(handler.next_promise, result);
    } else {
        // What the callback threw becomes the rejection reason, as a value:
        // `catch (e)` further down the chain sees the same object.
        Promise::reject(handler.next_promise, thrown);
    }
}

void queue_reaction(PromiseHandler handler, bool rejected, const JsValue& value) {
    std::vector<JsRootSet> roots;
    roots.push_back(js_roots({handler.on_fulfilled, handler.on_rejected, value}));
    if (handler.keep_alive) roots.push_back(handler.keep_alive);
    EventLoop::default_loop().post_microtask(
        [handler = std::move(handler), rejected, value]() { run_reaction(handler, rejected, value); },
        std::move(roots));
}

void add_reaction(const std::shared_ptr<PromiseData>& data, PromiseHandler handler) {
    data->handled = true;
    if (data->state == PromiseState::Pending) {
        const bool first = data->handlers.empty();
        data->handlers.push_back(std::move(handler));
        if (first) pending_registry().add(data);
    } else {
        queue_reaction(std::move(handler), data->state == PromiseState::Rejected, data->result);
    }
}

void settle(const std::shared_ptr<PromiseData>& data, PromiseState state, JsValue value) {
    if (!data || data->state != PromiseState::Pending) return;
    data->state = state;
    data->result = std::move(value);
    data->locked = false;

    auto handlers = std::move(data->handlers);
    data->handlers.clear();
    for (auto& handler : handlers) {
        queue_reaction(std::move(handler), state == PromiseState::Rejected, data->result);
    }

    if (state == PromiseState::Rejected && !data->handled) {
        // Nothing is listening yet. Give the script until the end of the
        // current task to attach a handler, then report it: a rejection that
        // vanishes silently is the hardest kind of bug to find.
        EventLoop::default_loop().post_task([data]() {
            if (!data->handled) js_report_unhandled_rejection(data->result);
        }, {js_roots({data->result})});
    }
}

JsValue arg_or_undefined(Args args, size_t index) {
    return index < args.size() ? args[index] : JsValue::make_undefined();
}

std::shared_ptr<PromiseData> this_promise(const char* method) {
    JsValue me = JsValue::current_this();
    auto data = me.promise_value();
    if (!data) {
        js_throw_type_error(std::string("Method Promise.prototype.") + method +
                            " called on incompatible receiver " + js_describe_thrown(me));
    }
    return data;
}

/// The pair of functions handed to an executor or a thenable. Only the first
/// call to either has any effect.
std::pair<JsValue, JsValue> make_resolving_functions(const std::shared_ptr<PromiseData>& data) {
    auto called = std::make_shared<bool>(false);
    JsValue resolve_fn = js_make_native("resolve", 1, [data, called](Args args) {
        if (*called) return JsValue::make_undefined();
        *called = true;
        data->locked = false;
        Promise::resolve(data, arg_or_undefined(args, 0));
        return JsValue::make_undefined();
    });
    JsValue reject_fn = js_make_native("reject", 1, [data, called](Args args) {
        if (*called) return JsValue::make_undefined();
        *called = true;
        data->locked = false;
        settle(data, PromiseState::Rejected, arg_or_undefined(args, 0));
        return JsValue::make_undefined();
    });
    return {resolve_fn, reject_fn};
}

// GetPromiseResolve happens before GetIterator, but the returned method is
// called once *per value*, while the iterator is still open. The constructor
// fallback keeps the long-standing native `Promise.all` call-without-this API.
std::pair<JsValue, JsValue> combinator_resolve(const JsValue& default_constructor) {
    JsValue constructor = JsValue::current_this();
    if (!constructor.is_object_like()) constructor = default_constructor;
    JsValue resolve = constructor.get_property("resolve");
    if (!resolve.is_function()) js_throw_type_error("Promise resolve is not a function");
    return {constructor, resolve};
}

// Invoke(nextPromise, "then", ...), not PromiseResolve(nextPromise). A custom
// resolve can return an object whose `then` getter/call throws; that abrupt
// completion must reach js_iterate's visitor while it can close the iterator.
// Keep the native subscription for an unmodified native promise: it gives the
// pending-promise registry explicit roots for values captured by the reaction.
void subscribe_combinator_item(const JsValue& next_promise,
                               std::function<void(bool, const JsValue&)> reaction,
                               const std::shared_ptr<PromiseData>& result,
                               JsRootSet keep_alive = nullptr) {
    JsValue then_fn = next_promise.get_property("then");
    if (!then_fn.is_function()) js_throw_type_error("Promise result has no callable then");
    const JsValue builtin_then = js_object_value(JsIntrinsics::get().promise_proto).get_property("then");
    if (auto data = next_promise.promise_value(); data && strict_equals(then_fn, builtin_then)) {
        Promise::subscribe(data, std::move(reaction), std::move(keep_alive));
        return;
    }

    // A user-supplied then() can retain the callbacks indefinitely. Its
    // native closures are opaque to the collector; keep their captured JS
    // values in the result's registered reaction until it settles instead.
    if (keep_alive && result->state == PromiseState::Pending) {
        const bool first = result->handlers.empty();
        PromiseHandler roots;
        roots.native = [](bool, const JsValue&) {};
        roots.keep_alive = std::move(keep_alive);
        result->handlers.push_back(std::move(roots));
        if (first) pending_registry().add(result);
    }
    auto called = std::make_shared<bool>(false);
    auto callback = [called, reaction = std::move(reaction)](bool rejected, Args args) {
        if (!*called) {
            *called = true;
            reaction(rejected, arg_or_undefined(args, 0));
        }
        return JsValue::make_undefined();
    };
    JsValue fulfilled = js_make_native("", 1, [callback](Args args) { return callback(false, args); });
    JsValue rejected = js_make_native("", 1, [callback](Args args) { return callback(true, args); });
    // The resolve/reject element functions have [[Call]], not [[Construct]].
    // Built-in functions otherwise default to constructible in this engine.
    fulfilled.function_cell()->not_constructor = true;
    rejected.function_cell()->not_constructor = true;
    then_fn.call_with_this(next_promise, {fulfilled, rejected});
}

/// Shared by all(), allSettled() and any(): walks the iterable, subscribing to
/// each element as it is yielded, and settles `result` through `on_item`.
template <typename OnItem, typename OnEmpty>
JsValue combine(Args args, const JsValue& default_constructor, OnItem on_item, OnEmpty on_done) {
    auto result = std::make_shared<PromiseData>();
    auto values = std::make_shared<std::vector<JsValue>>();
    // Starts at 1 so a synchronous custom then() cannot finish the combinator
    // before the iterator has reported its end.
    auto remaining = std::make_shared<size_t>(1);

    JsValue thrown;
    const bool ok = js_capture_throw([&] {
        auto [constructor, resolve] = combinator_resolve(default_constructor);
        size_t index = 0;
        js_iterate(js_arg(args, 0), [&](const JsValue& item) {
            const size_t i = index++;
            // Resolve before subscribing and before asking for the next item.
            JsValue next_promise = resolve.call_with_this(constructor, {item});
            values->push_back(JsValue::make_undefined());
            ++*remaining;
            subscribe_combinator_item(next_promise,
                [result, values, remaining, i, on_item, on_done](bool rejected, const JsValue& value) {
                    if (result->state != PromiseState::Pending) return;
                    if (!on_item(result, *values, i, rejected, value)) return;
                    if (--*remaining == 0) on_done(result, *values);
                }, result, values);
            return true;
        });
    }, thrown);
    if (!ok) {
        settle(result, PromiseState::Rejected, thrown);
    } else if (--*remaining == 0) {
        on_done(result, *values);
    }
    return Promise::create_promise_instance(result);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Promise::resolve(const std::shared_ptr<PromiseData>& data, JsValue val) {
    if (!data || data->state != PromiseState::Pending || data->locked) return;

    if (val.is_object_like()) {
        if (val.promise_value() == data) {
            settle(data, PromiseState::Rejected,
                   js_make_error(JsErrorKind::TypeError, "Chaining cycle detected for promise"));
            return;
        }
        JsValue then_fn;
        JsValue thrown;
        if (!js_capture_throw([&] { then_fn = val.get_property("then"); }, thrown)) {
            settle(data, PromiseState::Rejected, thrown);
            return;
        }
        if (then_fn.is_function()) {
            // Adopt the thenable's eventual state. Its then() is called from a
            // microtask, never synchronously, so a misbehaving thenable cannot
            // run code in the middle of whoever is resolving this promise.
            data->locked = true;
            EventLoop::default_loop().post_microtask([data, val, then_fn]() {
                auto [resolve_fn, reject_fn] = make_resolving_functions(data);
                JsValue error;
                if (!js_capture_throw([&] { then_fn.call_with_this(val, {resolve_fn, reject_fn}); }, error)) {
                    // Only counts if the thenable had not already settled us.
                    reject_fn.call({error});
                }
            }, {js_roots({val, then_fn})});
            return;
        }
    }
    settle(data, PromiseState::Fulfilled, std::move(val));
}

void Promise::reject(const std::shared_ptr<PromiseData>& data, JsValue reason) {
    if (!data || data->state != PromiseState::Pending || data->locked) return;
    settle(data, PromiseState::Rejected, std::move(reason));
}

std::shared_ptr<PromiseData> Promise::to_promise(const JsValue& value) {
    if (auto data = value.promise_value()) return data;
    auto data = std::make_shared<PromiseData>();
    resolve(data, value);
    return data;
}

std::shared_ptr<PromiseData> Promise::then(const std::shared_ptr<PromiseData>& data,
                                           JsValue on_fulfilled, JsValue on_rejected) {
    auto next = std::make_shared<PromiseData>();
    PromiseHandler handler;
    handler.on_fulfilled = std::move(on_fulfilled);
    handler.on_rejected = std::move(on_rejected);
    handler.next_promise = next;
    add_reaction(data, std::move(handler));
    return next;
}

void Promise::subscribe(const std::shared_ptr<PromiseData>& data,
                        std::function<void(bool rejected, const JsValue& value)> reaction,
                        JsRootSet keep_alive) {
    PromiseHandler handler;
    handler.native = std::move(reaction);
    handler.keep_alive = std::move(keep_alive);
    add_reaction(data, std::move(handler));
}

JsValue Promise::create_promise_instance(std::shared_ptr<PromiseData> data) {
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = JsIntrinsics::get().promise_proto;
    cell->klass = ObjectClass::Promise;
    // The promise's state lives outside the cell; whoever can reach the
    // promise can reach its result and its reactions.
    cell->trace_extra = [data](Heap& heap) {
        // This capture and the promise value's payload are the two owners a
        // promise has when only script refers to it. Any more and native code
        // (a resolver handed to a timer, a suspended async function) can settle
        // it after the object is gone, so its reactions must not be counted as
        // belonging to this cell: they stay roots.
        if (heap.counting() && data.use_count() > 2) return;
        if (!heap.first_visit(data.get())) return;
        heap.mark_value(data->result);
        for (const auto& handler : data->handlers) {
            heap.mark_value(handler.on_fulfilled);
            heap.mark_value(handler.on_rejected);
            if (handler.keep_alive) {
                for (const auto& kept : *handler.keep_alive) heap.mark_value(kept);
            }
        }
    };
    JsValue promise = JsValue::make_object(cell);
    promise.set_promise_value(std::move(data));
    return promise;
}

JsValue Promise::make_resolved(JsValue val) {
    auto data = std::make_shared<PromiseData>();
    resolve(data, std::move(val));
    return create_promise_instance(data);
}

JsValue Promise::make_rejected(JsValue reason) {
    auto data = std::make_shared<PromiseData>();
    settle(data, PromiseState::Rejected, std::move(reason));
    return create_promise_instance(data);
}

JsValue Promise::create_constructor() {
    const JsValue* ctor = JsIntrinsics::get().find_global("Promise");
    return ctor ? *ctor : JsValue::make_undefined();
}

// ---------------------------------------------------------------------------
// The Promise object
// ---------------------------------------------------------------------------

void install_promise_builtins(JsIntrinsics& realm) {
    JsObject* pp = realm.promise_proto;

    JsValue ctor = js_make_native("Promise", 1, [](Args args) {
        if (!JsValue::current_is_construct()) {
            js_throw_type_error("Promise constructor cannot be invoked without 'new'");
        }
        const JsValue executor = js_arg(args, 0);
        if (!executor.is_function()) {
            js_throw_type_error("Promise resolver " + js_describe_thrown(executor) + " is not a function");
        }
        auto data = std::make_shared<PromiseData>();
        JsValue promise = Promise::create_promise_instance(data);
        auto [resolve_fn, reject_fn] = make_resolving_functions(data);
        JsValue thrown;
        // The executor runs synchronously; if it throws, the promise rejects.
        if (!js_capture_throw([&] { executor.call({resolve_fn, reject_fn}); }, thrown)) {
            reject_fn.call({thrown});
        }
        return promise;
    });
    ctor.define_property("prototype", js_object_value(pp), 0);
    js_define_hidden(pp, "constructor", ctor);
    js_define_hidden(pp, "@@toStringTag", JsValue::make_string("Promise"));

    // Promise[Symbol.species] (§27.2.4.7): the constructor itself. `then` does
    // not consult it yet — that is the half of this row still open (see
    // AGENTS.md) — but the property is the standard's, and a script that reads
    // it gets a function rather than `undefined`.
    {
        JsValue species_getter = js_make_native("get [Symbol.species]", 0, [](Args) { return JsValue::current_this(); });
        species_getter.function_cell()->not_constructor = true;
        ctor.define_accessor(kSymbolSpeciesKey, species_getter, JsValue::make_undefined(),
                             PropertyMap::Configurable);
    }

    // ---- Promise.prototype ------------------------------------------------
    js_define_method(pp, "then", 2, [](Args args) {
        auto data = this_promise("then");
        return Promise::create_promise_instance(Promise::then(data, js_arg(args, 0), js_arg(args, 1)));
    });
    js_define_method(pp, "catch", 1, [](Args args) {
        auto data = this_promise("catch");
        return Promise::create_promise_instance(Promise::then(data, JsValue::make_undefined(), js_arg(args, 0)));
    });
    js_define_method(pp, "finally", 1, [](Args args) {
        auto data = this_promise("finally");
        const JsValue callback = js_arg(args, 0);
        if (!callback.is_function()) {
            return Promise::create_promise_instance(Promise::then(data, callback, callback));
        }
        // The callback's return value is ignored (unless it is a rejected
        // promise or it throws); the original outcome passes through.
        auto next = std::make_shared<PromiseData>();
        Promise::subscribe(data, [next, callback](bool rejected, const JsValue& value) {
            JsValue returned;
            JsValue thrown;
            if (!js_capture_throw([&] { returned = callback.call({}); }, thrown)) {
                Promise::reject(next, thrown);
                return;
            }
            Promise::subscribe(Promise::to_promise(returned), [next, rejected, value](bool inner_rejected, const JsValue& inner) {
                if (inner_rejected) Promise::reject(next, inner);
                else if (rejected) Promise::reject(next, value);
                else Promise::resolve(next, value);
            }, js_roots({value}));
        }, js_roots({callback}));
        return Promise::create_promise_instance(next);
    });

    // ---- Static methods ---------------------------------------------------
    js_define_method(ctor, "resolve", 1, [](Args args) {
        const JsValue& value = js_arg(args, 0);
        if (value.promise_value()) return value;        // already a promise: returned as is
        return Promise::make_resolved(value);
    });
    js_define_method(ctor, "reject", 1, [](Args args) {
        return Promise::make_rejected(js_arg(args, 0));
    });
    js_define_method(ctor, "withResolvers", 0, [](Args) {
        auto data = std::make_shared<PromiseData>();
        auto [resolve_fn, reject_fn] = make_resolving_functions(data);
        JsValue result = JsValue::make_object();
        result.set_property("promise", Promise::create_promise_instance(data));
        result.set_property("resolve", resolve_fn);
        result.set_property("reject", reject_fn);
        return result;
    });

    js_define_method(ctor, "all", 1, [](Args args) {
        return combine(args, Promise::create_constructor(),
            [](const std::shared_ptr<PromiseData>& result, std::vector<JsValue>& values, size_t i,
               bool rejected, const JsValue& value) {
                if (rejected) {
                    Promise::reject(result, value);     // the first rejection wins
                    return false;
                }
                values[i] = value;
                return true;
            },
            [](const std::shared_ptr<PromiseData>& result, std::vector<JsValue>& values) {
                Promise::resolve(result, JsValue::make_array(values));
            });
    });

    js_define_method(ctor, "allSettled", 1, [](Args args) {
        return combine(args, Promise::create_constructor(),
            [](const std::shared_ptr<PromiseData>&, std::vector<JsValue>& values, size_t i,
               bool rejected, const JsValue& value) {
                JsValue entry = JsValue::make_object();
                entry.set_property("status", JsValue::make_string(rejected ? "rejected" : "fulfilled"));
                entry.set_property(rejected ? "reason" : "value", value);
                values[i] = entry;
                return true;
            },
            [](const std::shared_ptr<PromiseData>& result, std::vector<JsValue>& values) {
                Promise::resolve(result, JsValue::make_array(values));
            });
    });

    js_define_method(ctor, "any", 1, [](Args args) {
        return combine(args, Promise::create_constructor(),
            [](const std::shared_ptr<PromiseData>& result, std::vector<JsValue>& values, size_t i,
               bool rejected, const JsValue& value) {
                if (!rejected) {
                    Promise::resolve(result, value);    // the first fulfilment wins
                    return false;
                }
                values[i] = value;
                return true;
            },
            [](const std::shared_ptr<PromiseData>& result, std::vector<JsValue>& values) {
                JsValue error = js_make_error(JsErrorKind::Error, "All promises were rejected");
                error.define_property("name", JsValue::make_string("AggregateError"), PropertyMap::Hidden);
                error.define_property("errors", JsValue::make_array(values), PropertyMap::Hidden);
                Promise::reject(result, error);
            });
    });

    js_define_method(ctor, "race", 1, [](Args args) {
        auto result = std::make_shared<PromiseData>();
        JsValue thrown;
        const bool ok = js_capture_throw([&] {
            auto [constructor, resolve] = combinator_resolve(Promise::create_constructor());
            js_iterate(js_arg(args, 0), [&](const JsValue& item) {
                JsValue next_promise = resolve.call_with_this(constructor, {item});
                subscribe_combinator_item(next_promise, [result](bool rejected, const JsValue& value) {
                    if (rejected) Promise::reject(result, value);
                    else Promise::resolve(result, value);
                }, result);
                return true;
            });
        }, thrown);
        if (!ok) Promise::reject(result, thrown);
        return Promise::create_promise_instance(result);
    });

    realm.add_global("Promise", ctor);
}

} // namespace litebird
