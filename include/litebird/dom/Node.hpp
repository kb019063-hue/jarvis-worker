#pragma once

#include "litebird/dom/Event.hpp"
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <functional>

namespace litebird {

class Element;
class Text;
class Document;

enum class NodeType {
    Document,
    Element,
    Text,
    Comment,
    DocumentFragment
};

using EventListener = std::function<void(const std::shared_ptr<Event>&)>;

class Node : public std::enable_shared_from_this<Node> {
public:
    explicit Node(NodeType type)
        : m_type(type), m_is_connected(type == NodeType::Document) {}
    virtual ~Node() = default;

    NodeType type() const { return m_type; }
    bool is_document() const { return m_type == NodeType::Document; }
    bool is_element() const { return m_type == NodeType::Element; }
    bool is_text() const { return m_type == NodeType::Text; }
    bool is_comment() const { return m_type == NodeType::Comment; }
    bool is_document_fragment() const { return m_type == NodeType::DocumentFragment; }

    std::shared_ptr<Node> parent() const { return m_parent.lock(); }
    void set_parent(std::shared_ptr<Node> p);

    const std::vector<std::shared_ptr<Node>>& children() const { return m_children; }
    std::vector<std::shared_ptr<Node>>& children() { return m_children; }

    std::shared_ptr<Node> first_child() const { return m_children.empty() ? nullptr : m_children.front(); }
    std::shared_ptr<Node> last_child() const { return m_children.empty() ? nullptr : m_children.back(); }
    std::shared_ptr<Node> previous_sibling() const;
    std::shared_ptr<Node> next_sibling() const;

    bool is_connected() const { return m_is_connected; }

    void append_child(std::shared_ptr<Node> child);
    void remove_child(std::shared_ptr<Node> child);
    void insert_before(std::shared_ptr<Node> new_child, std::shared_ptr<Node> ref_child);

    // Invalidation / dirty tracking
    bool needs_layout() const { return m_needs_layout; }
    bool child_needs_layout() const { return m_child_needs_layout; }
    void mark_needs_layout();
    void clear_layout_flags() {
        m_needs_layout = false;
        m_child_needs_layout = false;
    }

    // EventTarget implementation
    struct EventListenerEntry {
        EventListener listener;
        bool use_capture{false};
    };

    void add_event_listener(const std::string& type, EventListener listener, bool use_capture = false);
    void remove_event_listeners(const std::string& type);
    bool dispatch_event(const std::shared_ptr<Event>& event);
    bool has_listeners(const std::string& type) const {
        auto it = m_listeners.find(type);
        return it != m_listeners.end() && !it->second.empty();
    }

    virtual std::string text_content() const;
    virtual void dump(std::ostream& os, int indent = 0) const;

    Element* as_element();
    const Element* as_element() const;
    Text* as_text();
    const Text* as_text() const;
    Document* as_document();
    const Document* as_document() const;

protected:
    // Connectivity is maintained as the tree is attached/detached. Keeping it
    // cached makes append_child's connectedness check O(1) instead of walking
    // every ancestor for every parser insertion.
    void set_connected_state_subtree(bool connected);

    NodeType m_type;
    bool m_is_connected{false};
    std::weak_ptr<Node> m_parent;
    std::vector<std::shared_ptr<Node>> m_children;
    std::unordered_map<std::string, std::vector<EventListenerEntry>> m_listeners;
    bool m_needs_layout{true};
    bool m_child_needs_layout{true};
};

} // namespace litebird
