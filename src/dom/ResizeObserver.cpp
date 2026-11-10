#include "litebird/dom/ResizeObserver.hpp"
#include "litebird/js/EventLoop.hpp"
#include <algorithm>

namespace litebird {

std::vector<std::shared_ptr<ResizeObserver>> ResizeObserver::s_active_observers;

JsValue ResizeObserverEntry::to_js_value() const {
    JsValue obj = JsValue::make_object();

    auto make_rect_obj = [](const Rect& r) {
        JsValue ro = JsValue::make_object();
        ro.set_property("x", JsValue::make_number(r.x));
        ro.set_property("y", JsValue::make_number(r.y));
        ro.set_property("width", JsValue::make_number(r.width));
        ro.set_property("height", JsValue::make_number(r.height));
        ro.set_property("top", JsValue::make_number(r.top()));
        ro.set_property("bottom", JsValue::make_number(r.bottom()));
        ro.set_property("left", JsValue::make_number(r.left()));
        ro.set_property("right", JsValue::make_number(r.right()));
        return ro;
    };

    obj.set_property("contentRect", make_rect_obj(content_rect));

    JsValue bb_size = JsValue::make_object();
    bb_size.set_property("inlineSize", JsValue::make_number(border_box_inline_size));
    bb_size.set_property("blockSize", JsValue::make_number(border_box_block_size));

    JsValue bb_arr = JsValue::make_array();
    bb_arr.array_elements().push_back(bb_size);
    obj.set_property("borderBoxSize", bb_arr);

    JsValue cb_size = JsValue::make_object();
    cb_size.set_property("inlineSize", JsValue::make_number(content_box_inline_size));
    cb_size.set_property("blockSize", JsValue::make_number(content_box_block_size));

    JsValue cb_arr = JsValue::make_array();
    cb_arr.array_elements().push_back(cb_size);
    obj.set_property("contentBoxSize", cb_arr);

    if (target) {
        obj.set_property("target", JsValue::make_dom_node(target));
    } else {
        obj.set_property("target", JsValue::make_null());
    }

    return obj;
}

ResizeObserver::ResizeObserver(JsValue callback)
    : m_callback(std::move(callback)) {}

void ResizeObserver::observe(const std::shared_ptr<Element>& target) {
    if (!target) return;
    for (const auto& w : m_targets) {
        if (w.lock() == target) return;
    }
    m_targets.push_back(target);

    auto self = shared_from_this();
    if (std::find(s_active_observers.begin(), s_active_observers.end(), self) == s_active_observers.end()) {
        s_active_observers.push_back(self);
    }
}

void ResizeObserver::unobserve(const std::shared_ptr<Element>& target) {
    if (!target) return;
    m_targets.erase(
        std::remove_if(m_targets.begin(), m_targets.end(),
            [&](const std::weak_ptr<Element>& w) { return w.lock() == target; }),
        m_targets.end()
    );
    m_previous_sizes.erase(target.get());
    if (m_targets.empty()) {
        disconnect();
    }
}

void ResizeObserver::disconnect() {
    m_targets.clear();
    m_previous_sizes.clear();
    m_queued_records.clear();

    auto self = shared_from_this();
    s_active_observers.erase(
        std::remove(s_active_observers.begin(), s_active_observers.end(), self),
        s_active_observers.end()
    );
}

void ResizeObserver::check_resizes() {
    bool new_records = false;

    for (auto it = m_targets.begin(); it != m_targets.end();) {
        auto target = it->lock();
        if (!target) {
            it = m_targets.erase(it);
            continue;
        }
        ++it;

        Rect rect = target->bounding_box();
        auto prev_it = m_previous_sizes.find(target.get());
        bool size_changed = false;

        if (prev_it == m_previous_sizes.end()) {
            size_changed = true;
            m_previous_sizes[target.get()] = {rect.width, rect.height};
        } else {
            float prev_w = prev_it->second.first;
            float prev_h = prev_it->second.second;
            if (std::abs(rect.width - prev_w) > 0.001f || std::abs(rect.height - prev_h) > 0.001f) {
                size_changed = true;
                prev_it->second = {rect.width, rect.height};
            }
        }

        if (size_changed) {
            ResizeObserverEntry entry;
            entry.target = target;
            entry.content_rect = rect;
            entry.border_box_inline_size = rect.width;
            entry.border_box_block_size = rect.height;
            entry.content_box_inline_size = rect.width;
            entry.content_box_block_size = rect.height;

            m_queued_records.push_back(entry);
            new_records = true;
        }
    }

    if (new_records && !m_delivery_scheduled) {
        m_delivery_scheduled = true;
        auto self = shared_from_this();
        EventLoop::default_loop().post_microtask([self]() {
            self->deliver_records();
        });
    }
}

void ResizeObserver::deliver_records() {
    m_delivery_scheduled = false;
    if (m_queued_records.empty()) return;

    auto records = std::move(m_queued_records);
    m_queued_records.clear();

    JsValue records_arr = JsValue::make_array();
    for (const auto& rec : records) {
        records_arr.array_elements().push_back(rec.to_js_value());
    }

    if (m_callback.is_callable()) {
        m_callback.call({records_arr, to_js_value()});
    }
}

void ResizeObserver::check_all_observers() {
    auto copy = s_active_observers;
    for (const auto& obs : copy) {
        obs->check_resizes();
    }
}

JsValue ResizeObserver::to_js_value() {
    JsValue obj = JsValue::make_object();
    auto self = shared_from_this();

    obj.set_property("observe", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (!args.empty() && args[0].dom_node_value() && args[0].dom_node_value()->is_element()) {
            self->observe(std::static_pointer_cast<Element>(args[0].dom_node_value()));
        }
        return JsValue::make_undefined();
    }));

    obj.set_property("unobserve", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (!args.empty() && args[0].dom_node_value() && args[0].dom_node_value()->is_element()) {
            self->unobserve(std::static_pointer_cast<Element>(args[0].dom_node_value()));
        }
        return JsValue::make_undefined();
    }));

    obj.set_property("disconnect", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        self->disconnect();
        return JsValue::make_undefined();
    }));

    return obj;
}

} // namespace litebird
