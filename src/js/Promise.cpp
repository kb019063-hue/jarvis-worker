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
#include <cstdio>
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

void PromiseData::release_resolvers() {
    for (auto& weak : resolver_states) {
        if (auto state = weak.lock()) state->data.reset();
    }
    resolver_states.clear();
}

namespace {

using Args = const std::vector<JsValue>&;

// ---------------------------------------------------------------------------
// Collector support
// ---------------------------------------------------------------------------
//
// A pending promise's reactions are closures over script values. The promise
// itself may be reachable only from native code (a resolver handed to a
// timer, a suspended async function, or a queued thenable job), which the
// collector cannot see, so every pending promise that has reactions is
// listed here and its stored values are roots. The resolvers own the state
// until it settles; this list is a weak second owner whose only job is to
// keep the reactions rooted while the promise is pending.

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
                roots.push_back(&data->result);
                for (auto& handler : data->handlers) {
                    roots.push_back(&handler.on_fulfilled);
                    roots.push_back(&handler.on_rejected);
                    roots.push_back(&handler.resolve);
                    roots.push_back(&handler.reject);
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
                auto data = weak.lock();
                return !data || data->state != PromiseState::Pending || data->handlers.empty();
            }), entries.end());
        }
        entries.push_back(data);
    }

    void release(const std::shared_ptr<PromiseData>& data) {
        std::lock_guard<std::mutex> lock(mutex);
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&data](const std::weak_ptr<PromiseData>& entry) {
            return entry.lock() == data;
        }), entries.end());
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

/// Settles the promise a reaction produced. When the reaction was made by a
/// constructor that is not %Promise% (`capability` holds that constructor's
/// own resolve/reject), the engine calls those, as PromiseReactionJob does;
/// the promise state is the engine's own only when the constructor is.
void settle_through(const PromiseHandler& handler, bool rejected, const JsValue& value) {
    // PromiseReactionJob steps 8-9: the reaction settles the capability with
    // the function that matches the outcome -- [[Reject]] for a rejection and
    // [[Resolve]] for a value. Calling the resolve for both fulfilled and
    // rejected, as this did, turned every rejection that passed through a
    // `then` with no handler for it into a value: a chain's `catch` was never
    // reached and the reason arrived at the next `then` as an answer.
    const JsValue& settle_fn = rejected ? handler.reject : handler.resolve;
    if (settle_fn.is_function()) {
        // What that call throws does not reach the caller of `then`: the job
        // is not the script's, and the answer is a new rejected promise.
        JsValue thrown;
        if (!js_capture_throw([&] { settle_fn.call({value}); }, thrown)) {
            js_report_unhandled_rejection(thrown);
        }
        return;
    }
    settle(handler.next_promise, rejected ? PromiseState::Rejected : PromiseState::Fulfilled, value);
}

void run_reaction(const PromiseHandler& handler, bool rejected, const JsValue& value) {
    if (handler.native) {
        handler.native(rejected, value);
        return;
    }
    const JsValue& callback = rejected ? handler.on_rejected : handler.on_fulfilled;
    if (!callback.is_function()) {
        // No callback for this outcome: it passes through to the derived
        // promise, through the capability the constructor gave (if any).
        settle_through(handler, rejected, value);
        return;
    }
    JsValue result;
    JsValue thrown;
    if (js_capture_throw([&] { result = callback.call({value}); }, thrown)) {
        // The reaction's own resolve, exactly, not PromiseResolve: a subclass's
        // resolve was handed to the constructor and is what the job calls.
        settle_through(handler, false, result);
    } else {
        // What the callback threw becomes the rejection reason, as a value:
        // `catch (e)` further down the chain sees the same object.
        settle_through(handler, true, thrown);
    }
}

void queue_reaction(PromiseHandler handler, bool rejected, const JsValue& value) {
    std::vector<JsRootSet> roots;
    roots.push_back(js_roots({handler.on_fulfilled, handler.on_rejected, handler.resolve, handler.reject, value}));
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
    data->release_resolvers();

    auto handlers = std::move(data->handlers);
    data->handlers.clear();
    pending_registry().release(data);
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
    auto data = Promise::state_of(me);
    if (!data) {
        js_throw_type_error(std::string("Method Promise.prototype.") + method +
                            " called on incompatible receiver " + js_describe_thrown(me));
    }
    return data;
}

/// The pair of functions handed to an executor or a thenable. Only the first
/// call to either has any effect.
std::pair<JsValue, JsValue> make_resolving_functions(const std::shared_ptr<PromiseData>& data) {
    auto state = std::make_shared<ResolverState>();
    state->data = data;
    data->resolver_states.push_back(state);
    // §27.2.1.3.2 steps 3-4 and §27.2.1.4.2: the pair is made by
    // CreateBuiltinFunction with the empty string for a name, `length` 1 and no
    // `[[Construct]]` -- they are not `new resolve()`. Node prints ` 1` for
    // `resolve.name + ' ' + resolve.length` for exactly this reason, and a
    // script can tell: `new resolve(function () {})` was accepted here.
    JsValue resolve_fn = js_make_native("", 1, [state](Args args) {
        auto data = state->data;
        if (!data) return JsValue::make_undefined();
        if (state->called) return JsValue::make_undefined();
        state->called = true;
        data->locked = false;
        Promise::resolve(data, arg_or_undefined(args, 0));
        return JsValue::make_undefined();
    });
    JsValue reject_fn = js_make_native("", 1, [state](Args args) {
        auto data = state->data;
        if (!data) return JsValue::make_undefined();
        if (state->called) return JsValue::make_undefined();
        state->called = true;
        data->locked = false;
        settle(data, PromiseState::Rejected, arg_or_undefined(args, 0));
        return JsValue::make_undefined();
    });
    resolve_fn.function_cell()->not_constructor = true;
    reject_fn.function_cell()->not_constructor = true;
    return {resolve_fn, reject_fn};
}

// NewPromiseCapability (§27.2.1.5) and the two halves it needs.
//
// A capability's resolve and reject are the constructor's own two functions,
// captured by the GetCapabilitiesExecutor the constructor is handed. Where the
// constructor is %Promise% itself the engine knows the pair it is about to
// make, and makes it directly: the promise it settles is then the engine's own
// state, which is the case every script's `new Promise` takes.

JsValue make_capability_executor(JsValue* resolve, JsValue* reject) {
    // §27.2.1.5.1: the executor notes the first pair of functions it is given
    // and throws on a second, and it is anonymous (no name), length 2, and has
    // no [[Construct]].
    JsValue executor = js_make_native("", 2, [resolve, reject](Args args) {
        if (resolve->is_undefined() && reject->is_undefined()) {
            *resolve = arg_or_undefined(args, 0);
            *reject = arg_or_undefined(args, 1);
            return JsValue::make_undefined();
        }
        js_throw_type_error("Promise executor has already been invoked with non-undefined arguments");
    });
    executor.function_cell()->not_constructor = true;
    return executor;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::shared_ptr<PromiseData> Promise::state_of(const JsValue& value) {
    if (auto payload = value.promise_value()) return payload;
    // The cell, when the value did not keep the payload: see the declaration.
    JsObject* cell = value.object_val.get();
    if (cell && cell->klass == ObjectClass::Promise && cell->internal) {
        return std::static_pointer_cast<PromiseData>(cell->internal);
    }
    return nullptr;
}

bool Promise::is_promise(const JsValue& value) {
    return static_cast<bool>(state_of(value));
}

PromiseCapability Promise::capability_for(const JsValue& promise) {
    // A capability over a promise that already exists: the object is the one
    // the constructor answered, and the engine settles it where it can.
    PromiseCapability capability;
    capability.promise = promise;
    std::shared_ptr<PromiseData> data = Promise::state_of(promise);
    if (!data) return capability;
    auto [resolve_fn, reject_fn] = make_resolving_functions(data);
    capability.resolve = resolve_fn;
    capability.reject = reject_fn;
    return capability;
}

PromiseCapability Promise::new_capability(const JsValue& constructor) {
    if (!constructor.is_function() || !js_is_constructor(constructor)) {
        js_throw_type_error(js_describe_thrown(constructor) + " is not a constructor");
    }
    // %Promise%: the pair the engine makes is the pair the constructor would
    // have been handed, so it is made here and the constructor is not called.
    // A subclass overriding the constructor is not %Promise%, and goes below.
    const JsValue* builtin = JsIntrinsics::get().find_global("Promise");
    if (builtin && strict_equals(constructor, *builtin)) {
        auto data = std::make_shared<PromiseData>();
        PromiseCapability capability;
        capability.promise = Promise::create_promise_instance(data);
        auto [resolve_fn, reject_fn] = make_resolving_functions(data);
        capability.resolve = resolve_fn;
        capability.reject = reject_fn;
        return capability;
    }
    PromiseCapability capability;
    JsValue executor = make_capability_executor(&capability.resolve, &capability.reject);
    JsValue made = js_construct(constructor, {executor});
    if (!made.is_object_like()) {
        js_throw_type_error("Promise constructor returned a non-object");
    }
    if (!capability.resolve.is_function() || !capability.reject.is_function()) {
        if (capability.resolve.is_undefined() && capability.reject.is_undefined()) {
            js_throw_type_error("Promise constructor did not call the executor");
        }
        js_throw_type_error("Promise resolve or reject function is not callable");
    }
    capability.promise = made;
    return capability;
}

JsValue Promise::resolve_from(const JsValue& constructor, const JsValue& value) {
    const JsValue* builtin = JsIntrinsics::get().find_global("Promise");
    // §27.2.4.7.1, steps 2-3: a value that is already a promise of this very
    // constructor is the answer itself, not a new promise around it. The
    // combinators depend on it: `Promise.all` asks `this.resolve` for each
    // item, and `then` on the answer is what the iteration is watching. Wrapping
    // a promise here hid a `then` getter that throws -- the failure surfaced a
    // step later, outside `js_iterate`, so the iterator was never closed.
    if (Promise::state_of(value)) {
        JsValue value_constructor;
        JsValue thrown;
        if (!js_capture_throw([&] { value_constructor = value.get_property("constructor"); }, thrown)) {
            js_throw(thrown);
        }
        // A plain promise whose `constructor` was deleted, or set to something
        // that is not a function, is still a promise of %Promise%: its own
        // prototype is what says so (`not-a-constructor.js`).
        if (builtin && strict_equals(constructor, *builtin) && !value_constructor.is_function() &&
            !value_constructor.is_object_like()) {
            return value;
        }
        if (strict_equals(value_constructor, constructor)) return value;
    }
    if (builtin && strict_equals(constructor, *builtin)) return Promise::make_resolved(value);
    PromiseCapability capability = Promise::new_capability(constructor);
    capability.resolve.call({value});
    return capability.promise;
}

namespace {

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
    if (auto data = Promise::state_of(next_promise); data && strict_equals(then_fn, builtin_then)) {
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
    // §27.2.4.1: C is the receiver (the constructor the method was called on),
    // and the answer is a promise of C's, settled through C's own functions.
    JsValue constructor = JsValue::current_this();
    if (!constructor.is_object_like()) constructor = default_constructor;
    PromiseCapability capability = Promise::new_capability(constructor);
    // The engine's state behind the answer when it can see it; a subclass's
    // promise is settled through its capability, and the state is a stand-in.
    auto result = Promise::state_of(capability.promise);
    if (!result) result = std::make_shared<PromiseData>();
    JsValue answer = capability.promise;
    // The engine settles `result` and then the answer through the capability's
    // own resolve: for %Promise% they are the same promise.
    const bool through_capability = Promise::state_of(answer) != result;
    auto settle_answer = [capability, result, through_capability](bool rejected, const JsValue& value) {
        if (through_capability) {
            (rejected ? capability.reject : capability.resolve).call({value});
        } else if (rejected) {
            settle(result, PromiseState::Rejected, value);
        } else {
            Promise::resolve(result, value);
        }
    };

    auto values = std::make_shared<std::vector<JsValue>>();
    // Starts at 1 so a synchronous custom then() cannot finish the combinator
    // before the iterator has reported its end.
    auto remaining = std::make_shared<size_t>(1);

    JsValue thrown;
    const bool ok = js_capture_throw([&] {
        JsValue resolve = constructor.get_property("resolve");
        if (!resolve.is_function()) js_throw_type_error("Promise resolve is not a function");
        size_t index = 0;
        js_iterate(js_arg(args, 0), [&](const JsValue& item) {
            const size_t i = index++;
            // Resolve before subscribing and before asking for the next item.
            JsValue next_promise = resolve.call_with_this(constructor, {item});
            values->push_back(JsValue::make_undefined());
            ++*remaining;
            subscribe_combinator_item(next_promise,
                [result, values, remaining, i, on_item, on_done, settle_answer](bool rejected, const JsValue& value) {
                    if (result->state != PromiseState::Pending) return;
                    if (!on_item(result, *values, i, rejected, value)) {
                        // The combinator is decided: the answer takes it.
                        if (rejected) settle_answer(true, value);
                        else settle_answer(false, value);
                        return;
                    }
                    if (--*remaining == 0) on_done(result, *values);
                }, result, values);
            return true;
        });
    }, thrown);
    if (!ok) {
        settle_answer(true, thrown);
    } else if (--*remaining == 0) {
        on_done(result, *values);
    }
    return answer;
}


/// The realm's own `AggregateError`, which Promise.any rejects with (§27.2.4.7
/// step 9 makes it with the intrinsic, not through the `AggregateError` a
/// script may have put on the global). Undefined in a realm that has none,
/// and then a plain error is made instead, as this engine did before the
/// constructor existed at all.
JsValue aggregate_error_constructor() {
    JsIntrinsics& realm = JsIntrinsics::get();
    // A realm is installed once, so this is the constructor the library put
    // there unless the script replaced it. `structuredClone` and
    // `queueMicrotask` are read the same way, and `Promise` is a global too.
    if (const JsValue* found = realm.find_global("AggregateError")) return *found;
    return JsValue::make_undefined();
}

// §27.2.4.7, Promise.any. It is not `combine`: that one settles as soon as a
// slot decides the answer, and the whole list of reasons is needed when every
// promise rejects. A reason can also be `undefined`, which a value slot cannot
// tell from a promise that has not rejected at all -- the slot starts as
// undefined -- so the state is kept apart from it. Counting decides: while
// anything is still pending the answer waits, and the last rejection makes the
// error, in the order the promises were given.
struct AnyState {
    std::shared_ptr<std::vector<JsValue>> values;    // the reason at each index
    std::shared_ptr<std::vector<bool>> rejected;     // whether it has one
    std::shared_ptr<size_t> counted;                 // promises seen so far
    std::shared_ptr<size_t> settled;                 // how many have settled
    std::shared_ptr<bool> ended;                     // the iterator has ended
    std::shared_ptr<bool> decided;

    void finish(const std::shared_ptr<PromiseData>& result, size_t index, bool was_rejected,
                const JsValue& value) {
        if (*decided) return;
        if (!was_rejected) {
            *decided = true;
            Promise::resolve(result, value);         // the first fulfilment wins
            return;
        }
        // The reason, which can be `undefined` itself: the slot starts as
        // undefined and cannot tell that apart from "not rejected".
        if (index < values->size()) {
            (*values)[index] = value;
            (*rejected)[index] = true;
        }
        ++*settled;
        // Everything has rejected only when the list is complete too: the
        // last promise may settle before the iterator has ended, and a
        // custom `then` may settle one while it is still being built.
        if (!*ended || *settled < *counted) return;
        *decided = true;
        std::vector<JsValue> reasons;
        reasons.reserve(values->size());
        for (size_t i = 0; i < values->size(); ++i) {
            if ((*rejected)[i]) reasons.push_back((*values)[i]);
        }
        JsValue aggregate = JsValue::make_undefined();
        JsValue thrown;
        const bool built = js_capture_throw([&] {
            const JsValue ctor = aggregate_error_constructor();
            if (!ctor.is_function()) {
                // A realm without the constructor (before Phase 285 there was
                // none anywhere): the shape it had, a plain error named so.
                aggregate = js_make_error(JsErrorKind::Error, "All promises were rejected");
                aggregate.define_property("name", JsValue::make_string("AggregateError"), PropertyMap::Hidden);
                aggregate.define_property("errors", JsValue::make_array(std::move(reasons)), PropertyMap::Hidden);
                return;
            }
            aggregate = js_construct(ctor, {JsValue::make_array(std::move(reasons)),
                                            JsValue::make_string("All promises were rejected")});
        }, thrown);
        Promise::reject(result, built ? aggregate : thrown);
    }

    /// The iterator has ended: finish if everything settled while it ran.
    void complete(const std::shared_ptr<PromiseData>& result) {
        *ended = true;
        if (*decided || *settled < *counted) return;
        if (*counted == 0) {
            finish(result, 0, true, JsValue::make_undefined());   // nothing to fulfil with
        } else {
            // Every promise settled, each one through finish().
            *decided = true;
        }
    }
};

JsValue promise_any(Args& args) {
    // §27.2.4.7 steps 1-3: the receiver's capability, and `C.resolve`.
    JsValue constructor = JsValue::current_this();
    if (!constructor.is_object_like()) constructor = Promise::create_constructor();
    PromiseCapability capability = Promise::new_capability(constructor);
    auto result = Promise::state_of(capability.promise);
    if (!result) result = std::make_shared<PromiseData>();
    const bool through_capability = Promise::state_of(capability.promise) != result;
    auto settle_answer = [capability, result, through_capability](bool rejected, const JsValue& value) {
        if (through_capability) (rejected ? capability.reject : capability.resolve).call({value});
        else if (rejected) Promise::reject(result, value);
        else Promise::resolve(result, value);
    };

    auto state = std::make_shared<AnyState>();
    state->values = std::make_shared<std::vector<JsValue>>();
    state->rejected = std::make_shared<std::vector<bool>>();
    state->counted = std::make_shared<size_t>(0);
    state->settled = std::make_shared<size_t>(0);
    state->ended = std::make_shared<bool>(false);
    state->decided = std::make_shared<bool>(false);
    // The promise the engine settles. The subscription is given it as well,
    // which is what keeps the cell alive while the promises settle.
    const auto engine_result = result;

    JsValue thrown;
    const bool ok = js_capture_throw([&] {
        JsValue resolve = constructor.get_property("resolve");
        if (!resolve.is_function()) js_throw_type_error("Promise resolve is not a function");
        size_t index = 0;
        js_iterate(js_arg(args, 0), [&](const JsValue& item) {
            // An index is taken before the promise is made: a custom `then`
            // can settle it inside `subscribe_combinator_item`.
            const size_t i = index++;
            JsValue next_promise = resolve.call_with_this(constructor, {item});
            state->values->push_back(JsValue::make_undefined());
            state->rejected->push_back(false);
            ++*state->counted;
            subscribe_combinator_item(next_promise,
                [state, engine_result, i, settle_answer](bool rejected, const JsValue& value) {
                    if (*state->decided) return;
                    state->finish(engine_result, i, rejected, value);
                    // A subclass's promise is a stand-in: it takes the answer.
                    if (*state->decided && engine_result->state != PromiseState::Pending) {
                        settle_answer(engine_result->state == PromiseState::Rejected, engine_result->result);
                    }
                }, result, state->values);
            return true;
        });
    }, thrown);
    if (!ok) {
        settle_answer(true, thrown);
        return capability.promise;
    }
    state->complete(engine_result);
    if (*state->decided && engine_result->state == PromiseState::Rejected) {
        settle_answer(true, engine_result->result);
    }
    return capability.promise;
}

} // namespace

void Promise::resolve(const std::shared_ptr<PromiseData>& data, JsValue val) {
    if (!data || data->state != PromiseState::Pending || data->locked) return;

    if (val.is_object_like()) {
        if (Promise::state_of(val) == data) {
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
    if (auto data = Promise::state_of(value)) return data;
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

PromiseCapability Promise::capability_for(const std::shared_ptr<PromiseData>& data,
                                          const JsValue& constructor) {
    const JsValue* builtin = JsIntrinsics::get().find_global("Promise");
    if (builtin && strict_equals(constructor, *builtin)) {
        // %Promise%: the engine's own state, settled directly. It has to be a
        // state of its own -- the promise `then` answers is a different
        // promise from the receiver, and giving the two one state made every
        // settlement of the new one settle the source as well: `p.then(g)`
        // handed g what f had already received.
        (void)data;
        PromiseCapability capability;
        auto next = std::make_shared<PromiseData>();
        capability.promise = Promise::create_promise_instance(next);
        // Bind the pair to the new state: the reaction settles the promise
        // through these, and Promise::resolve is what adopts a thenable a
        // handler answered with (`then(function (r) { return r.json(); })`).
        // Settling the state directly would fulfil with the promise itself.
        auto [resolve_fn, reject_fn] = make_resolving_functions(next);
        capability.resolve = resolve_fn;
        capability.reject = reject_fn;
        return capability;
    }
    // The constructor's promise is made, and settled through its own
    // functions; the engine's state carries the reactions either way.
    return Promise::new_capability(constructor);
}

JsValue Promise::then_into(const JsValue& promise, const std::shared_ptr<PromiseData>& data,
                           JsValue on_fulfilled, JsValue on_rejected, PromiseCapability capability) {
    // NewPromiseCapability has already run: a constructor that throws, refuses
    // the executor or answers a non-object has thrown in `then` itself, before
    // anything is registered.
    // What the reaction settles is the promise NewPromiseCapability made. For
    // %Promise% that promise is the engine's own, carrying the state it was
    // made with; a fresh one here would be settled while the promise handed
    // back stayed pending, and a chain would give every step the first value
    // (`p.then(f).then(g)` -- g saw what f was given, not what it returned).
    auto next = Promise::state_of(capability.promise);
    if (!next) next = std::make_shared<PromiseData>();
    PromiseHandler handler;
    handler.on_fulfilled = std::move(on_fulfilled);
    handler.on_rejected = std::move(on_rejected);
    handler.next_promise = next;
    handler.resolve = capability.resolve;       // undefined for %Promise%
    handler.reject = capability.reject;
    add_reaction(data, std::move(handler));
    (void)promise;
    return capability.promise;
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
    // The state is reachable from the cell as well as from the value: a class
    // that extends Promise takes its parent's object over into the one `new`
    // made (JsInterpreter::eval_super_call, take_over), and what travels
    // between two cells is what a cell holds. The value's payload is what the
    // engine copies around; `internal` is what survives a cell being copied.
    cell->internal = data;
    // The promise's state lives outside the cell; whoever can reach the
    // promise can reach its result and its reactions. Pending reactions are
    // registered in `pending_registry`, so a promise held only by native code
    // still keeps them rooted; a settled promise's result has no such need and
    // is counted like any other reference stored in the cell.
    cell->trace_extra = [cell](Heap& heap) {
        const auto state = std::static_pointer_cast<PromiseData>(cell->internal);
        const PromiseData& data = *state;
        if (!heap.first_visit(state.get())) return;
        heap.mark_value(data.result);
        for (const auto& handler : data.handlers) {
            heap.mark_value(handler.on_fulfilled);
            heap.mark_value(handler.on_rejected);
            heap.mark_value(handler.resolve);
            heap.mark_value(handler.reject);
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
        // §27.2.3.1: the object is OrdinaryCreateFromConstructor(new.target,
        // "%Promise.prototype%", « [[PromiseState]] ... »). The executor is
        // checked afterwards -- `Reflect.construct(Promise, [], NewTarget)`
        // complains about the executor before NewTarget.prototype is read --
        // and a new.target whose prototype is not an object leaves the
        // promise with %Promise.prototype% (`get-prototype-abrupt.js`).
        const JsValue executor = js_arg(args, 0);
        // The executor is asked about first, before new.target's prototype is
        // read, and nothing of the promise exists until it has been accepted:
        // §27.2.3.1 steps 3-7 read `NewTarget.prototype` only after step 3 has
        // refused a non-callable executor. (`Reflect.construct(Promise, [],
        // NewTarget)` with a counting prototype getter reads it zero times.)
        if (!executor.is_function()) {
            js_throw_type_error("Promise resolver " + js_describe_thrown(executor) + " is not a function");
        }
        auto data = std::make_shared<PromiseData>();
        JsValue promise = Promise::create_promise_instance(data);
        // OrdinaryCreateFromConstructor(new.target, "%Promise.prototype%"): the
        // object gets new.target's `prototype` when that is an object, and
        // %Promise.prototype% otherwise. current_derived_this() is the engine's
        // one place for that question: it reads the property once per call and
        // remembers the answer in the call's slot, which js_construct then
        // uses instead of reading the property a second time (`Reflect
        // .construct(Promise, [fn], NewTarget)` with a counting getter: one
        // read). Null there means it was read and was not an object; no
        // answer at all means this is an ordinary call and there is nothing to
        // do.
        if (const JsValue* chosen = JsValue::current_derived_this()) {
            if (chosen->is_object_like()) {
                if (JsObject* target = chosen->as_prototype()) promise.object_val->prototype = target;
            }
        }
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
    // §27.2.5.4: the receiver, then SpeciesConstructor(promise, %Promise%),
    // then NewPromiseCapability(C). The promise the reaction fills is the
    // species constructor's, so `class P extends Promise {}` gets a P back.
    js_define_method(pp, "then", 2, [](Args args) {
        JsValue me = JsValue::current_this();
        if (!Promise::is_promise(me)) {
            js_throw_type_error("Method Promise.prototype.then called on incompatible receiver " +
                                js_describe_thrown(me));
        }
        auto data = Promise::state_of(me);
        JsValue constructor = species_constructor(me, Promise::create_constructor());
        PromiseCapability capability = Promise::capability_for(data, constructor);
        return Promise::then_into(me, data, js_arg(args, 0), js_arg(args, 1), capability);
    });
    // §27.2.5.1: a call to `then`, whatever the receiver it was found on --
    // so a receiver that only has `then` is enough, and its own `then` is what
    // runs (`SpeciesConstructor` is asked by `then`, not here).
    js_define_method(pp, "catch", 1, [](Args args) {
        JsValue me = JsValue::current_this();
        JsValue then = me.get_property("then");
        if (!then.is_function()) js_throw_type_error("Promise.prototype.catch: then is not a function");
        return then.call_with_this(me, {JsValue::make_undefined(), js_arg(args, 0)});
    });
    // §27.2.5.3: `then` twice with the wrappers the standard names
    // (ThenFinally and CatchFinally). Both wrappers return `valueThunk`'s or
    // `thenFinally`'s answer, and a non-callable argument makes them the
    // identity: the original outcome then passes through untouched, and it is
    // `then` that makes the promise, so a subclass's species is honoured.
    js_define_method(pp, "finally", 1, [](Args args) {
        JsValue me = JsValue::current_this();
        if (!me.is_object_like()) {
            js_throw_type_error("Method Promise.prototype.finally called on incompatible receiver " +
                                js_describe_thrown(me));
        }
        const JsValue on_finally = js_arg(args, 0);
        // PromiseResolve(%Promise%, value) for `then`'s first call, which the
        // standard spells as `then` on the result of ThenFinally.
        auto make_wrapper = [on_finally](bool fulfilled) {
            return js_make_native("", 1, [on_finally, fulfilled](Args inner) {
                const JsValue& value = arg_or_undefined(inner, 0);
                if (!on_finally.is_function()) return value;
                JsValue result;
                JsValue thrown;
                if (!js_capture_throw([&] { result = on_finally.call({}); }, thrown)) js_throw(thrown);
                JsValue value_thunk = js_make_native("", 0, [value](Args) { return value; });
                value_thunk.function_cell()->not_constructor = true;
                JsValue from_finally = Promise::resolve_from(Promise::create_constructor(), result);
                if (fulfilled) return from_finally.get_property("then").call_with_this(from_finally, {value_thunk});
                JsValue thrower = js_make_native("", 0, [value](Args) -> JsValue {
                    js_throw(value);
                    return JsValue::make_undefined();
                });
                thrower.function_cell()->not_constructor = true;
                return from_finally.get_property("then").call_with_this(from_finally, {thrower});
            });
        };
        // `onFinally` is read once, as the wrapper closes over it.
        JsValue then_fn = me.get_property("then");
        if (!then_fn.is_function()) js_throw_type_error("Promise.prototype.finally: then is not a function");
        return then_fn.call_with_this(me, {make_wrapper(true), make_wrapper(false)});
    });

    // ---- Static methods ---------------------------------------------------
    // All four of these are generic in their receiver (§27.2.4): `this` is a
    // constructor, and what it makes is the promise the answer is. `resolve`
    // and `reject` are on %Promise% itself too, where the receiver is the
    // constructor and the engine makes the promise and its state together.
    js_define_method(ctor, "resolve", 1, [](Args args) {
        // A copy: current_this() is a reference into the call context, and the
        // constructor below starts a call that overwrites it. Held by reference
        // the receiver became that call's `this` -- an object -- and the
        // constructor it stood for was lost (Phase 196).
        const JsValue constructor = JsValue::current_this();
        return Promise::resolve_from(constructor, js_arg(args, 0));
    });
    js_define_method(ctor, "reject", 1, [](Args args) {
        const JsValue constructor = JsValue::current_this();   // a copy; see `resolve`
        PromiseCapability capability = Promise::new_capability(constructor);
        capability.reject.call({js_arg(args, 0)});
        return capability.promise;
    });
    js_define_method(ctor, "withResolvers", 0, [](Args) {
        // §27.2.4.8: the object has `promise`, then `resolve`, then `reject`,
        // in that order, and they are the capability's own functions.
        const JsValue constructor = JsValue::current_this();   // a copy; see `resolve`
        PromiseCapability capability = Promise::new_capability(constructor);
        JsValue result = JsValue::make_object();
        result.set_property("promise", capability.promise);
        result.set_property("resolve", capability.resolve);
        result.set_property("reject", capability.reject);
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
        return promise_any(args);
    });

    js_define_method(ctor, "race", 1, [](Args args) {
        // §27.2.4.5: the first element to settle decides the answer, through
        // the capability's own functions.
        JsValue constructor = JsValue::current_this();
        if (!constructor.is_object_like()) constructor = Promise::create_constructor();
        PromiseCapability capability = Promise::new_capability(constructor);
        auto result = Promise::state_of(capability.promise);
        const bool through_capability = !result;
        if (through_capability) result = std::make_shared<PromiseData>();
        auto settle_answer = [capability, result, through_capability](bool rejected, const JsValue& value) {
            if (through_capability) (rejected ? capability.reject : capability.resolve).call({value});
            else if (rejected) Promise::reject(result, value);
            else Promise::resolve(result, value);
        };
        JsValue thrown;
        const bool ok = js_capture_throw([&] {
            JsValue resolve = constructor.get_property("resolve");
            if (!resolve.is_function()) js_throw_type_error("Promise resolve is not a function");
            js_iterate(js_arg(args, 0), [&](const JsValue& item) {
                JsValue next_promise = resolve.call_with_this(constructor, {item});
                subscribe_combinator_item(next_promise, [result, settle_answer](bool rejected, const JsValue& value) {
                    if (result->state != PromiseState::Pending) return;
                    settle_answer(rejected, value);
                }, result);
                return true;
            });
        }, thrown);
        if (!ok) settle_answer(true, thrown);
        return capability.promise;
    });

    realm.add_global("Promise", ctor);
}

} // namespace litebird
