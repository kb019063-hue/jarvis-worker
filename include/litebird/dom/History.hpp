#pragma once

#include "litebird/js/JsValue.hpp"
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace litebird {

struct HistoryEntry {
    JsValue state;
    std::string title;
    std::string url;
};

class History {
public:
    using NavigationCallback = std::function<void(const std::string& new_url, const JsValue& state)>;

    History();

    size_t length() const { return m_entries.size(); }
    JsValue state() const;
    std::string current_url() const;

    void push_state(JsValue state, const std::string& title, const std::string& url);
    void replace_state(JsValue state, const std::string& title, const std::string& url);
    void back();
    void forward();
    void go(int delta = 0);

    void set_navigation_callback(NavigationCallback cb) { m_on_navigate = std::move(cb); }
    void set_popstate_dispatcher(std::function<void(const JsValue& state)> dispatcher) {
        m_popstate_dispatcher = std::move(dispatcher);
    }

    JsValue to_js_value();

    void clear();

private:
    std::vector<HistoryEntry> m_entries;
    size_t m_current_index{0};
    NavigationCallback m_on_navigate;
    std::function<void(const JsValue& state)> m_popstate_dispatcher;
};

} // namespace litebird
