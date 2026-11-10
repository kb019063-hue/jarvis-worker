#include "litebird/dom/MutationObserver.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Heap.hpp"
#include <algorithm>

namespace litebird {

std::vector<std::weak_ptr<MutationObserver>> MutationObserver::s_active_observers;

JsValue MutationRecord::to_js_value() const {
    JsValue obj = JsValue::make_object();
    obj.set_property("type", JsValue::make_string(type));
    if (target) {
        obj.set_property("target", JsValue::make_dom_node(target));
    } else {
        obj.set_property("target", JsValue::make_null());
    }

    JsValue added_arr = JsValue::make_array();
    for (const auto& node : added_nodes) {
        added_arr.array_elements().push_back(JsValue::make_dom_node(node));
    }
    obj.set_property("addedNodes", added_arr);

    JsValue removed_arr = JsValue::make_array();
    for (const auto& node : removed_nodes) {
        removed_arr.array_elements().push_back(JsValue::make_dom_node(node));
    }
    obj.set_property("removedNodes", removed_arr);

    if (previous_sibling) {
        obj.set_property("previousSibling", JsValue::make_dom_node(previous_sibling));
    } else {
        obj.set_property("previousSibling", JsValue::make_null());
    }

    if (next_sibling) {
        obj.set_property("nextSibling", JsValue::make_dom_node(next_sibling));
    } else {
        obj.set_property("nextSibling", JsValue::make_null());
    }

    if (!attribute_name.empty()) {
        obj.set_property("attributeName", JsValue::make_string(attribute_name));
    } else {
        obj.set_property("attributeName", JsValue::make_null());
    }

    if (!old_value.empty()) {
        obj.set_property("oldValue", JsValue::make_string(old_value));
    } else {
        obj.set_property("oldValue", JsValue::make_null());
    }

    return obj;
}

MutationObserver::MutationObserver(JsValue callback)
    : m_callback(std::move(callback)) {}

MutationObserver::~MutationObserver() {
    disconnect();
    unregister_observer(this);
}

void MutationObserver::register_observer(const std::shared_ptr<MutationObserver>& obs) {
    for (auto it = s_active_observers.begin(); it != s_active_observers.end();) {
        if (it->expired()) it = s_active_observers.erase(it);
        else ++it;
    }
    s_active_observers.push_back(obs);
}

void MutationObserver::unregister_observer(MutationObserver* obs) {
    for (auto it = s_active_observers.begin(); it != s_active_observers.end();) {
        auto locked = it->lock();
        if (!locked || locked.get() == obs) {
            it = s_active_observers.erase(it);
        } else {
            ++it;
        }
    }
}

void MutationObserver::observe(std::shared_ptr<Node> target, const MutationObserverInit& options) {
    if (!target) return;

    register_observer(shared_from_this());

    for (auto& obs : m_observations) {
        if (obs.target.lock() == target) {
            obs.options = options;
            return;
        }
    }

    m_observations.push_back({target, options});
}

void MutationObserver::disconnect() {
    m_observations.clear();
    m_pending_records.clear();
}

std::vector<MutationRecord> MutationObserver::take_records() {
    std::vector<MutationRecord> result = std::move(m_pending_records);
    m_pending_records.clear();
    return result;
}

void MutationObserver::queue_record(MutationRecord record) {
    m_pending_records.push_back(std::move(record));
    if (!m_microtask_queued) {
        m_microtask_queued = true;
        auto self = shared_from_this();
        EventLoop::default_loop().post_microtask([self]() {
            self->deliver();
        });
    }
}

void MutationObserver::deliver() {
    m_microtask_queued = false;
    if (m_pending_records.empty()) return;

    auto records = std::move(m_pending_records);
    m_pending_records.clear();

    JsValue records_arr = JsValue::make_array();
    for (const auto& r : records) {
        records_arr.array_elements().push_back(r.to_js_value());
    }

    if (m_callback.is_callable()) {
        m_callback.call({records_arr, to_js_value()});
    }
}

JsValue MutationObserver::to_js_value() {
    JsValue obj = JsValue::make_object();
    auto self = shared_from_this();

    obj.set_property("observe", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_undefined();
        std::shared_ptr<Node> target = args[0].dom_node_value();
        if (!target) return JsValue::make_undefined();

        MutationObserverInit init;
        if (args.size() > 1 && (args[1].type == JsType::Object)) {
            const auto& opt = args[1];
            init.child_list = opt.get_property("childList").is_truthy();
            init.attributes = opt.get_property("attributes").is_truthy();
            init.character_data = opt.get_property("characterData").is_truthy();
            init.subtree = opt.get_property("subtree").is_truthy();
            init.attribute_old_value = opt.get_property("attributeOldValue").is_truthy();
            init.character_data_old_value = opt.get_property("characterDataOldValue").is_truthy();

            auto filter_val = opt.get_property("attributeFilter");
            if (filter_val.is_array()) {
                for (const auto& item : filter_val.array_elements()) {
                    init.attribute_filter.push_back(item.to_string());
                }
            }
        }
        self->observe(target, init);
        return JsValue::make_undefined();
    }));

    obj.set_property("disconnect", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        self->disconnect();
        return JsValue::make_undefined();
    }));

    obj.set_property("takeRecords", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        auto records = self->take_records();
        JsValue arr = JsValue::make_array();
        for (const auto& r : records) {
            arr.array_elements().push_back(r.to_js_value());
        }
        return arr;
    }));

    return obj;
}

static bool is_descendant_of(const std::shared_ptr<Node>& node, const std::shared_ptr<Node>& ancestor) {
    if (!node || !ancestor) return false;
    auto curr = node->parent();
    while (curr) {
        if (curr == ancestor) return true;
        curr = curr->parent();
    }
    return false;
}

void MutationObserver::notify_child_list(
    const std::shared_ptr<Node>& target,
    std::vector<std::shared_ptr<Node>> added,
    std::vector<std::shared_ptr<Node>> removed,
    std::shared_ptr<Node> prev,
    std::shared_ptr<Node> next
) {
    for (const auto& weak_obs : s_active_observers) {
        auto obs = weak_obs.lock();
        if (!obs) continue;

        for (const auto& entry : obs->m_observations) {
            auto observed_node = entry.target.lock();
            if (!observed_node) continue;

            bool matches = false;
            if (observed_node == target) {
                matches = entry.options.child_list;
            } else if (entry.options.subtree && is_descendant_of(target, observed_node)) {
                matches = entry.options.child_list;
            }

            if (matches) {
                MutationRecord record;
                record.type = "childList";
                record.target = target;
                record.added_nodes = added;
                record.removed_nodes = removed;
                record.previous_sibling = prev;
                record.next_sibling = next;
                obs->queue_record(record);
                break;
            }
        }
    }
}

void MutationObserver::notify_attributes(
    const std::shared_ptr<Element>& target,
    const std::string& attr_name,
    const std::string& old_value
) {
    for (const auto& weak_obs : s_active_observers) {
        auto obs = weak_obs.lock();
        if (!obs) continue;

        for (const auto& entry : obs->m_observations) {
            auto observed_node = entry.target.lock();
            if (!observed_node) continue;

            bool matches = false;
            if (observed_node == target) {
                matches = entry.options.attributes;
            } else if (entry.options.subtree && is_descendant_of(target, observed_node)) {
                matches = entry.options.attributes;
            }

            if (matches) {
                if (!entry.options.attribute_filter.empty()) {
                    auto it = std::find(entry.options.attribute_filter.begin(),
                                        entry.options.attribute_filter.end(), attr_name);
                    if (it == entry.options.attribute_filter.end()) {
                        continue;
                    }
                }

                MutationRecord record;
                record.type = "attributes";
                record.target = target;
                record.attribute_name = attr_name;
                if (entry.options.attribute_old_value) {
                    record.old_value = old_value;
                }
                obs->queue_record(record);
                break;
            }
        }
    }
}

void MutationObserver::notify_character_data(
    const std::shared_ptr<Node>& target,
    const std::string& old_value
) {
    for (const auto& weak_obs : s_active_observers) {
        auto obs = weak_obs.lock();
        if (!obs) continue;

        for (const auto& entry : obs->m_observations) {
            auto observed_node = entry.target.lock();
            if (!observed_node) continue;

            bool matches = false;
            if (observed_node == target) {
                matches = entry.options.character_data;
            } else if (entry.options.subtree && is_descendant_of(target, observed_node)) {
                matches = entry.options.character_data;
            }

            if (matches) {
                MutationRecord record;
                record.type = "characterData";
                record.target = target;
                if (entry.options.character_data_old_value) {
                    record.old_value = old_value;
                }
                obs->queue_record(record);
                break;
            }
        }
    }
}

} // namespace litebird
