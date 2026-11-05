#pragma once

#include "litebird/js/JsValue.hpp"
#include <memory>
#include <vector>

namespace litebird {

enum class PromiseState {
    Pending,
    Fulfilled,
    Rejected
};

struct PromiseHandler {
    JsValue on_fulfilled;
    JsValue on_rejected;
    std::shared_ptr<struct PromiseData> next_promise;
};

struct PromiseData {
    PromiseState state{PromiseState::Pending};
    JsValue result;
    std::vector<PromiseHandler> handlers;
};

class Promise {
public:
    static JsValue create_constructor();
    static JsValue make_resolved(JsValue val);
    static JsValue make_rejected(JsValue reason);
    static JsValue create_promise_instance(std::shared_ptr<PromiseData> data);

    static void resolve(const std::shared_ptr<PromiseData>& data, JsValue val);
    static void reject(const std::shared_ptr<PromiseData>& data, JsValue reason);
};

} // namespace litebird
