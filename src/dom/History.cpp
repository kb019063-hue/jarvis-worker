#include "litebird/dom/History.hpp"
#include <algorithm>

namespace litebird {

History::History() {
    clear();
}

void History::clear() {
    m_entries.clear();
    m_entries.push_back({JsValue::make_undefined(), "", ""});
    m_current_index = 0;
}

JsValue History::state() const {
    if (m_current_index < m_entries.size()) {
        return m_entries[m_current_index].state;
    }
    return JsValue::make_undefined();
}

std::string History::current_url() const {
    if (m_current_index < m_entries.size()) {
        return m_entries[m_current_index].url;
    }
    return "";
}

void History::push_state(JsValue state, const std::string& title, const std::string& url) {
    if (m_current_index + 1 < m_entries.size()) {
        m_entries.erase(m_entries.begin() + m_current_index + 1, m_entries.end());
    }
    m_entries.push_back({std::move(state), title, url});
    m_current_index = m_entries.size() - 1;

    if (m_on_navigate) {
        m_on_navigate(url, m_entries[m_current_index].state);
    }
}

void History::replace_state(JsValue state, const std::string& title, const std::string& url) {
    if (m_entries.empty()) {
        m_entries.push_back({std::move(state), title, url});
        m_current_index = 0;
    } else {
        m_entries[m_current_index] = {std::move(state), title, url};
    }

    if (m_on_navigate) {
        m_on_navigate(url, m_entries[m_current_index].state);
    }
}

void History::back() {
    go(-1);
}

void History::forward() {
    go(1);
}

void History::go(int delta) {
    if (m_entries.empty()) return;
    int new_idx = static_cast<int>(m_current_index) + delta;
    new_idx = std::clamp(new_idx, 0, static_cast<int>(m_entries.size() - 1));

    if (static_cast<size_t>(new_idx) != m_current_index) {
        m_current_index = static_cast<size_t>(new_idx);
        const auto& entry = m_entries[m_current_index];

        if (m_on_navigate) {
            m_on_navigate(entry.url, entry.state);
        }
        if (m_popstate_dispatcher) {
            m_popstate_dispatcher(entry.state);
        }
    }
}

JsValue History::to_js_value() {
    JsValue obj = JsValue::make_object();

    obj.set_property("pushState", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        JsValue st = (args.size() > 0) ? args[0] : JsValue::make_undefined();
        std::string title = (args.size() > 1) ? args[1].to_string() : "";
        std::string url = (args.size() > 2) ? args[2].to_string() : "";
        push_state(st, title, url);
        return JsValue::make_undefined();
    }));

    obj.set_property("replaceState", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        JsValue st = (args.size() > 0) ? args[0] : JsValue::make_undefined();
        std::string title = (args.size() > 1) ? args[1].to_string() : "";
        std::string url = (args.size() > 2) ? args[2].to_string() : "";
        replace_state(st, title, url);
        return JsValue::make_undefined();
    }));

    obj.set_property("back", JsValue::make_native_function([this](const std::vector<JsValue>&) {
        back();
        return JsValue::make_undefined();
    }));

    obj.set_property("forward", JsValue::make_native_function([this](const std::vector<JsValue>&) {
        forward();
        return JsValue::make_undefined();
    }));

    obj.set_property("go", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        int delta = (args.size() > 0) ? static_cast<int>(args[0].to_number()) : 0;
        go(delta);
        return JsValue::make_undefined();
    }));

    // Dynamic getters via internal properties or methods
    obj.set_property("length", JsValue::make_number(static_cast<double>(length())));
    obj.set_property("state", state());

    return obj;
}

} // namespace litebird
