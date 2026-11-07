#pragma once

#include "litebird/dom/Element.hpp"
#include "litebird/layout/Dimensions.hpp"
#include "litebird/js/JsValue.hpp"
#include <vector>
#include <memory>
#include <unordered_map>

namespace litebird {

struct ResizeObserverEntry {
    std::shared_ptr<Element> target;
    Rect content_rect;
    float border_box_inline_size{0.0f}; // width
    float border_box_block_size{0.0f};  // height
    float content_box_inline_size{0.0f};
    float content_box_block_size{0.0f};

    JsValue to_js_value() const;
};

class ResizeObserver : public std::enable_shared_from_this<ResizeObserver> {
public:
    ResizeObserver(JsValue callback);
    ~ResizeObserver() = default;

    void observe(const std::shared_ptr<Element>& target);
    void unobserve(const std::shared_ptr<Element>& target);
    void disconnect();

    JsValue to_js_value();

    void check_resizes();

    static const std::vector<std::shared_ptr<ResizeObserver>>& active_observers() {
        return s_active_observers;
    }
    static void check_all_observers();

private:
    JsValue m_callback;
    std::vector<std::weak_ptr<Element>> m_targets;
    std::vector<ResizeObserverEntry> m_queued_records;
    std::unordered_map<Element*, std::pair<float, float>> m_previous_sizes;
    bool m_delivery_scheduled{false};

    void deliver_records();

    static std::vector<std::shared_ptr<ResizeObserver>> s_active_observers;
};

} // namespace litebird
