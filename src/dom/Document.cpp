#include "litebird/dom/Document.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include <queue>

namespace litebird {

std::shared_ptr<Element> Document::document_element() const {
    for (const auto& ch : m_children) {
        if (ch->is_element()) {
            return std::static_pointer_cast<Element>(ch);
        }
    }
    return nullptr;
}

std::shared_ptr<Element> Document::head() const {
    auto doc_el = document_element();
    if (doc_el) {
        if (doc_el->tag_name() == "head") return doc_el;
        for (const auto& ch : doc_el->children()) {
            if (auto el = std::dynamic_pointer_cast<Element>(ch)) {
                if (el->tag_name() == "head") return el;
            }
        }
    }
    for (const auto& ch : m_children) {
        if (auto el = std::dynamic_pointer_cast<Element>(ch)) {
            if (el->tag_name() == "head") return el;
        }
    }
    return nullptr;
}

std::shared_ptr<Element> Document::body() const {
    auto doc_el = document_element();
    if (doc_el) {
        if (doc_el->tag_name() == "body") return doc_el;
        for (const auto& ch : doc_el->children()) {
            if (auto el = std::dynamic_pointer_cast<Element>(ch)) {
                if (el->tag_name() == "body") return el;
            }
        }
    }
    for (const auto& ch : m_children) {
        if (auto el = std::dynamic_pointer_cast<Element>(ch)) {
            if (el->tag_name() == "body") return el;
        }
    }
    return nullptr;
}

// The searches walk the tree without recursion (see TreeWalk.hpp). The first
// of them, the <title> lookup at the end of every parse, used to be where a
// page with 300,000 nested elements ended the process.
std::shared_ptr<Element> Document::get_element_by_id(const std::string& id) const {
    return descendant_with_id(id);
}

std::vector<std::shared_ptr<Element>> Document::get_elements_by_tag_name(const std::string& tag) const {
    return descendants_with_tag(tag);
}

std::vector<std::shared_ptr<Element>> Document::query_selector_all(const std::string& selector) const {
    return descendants_matching(selector);
}

std::shared_ptr<Element> Document::query_selector(const std::string& selector) const {
    return first_descendant_matching(selector);
}

std::shared_ptr<Element> Document::create_element(const std::string& tag_name) {
    auto el = std::make_shared<Element>(tag_name);
    return el;
}

std::shared_ptr<Text> Document::create_text_node(const std::string& text) {
    return std::make_shared<Text>(text);
}

std::shared_ptr<Node> Document::create_document_fragment() {
    return std::make_shared<Node>(NodeType::DocumentFragment);
}

void Document::dump_open(std::ostream& os, const std::string& indent, bool) const {
    os << indent << "#document";
    if (!m_title.empty()) os << " (title: \"" << m_title << "\")";
    os << "\n";
}

} // namespace litebird
