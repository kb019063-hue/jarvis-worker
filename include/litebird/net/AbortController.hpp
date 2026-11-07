#pragma once

#include "litebird/dom/Event.hpp"
#include "litebird/js/JsValue.hpp"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>

namespace litebird {

class AbortSignal : public std::enable_shared_from_this<AbortSignal> {
public:
    using EventCallback = std::function<void(const std::shared_ptr<Event>&)>;

    AbortSignal();

    bool aborted() const { return m_aborted; }
    const JsValue& reason() const { return m_reason; }

    void signal_abort(JsValue reason = JsValue::make_undefined());
    void throw_if_aborted() const;

    void add_event_listener(const std::string& type, EventCallback callback);
    void remove_event_listener(const std::string& type);
    bool dispatch_event(const std::shared_ptr<Event>& event);

    JsValue onabort() const { return m_onabort; }
    void set_onabort(JsValue cb) { m_onabort = std::move(cb); }

    JsValue to_js_value();

    static std::shared_ptr<AbortSignal> abort(JsValue reason = JsValue::make_undefined()) {
        return make_aborted(reason);
    }
    static std::shared_ptr<AbortSignal> timeout(uint32_t delay_ms) {
        return make_timeout(delay_ms);
    }
    static std::shared_ptr<AbortSignal> make_aborted(JsValue reason = JsValue::make_undefined());
    static std::shared_ptr<AbortSignal> make_timeout(uint32_t delay_ms);

private:
    bool m_aborted{false};
    JsValue m_reason;
    JsValue m_onabort;
    std::unordered_map<std::string, std::vector<EventCallback>> m_listeners;
};

class AbortController : public std::enable_shared_from_this<AbortController> {
public:
    AbortController();

    std::shared_ptr<AbortSignal> signal() const { return m_signal; }
    void abort(JsValue reason = JsValue::make_undefined());

    JsValue to_js_value();

private:
    std::shared_ptr<AbortSignal> m_signal;
};

} // namespace litebird
