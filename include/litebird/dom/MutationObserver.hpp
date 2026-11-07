#pragma once

#include "litebird/dom/Node.hpp"
#include "litebird/js/JsValue.hpp"
#include <string>
#include <vector>
#include <memory>

namespace litebird {

class Element;

struct MutationRecord {
    std::string type; // "childList", "attributes", "characterData"
    std::shared_ptr<Node> target;
    std::vector<std::shared_ptr<Node>> added_nodes;
    std::vector<std::shared_ptr<Node>> removed_nodes;
    std::shared_ptr<Node> previous_sibling;
    std::shared_ptr<Node> next_sibling;
    std::string attribute_name;
    std::string old_value;

    JsValue to_js_value() const;
};

struct MutationObserverInit {
    bool child_list{false};
    bool attributes{false};
    bool character_data{false};
    bool subtree{false};
    bool attribute_old_value{false};
    bool character_data_old_value{false};
    std::vector<std::string> attribute_filter;
};

class MutationObserver : public std::enable_shared_from_this<MutationObserver> {
public:
    explicit MutationObserver(JsValue callback);
    ~MutationObserver();

    void observe(std::shared_ptr<Node> target, const MutationObserverInit& options);
    void disconnect();
    std::vector<MutationRecord> take_records();

    void queue_record(MutationRecord record);
    void deliver();

    JsValue to_js_value();

    // Central mutation notification hooks
    static void notify_child_list(
        const std::shared_ptr<Node>& target,
        std::vector<std::shared_ptr<Node>> added,
        std::vector<std::shared_ptr<Node>> removed,
        std::shared_ptr<Node> prev,
        std::shared_ptr<Node> next
    );

    static void notify_attributes(
        const std::shared_ptr<Element>& target,
        const std::string& attr_name,
        const std::string& old_value
    );

    static void notify_character_data(
        const std::shared_ptr<Node>& target,
        const std::string& old_value
    );

private:
    struct Observation {
        std::weak_ptr<Node> target;
        MutationObserverInit options;
    };

    JsValue m_callback;
    std::vector<Observation> m_observations;
    std::vector<MutationRecord> m_pending_records;
    bool m_microtask_queued{false};

    static std::vector<std::weak_ptr<MutationObserver>> s_active_observers;
    static void register_observer(const std::shared_ptr<MutationObserver>& obs);
    static void unregister_observer(MutationObserver* obs);
};

} // namespace litebird
