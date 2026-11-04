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
    Comment
};

using EventListener = std::function<void(const std::shared_ptr<Event>&)>;

class Node : public std::enable_shared_from_this<Node> {
public:
    explicit Node(NodeType type) : m_type(type) {}
    virtual ~Node() = default;

    NodeType type() const { return m_type; }
    bool is_document() const { return m_type == NodeType::Document; }
    bool is_element() const { return m_type == NodeType::Element; }
    bool is_text() const { return m_type == NodeType::Text; }
    bool is_comment() const { return m_type == NodeType::Comment; }

    std::shared_ptr<Node> parent() const { return m_parent.lock(); }
    void set_parent(std::shared_ptr<Node> p) { m_parent = p; }

    const std::vector<std::shared_ptr<Node>>& children() const { return m_children; }
    std::vector<std::shared_ptr<Node>>& children() { return m_children; }

    void append_child(std::shared_ptr<Node> child) {
        if (!child) return;
        child->set_parent(shared_from_this());
        m_children.push_back(child);
    }

    void remove_child(std::shared_ptr<Node> child) {
        auto it = std::find(m_children.begin(), m_children.end(), child);
        if (it != m_children.end()) {
            (*it)->m_parent.reset();
            m_children.erase(it);
        }
    }

    // EventTarget implementation
    void add_event_listener(const std::string& type, EventListener listener);
    void remove_event_listeners(const std::string& type);
    bool dispatch_event(const std::shared_ptr<Event>& event);

    virtual std::string text_content() const;
    virtual void dump(std::ostream& os, int indent = 0) const;

    Element* as_element();
    const Element* as_element() const;
    Text* as_text();
    const Text* as_text() const;
    Document* as_document();
    const Document* as_document() const;

protected:
    NodeType m_type;
    std::weak_ptr<Node> m_parent;
    std::vector<std::shared_ptr<Node>> m_children;
    std::unordered_map<std::string, std::vector<EventListener>> m_listeners;
};

} // namespace litebird
