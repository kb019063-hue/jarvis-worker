#pragma once

#include "litebird/js/JsValue.hpp"
#include "litebird/js/EventLoop.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace litebird {

struct JsIntrinsics;

enum class PromiseState {
    Pending,
    Fulfilled,
    Rejected
};

/// One reaction registered on a promise: either the script callbacks passed to
/// then(), or an engine-internal continuation (await, Promise.all, ...).
struct PromiseHandler {
    JsValue on_fulfilled;
    JsValue on_rejected;
    std::shared_ptr<struct PromiseData> next_promise;
    /// The capability the reaction settles, when the constructor that made the
    /// derived promise was not %Promise%: its own resolve and reject, which the
    /// engine calls instead of settling `next_promise` itself.
    JsValue resolve;
    /// The other half of that pair; a rejection goes through this one.
    JsValue reject;

    /// When set, called instead of the script callbacks.
    std::function<void(bool rejected, const JsValue& value)> native;
    /// Values `native` captured, for the collector.
    JsRootSet keep_alive;
};

struct PromiseData {
    PromiseData() = default;
    /// then() links a promise to the one it returned, so a long pending chain
    /// is a linked list; it is released in a loop (common/ReleaseQueue.hpp).
    ~PromiseData();

    PromiseState state{PromiseState::Pending};
    JsValue result;
    std::vector<PromiseHandler> handlers;
    /// A reaction was attached at some point; an unhandled rejection is only
    /// reported for promises where this stays false.
    bool handled{false};
    /// Resolved with a thenable: the outcome now belongs to that thenable and
    /// further resolve()/reject() calls are ignored.
    bool locked{false};
};

/// A PromiseCapability record (ECMA-262 §27.2.1.1): the object a constructor
/// made, and the pair of functions that settle it. `resolve` and `reject` are
/// undefined until the GetCapabilitiesExecutor has been called with them.
struct PromiseCapability {
    JsValue promise;
    JsValue resolve;
    JsValue reject;
};

class Promise {
public:
    /// The Promise constructor of the current realm.
    static JsValue create_constructor();
    static JsValue make_resolved(JsValue val);
    static JsValue make_rejected(JsValue reason);
    /// A script-visible promise object for `data`.
    static JsValue create_promise_instance(std::shared_ptr<PromiseData> data);

    /// The promise's resolve function: adopts thenables, otherwise fulfils.
    static void resolve(const std::shared_ptr<PromiseData>& data, JsValue val);
    static void reject(const std::shared_ptr<PromiseData>& data, JsValue reason);

    /// IsPromise(value) (§27.2.1.6): an object with the promise state.
    static bool is_promise(const JsValue& value);

    /// NewPromiseCapability(C) (§27.2.1.5). `C` must be a constructor; the
    /// capability's `resolve` and `reject` are its own if it did not call the
    /// executor, and the object it answered is its `[[Promise]]`.
    static PromiseCapability new_capability(const JsValue& constructor);
    /// PromiseResolve(C, x) (§27.2.1.4): for %Promise%, `x` itself when it is
    /// a promise; for any other constructor, NewPromiseCapability(C) and the
    /// capability's resolve called with `x`.
    static JsValue resolve_from(const JsValue& constructor, const JsValue& value);

    /// The capability a derived promise is settled through: one whose
    /// `[[Promise]]` is `promise`, which is an object of `C.prototype`.
    /// Undefined values where the object is not a promise the engine made.
    static PromiseCapability capability_for(const JsValue& promise);

    /// The object a response to `promise` is: `NewPromiseCapability(C)`, with
    /// a capability over the engine's own state when `C` is %Promise% (the
    /// common case, and the only one that can be a native promise).
    static PromiseCapability capability_for(const std::shared_ptr<PromiseData>& data,
                                            const JsValue& constructor);

    /// Promise.prototype.then's result: `capability`'s promise, settled by the
    /// engine through the capability's own functions where it can be, and fed
    /// by a native subscription where the promise `data` is the engine's.
    static JsValue then_into(const JsValue& promise, const std::shared_ptr<PromiseData>& data,
                             JsValue on_fulfilled, JsValue on_rejected, PromiseCapability capability);

    /// PromiseResolve: `value` itself when it is a promise, otherwise a new
    /// promise resolved with it.
    static std::shared_ptr<PromiseData> to_promise(const JsValue& value);

    /// then(): registers script callbacks and returns the derived promise.
    static std::shared_ptr<PromiseData> then(const std::shared_ptr<PromiseData>& data,
                                             JsValue on_fulfilled, JsValue on_rejected);

    /// Registers an engine-internal reaction. Like any reaction it runs as a
    /// microtask, never synchronously. `keep_alive` lists the values the
    /// reaction captured.
    static void subscribe(const std::shared_ptr<PromiseData>& data,
                          std::function<void(bool rejected, const JsValue& value)> reaction,
                          JsRootSet keep_alive = nullptr);
};

/// Installs Promise and Promise.prototype into a realm.
void install_promise_builtins(JsIntrinsics& realm);

} // namespace litebird
