#include "litebird/dom/Node.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/dom/Document.hpp"
#include <iomanip>

namespace litebird {

void Node::add_event_listener(const std::string& type, EventListener listener) {
    m_listeners[type].push_back(std::move(listener));
}

void Node::remove_event_listeners(const std::string& type) {
    m_listeners.erase(type);
}

bool Node::dispatch_event(const std::shared_ptr<Event>& event) {
    if (!event) return false;

    event->set_target(shared_from_this());

    // 1. Target Phase
    event->set_current_target(shared_from_this());
    auto it = m_listeners.find(event->type());
    if (it != m_listeners.end()) {
        auto copy = it->second;
        for (const auto& listener : copy) {
            listener(event);
            if (event->propagation_stopped()) return !event->default_prevented();
        }
    }

    // 2. Bubbling Phase
    if (event->bubbles()) {
        auto curr = parent();
        while (curr) {
            event->set_current_target(curr);
            auto p_it = curr->m_listeners.find(event->type());
            if (p_it != curr->m_listeners.end()) {
                auto p_copy = p_it->second;
                for (const auto& listener : p_copy) {
                    listener(event);
                    if (event->propagation_stopped()) return !event->default_prevented();
                }
            }
            curr = curr->parent();
        }
    }

    return !event->default_prevented();
}

std::string Node::text_content() const {
    std::string res;
    for (const auto& ch : m_children) {
        res += ch->text_content();
    }
    return res;
}

void Node::dump(std::ostream& os, int indent) const {
    std::string spaces(indent * 2, ' ');
    os << spaces << "[Node type=" << static_cast<int>(m_type) << "]\n";
    for (const auto& ch : m_children) {
        ch->dump(os, indent + 1);
    }
}

Element* Node::as_element() {
    return m_type == NodeType::Element ? static_cast<Element*>(this) : nullptr;
}

const Element* Node::as_element() const {
    return m_type == NodeType::Element ? static_cast<const Element*>(this) : nullptr;
}

Text* Node::as_text() {
    return m_type == NodeType::Text ? static_cast<Text*>(this) : nullptr;
}

const Text* Node::as_text() const {
    return m_type == NodeType::Text ? static_cast<const Text*>(this) : nullptr;
}

Document* Node::as_document() {
    return m_type == NodeType::Document ? static_cast<Document*>(this) : nullptr;
}

const Document* Node::as_document() const {
    return m_type == NodeType::Document ? static_cast<const Document*>(this) : nullptr;
}

void Element::dump(std::ostream& os, int indent) const {
    std::string spaces(indent * 2, ' ');
    os << spaces << "<" << m_tag_name;
    for (const auto& [k, v] : m_attributes) {
        os << " " << k << "=\"" << v << "\"";
    }
    if (m_children.empty()) {
        os << " />\n";
    } else {
        os << ">\n";
        for (const auto& ch : m_children) {
            ch->dump(os, indent + 1);
        }
        os << spaces << "</" << m_tag_name << ">\n";
    }
}

void Text::dump(std::ostream& os, int indent) const {
    std::string spaces(indent * 2, ' ');
    os << spaces << "#text: \"";
    for (char c : m_text) {
        if (c == '\n') os << "\\n";
        else if (c == '\t') os << "\\t";
        else os << c;
    }
    os << "\"\n";
}

} // namespace litebird
