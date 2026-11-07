#include "litebird/dom/IntersectionObserver.hpp"
#include "litebird/js/EventLoop.hpp"
#include <algorithm>
#include <sstream>

namespace litebird {

std::vector<std::shared_ptr<IntersectionObserver>> IntersectionObserver::s_active_observers;

JsValue IntersectionObserverEntry::to_js_value() const {
    JsValue obj = JsValue::make_object();
    obj.set_property("time", JsValue::make_number(time));
    obj.set_property("isIntersecting", JsValue::make_boolean(is_intersecting));
    obj.set_property("intersectionRatio", JsValue::make_number(intersection_ratio));

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

    obj.set_property("rootBounds", make_rect_obj(root_bounds));
    obj.set_property("boundingClientRect", make_rect_obj(bounding_client_rect));
    obj.set_property("intersectionRect", make_rect_obj(intersection_rect));
    if (target) {
        obj.set_property("target", JsValue::make_dom_node(target));
    } else {
        obj.set_property("target", JsValue::make_null());
    }

    return obj;
}

IntersectionObserver::IntersectionObserver(JsValue callback, IntersectionObserverInit init)
    : m_callback(std::move(callback)), m_init(std::move(init)) {
    if (m_init.thresholds.empty()) {
        m_init.thresholds.push_back(0.0);
    }
    std::sort(m_init.thresholds.begin(), m_init.thresholds.end());
}

void IntersectionObserver::observe(const std::shared_ptr<Element>& target) {
    if (!target) return;
    for (const auto& w : m_targets) {
        if (w.lock() == target) return;
    }
    m_targets.push_back(target);

    // Add to static active list if not already present
    auto self = shared_from_this();
    if (std::find(s_active_observers.begin(), s_active_observers.end(), self) == s_active_observers.end()) {
        s_active_observers.push_back(self);
    }
}

void IntersectionObserver::unobserve(const std::shared_ptr<Element>& target) {
    if (!target) return;
    m_targets.erase(
        std::remove_if(m_targets.begin(), m_targets.end(),
            [&](const std::weak_ptr<Element>& w) { return w.lock() == target; }),
        m_targets.end()
    );
    m_previous_ratios.erase(target.get());
    if (m_targets.empty()) {
        disconnect();
    }
}

void IntersectionObserver::disconnect() {
    m_targets.clear();
    m_previous_ratios.clear();
    m_queued_records.clear();

    auto self = shared_from_this();
    s_active_observers.erase(
        std::remove(s_active_observers.begin(), s_active_observers.end(), self),
        s_active_observers.end()
    );
}

std::vector<IntersectionObserverEntry> IntersectionObserver::take_records() {
    auto res = std::move(m_queued_records);
    m_queued_records.clear();
    return res;
}

static EdgeSizes parse_root_margin(const std::string& margin_str) {
    EdgeSizes margins(0.0f);
    std::stringstream ss(margin_str);
    std::vector<float> values;
    std::string token;
    while (ss >> token) {
        try {
            values.push_back(std::stof(token));
        } catch (...) {
            values.push_back(0.0f);
        }
    }
    if (values.size() == 1) {
        margins = EdgeSizes(values[0]);
    } else if (values.size() == 2) {
        margins.top = margins.bottom = values[0];
        margins.left = margins.right = values[1];
    } else if (values.size() == 3) {
        margins.top = values[0];
        margins.left = margins.right = values[1];
        margins.bottom = values[2];
    } else if (values.size() >= 4) {
        margins.top = values[0];
        margins.right = values[1];
        margins.bottom = values[2];
        margins.left = values[3];
    }
    return margins;
}

void IntersectionObserver::check_intersections(const Rect& viewport_rect) {
    Rect root_rect = viewport_rect;
    if (m_init.root) {
        root_rect = m_init.root->bounding_box();
    }

    EdgeSizes margins = parse_root_margin(m_init.root_margin);
    root_rect.x -= margins.left;
    root_rect.y -= margins.top;
    root_rect.width += margins.left + margins.right;
    root_rect.height += margins.top + margins.bottom;

    bool new_records = false;

    for (auto it = m_targets.begin(); it != m_targets.end();) {
        auto target = it->lock();
        if (!target) {
            it = m_targets.erase(it);
            continue;
        }
        ++it;

        Rect target_rect = target->bounding_box();
        // If element width or height is 0, estimate from style if available
        if (target_rect.width <= 0.0f && target_rect.height <= 0.0f) {
            target_rect.width = 100.0f;
            target_rect.height = 30.0f;
        }

        float inter_x = std::max(target_rect.x, root_rect.x);
        float inter_y = std::max(target_rect.y, root_rect.y);
        float inter_right = std::min(target_rect.right(), root_rect.right());
        float inter_bottom = std::min(target_rect.bottom(), root_rect.bottom());

        float inter_w = std::max(0.0f, inter_right - inter_x);
        float inter_h = std::max(0.0f, inter_bottom - inter_y);
        Rect inter_rect(inter_x, inter_y, inter_w, inter_h);

        float target_area = target_rect.width * target_rect.height;
        float inter_area = inter_w * inter_h;
        double ratio = (target_area > 0.0f) ? static_cast<double>(inter_area / target_area) : 0.0;
        ratio = std::clamp(ratio, 0.0, 1.0);

        bool is_intersecting = (inter_w > 0.0f && inter_h > 0.0f);

        auto prev_it = m_previous_ratios.find(target.get());
        bool first_check = (prev_it == m_previous_ratios.end());
        double prev_ratio = first_check ? -1.0 : prev_it->second;

        bool threshold_crossed = first_check;
        if (!first_check) {
            for (double th : m_init.thresholds) {
                if ((prev_ratio < th && ratio >= th) || (prev_ratio >= th && ratio < th)) {
                    threshold_crossed = true;
                    break;
                }
            }
        }

        if (threshold_crossed) {
            m_previous_ratios[target.get()] = ratio;

            IntersectionObserverEntry entry;
            entry.time = 0.0; // In standard implementations, high-res timestamp
            entry.root_bounds = root_rect;
            entry.bounding_client_rect = target_rect;
            entry.intersection_rect = inter_rect;
            entry.is_intersecting = is_intersecting;
            entry.intersection_ratio = ratio;
            entry.target = target;

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

void IntersectionObserver::deliver_records() {
    m_delivery_scheduled = false;
    if (m_queued_records.empty()) return;

    auto records = take_records();
    JsValue records_arr = JsValue::make_array();
    for (const auto& rec : records) {
        records_arr.array_elements().push_back(rec.to_js_value());
    }

    if (m_callback.is_callable()) {
        m_callback.call({records_arr, to_js_value()});
    }
}

void IntersectionObserver::check_all_observers(const Rect& viewport_rect) {
    auto copy = s_active_observers;
    for (const auto& obs : copy) {
        obs->check_intersections(viewport_rect);
    }
}

JsValue IntersectionObserver::to_js_value() {
    JsValue obj = JsValue::make_object();
    auto self = shared_from_this();

    obj.set_property("observe", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (!args.empty() && args[0].dom_node && args[0].dom_node->is_element()) {
            self->observe(std::static_pointer_cast<Element>(args[0].dom_node));
        }
        return JsValue::make_undefined();
    }));

    obj.set_property("unobserve", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (!args.empty() && args[0].dom_node && args[0].dom_node->is_element()) {
            self->unobserve(std::static_pointer_cast<Element>(args[0].dom_node));
        }
        return JsValue::make_undefined();
    }));

    obj.set_property("disconnect", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        self->disconnect();
        return JsValue::make_undefined();
    }));

    obj.set_property("takeRecords", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        auto recs = self->take_records();
        JsValue arr = JsValue::make_array();
        for (const auto& r : recs) {
            arr.array_elements().push_back(r.to_js_value());
        }
        return arr;
    }));

    if (m_init.root) {
        obj.set_property("root", JsValue::make_dom_node(m_init.root));
    } else {
        obj.set_property("root", JsValue::make_null());
    }
    obj.set_property("rootMargin", JsValue::make_string(m_init.root_margin));

    JsValue th_arr = JsValue::make_array();
    for (double th : m_init.thresholds) {
        th_arr.array_elements().push_back(JsValue::make_number(th));
    }
    obj.set_property("thresholds", th_arr);

    return obj;
}

} // namespace litebird
