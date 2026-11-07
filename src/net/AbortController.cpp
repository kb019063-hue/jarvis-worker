#include "litebird/net/AbortController.hpp"
#include "litebird/js/EventLoop.hpp"

namespace litebird {

AbortSignal::AbortSignal()
    : m_reason(JsValue::make_undefined()) {}

void AbortSignal::signal_abort(JsValue reason) {
    if (m_aborted) return;
    m_aborted = true;
    if (reason.is_undefined()) {
        JsValue err = JsValue::make_object();
        err.set_property("name", JsValue::make_string("AbortError"));
        err.set_property("message", JsValue::make_string("This operation was aborted"));
        m_reason = err;
    } else {
        m_reason = std::move(reason);
    }

    JsValue ev_obj = JsValue::make_object();
    ev_obj.set_property("type", JsValue::make_string("abort"));
    ev_obj.set_property("bubbles", JsValue::make_boolean(false));
    ev_obj.set_property("cancelable", JsValue::make_boolean(false));

    if (m_onabort.is_callable()) {
        m_onabort.call({ev_obj});
    }

    auto ev = std::make_shared<Event>("abort", false, false);
    dispatch_event(ev);
}

void AbortSignal::add_event_listener(const std::string& type, EventCallback callback) {
    m_listeners[type].push_back(std::move(callback));
}

void AbortSignal::remove_event_listener(const std::string& type) {
    m_listeners.erase(type);
}

bool AbortSignal::dispatch_event(const std::shared_ptr<Event>& event) {
    if (!event) return false;
    auto it = m_listeners.find(event->type());
    if (it != m_listeners.end()) {
        for (const auto& cb : it->second) {
            cb(event);
        }
    }
    return true;
}

void AbortSignal::throw_if_aborted() const {
    if (m_aborted) {
        throw std::runtime_error(m_reason.to_string());
    }
}

std::shared_ptr<AbortSignal> AbortSignal::make_aborted(JsValue reason) {
    auto sig = std::make_shared<AbortSignal>();
    sig->signal_abort(std::move(reason));
    return sig;
}

std::shared_ptr<AbortSignal> AbortSignal::make_timeout(uint32_t delay_ms) {
    auto sig = std::make_shared<AbortSignal>();
    EventLoop::default_loop().set_timeout([sig]() {
        JsValue err = JsValue::make_object();
        err.set_property("name", JsValue::make_string("TimeoutError"));
        err.set_property("message", JsValue::make_string("The operation timed out"));
        sig->signal_abort(err);
    }, delay_ms);
    return sig;
}

JsValue AbortSignal::to_js_value() {
    JsValue obj = JsValue::make_object();
    auto self = shared_from_this();

    obj.set_property("__get_aborted__", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        return JsValue::make_boolean(self->aborted());
    }));
    obj.set_property("__get_reason__", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        return self->reason();
    }));

    obj.set_property("addEventListener", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (args.size() >= 2 && args[0].type == JsType::String && args[1].is_callable()) {
            std::string type = args[0].to_string();
            JsValue listener = args[1];
            self->add_event_listener(type, [listener](const std::shared_ptr<Event>& ev) {
                JsValue eo = JsValue::make_object();
                eo.set_property("type", JsValue::make_string(ev->type()));
                listener.call({eo});
            });
        }
        return JsValue::make_undefined();
    }));

    obj.set_property("removeEventListener", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (!args.empty()) {
            self->remove_event_listener(args[0].to_string());
        }
        return JsValue::make_undefined();
    }));

    obj.set_property("throwIfAborted", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        self->throw_if_aborted();
        return JsValue::make_undefined();
    }));

    return obj;
}

AbortController::AbortController()
    : m_signal(std::make_shared<AbortSignal>()) {}

void AbortController::abort(JsValue reason) {
    if (m_signal) {
        m_signal->signal_abort(std::move(reason));
    }
}

JsValue AbortController::to_js_value() {
    JsValue obj = JsValue::make_object();
    auto self = shared_from_this();

    if (m_signal) {
        obj.set_property("signal", m_signal->to_js_value());
    }

    obj.set_property("abort", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        JsValue r = args.empty() ? JsValue::make_undefined() : args[0];
        self->abort(r);
        return JsValue::make_undefined();
    }));

    return obj;
}

} // namespace litebird
