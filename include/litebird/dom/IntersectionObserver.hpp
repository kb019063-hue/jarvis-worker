#pragma once

#include "litebird/dom/Element.hpp"
#include "litebird/layout/Dimensions.hpp"
#include "litebird/js/JsValue.hpp"
#include <vector>
#include <memory>
#include <string>

namespace litebird {

struct IntersectionObserverEntry {
    double time{0.0};
    Rect root_bounds;
    Rect bounding_client_rect;
    Rect intersection_rect;
    bool is_intersecting{false};
    double intersection_ratio{0.0};
    std::shared_ptr<Element> target;

    JsValue to_js_value() const;
};

struct IntersectionObserverInit {
    std::shared_ptr<Element> root{nullptr}; // nullptr means viewport
    std::string root_margin{"0px"};
    std::vector<double> thresholds{0.0};
};

class IntersectionObserver : public std::enable_shared_from_this<IntersectionObserver> {
public:
    IntersectionObserver(JsValue callback, IntersectionObserverInit init = {});
    ~IntersectionObserver() = default;

    void observe(const std::shared_ptr<Element>& target);
    void unobserve(const std::shared_ptr<Element>& target);
    void disconnect();
    std::vector<IntersectionObserverEntry> take_records();

    const std::shared_ptr<Element>& root() const { return m_init.root; }
    const std::string& root_margin() const { return m_init.root_margin; }
    const std::vector<double>& thresholds() const { return m_init.thresholds; }

    JsValue to_js_value();

    // Triggered during engine layout/rendering loop
    void check_intersections(const Rect& viewport_rect);

    static const std::vector<std::shared_ptr<IntersectionObserver>>& active_observers() {
        return s_active_observers;
    }
    static void check_all_observers(const Rect& viewport_rect);

private:
    JsValue m_callback;
    IntersectionObserverInit m_init;
    std::vector<std::weak_ptr<Element>> m_targets;
    std::vector<IntersectionObserverEntry> m_queued_records;
    std::unordered_map<Element*, double> m_previous_ratios;
    bool m_delivery_scheduled{false};

    void deliver_records();

    static std::vector<std::shared_ptr<IntersectionObserver>> s_active_observers;
};

} // namespace litebird
