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

    /// When set, called instead of the script callbacks.
    std::function<void(bool rejected, const JsValue& value)> native;
    /// Values `native` captured, for the collector.
    JsRootSet keep_alive;
};

struct PromiseData {
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
