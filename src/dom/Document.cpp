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

static std::shared_ptr<Element> find_by_id_recursive(const std::shared_ptr<Node>& node, const std::string& id) {
    if (auto el = std::dynamic_pointer_cast<Element>(node)) {
        if (el->id() == id) return el;
    }
    for (const auto& ch : node->children()) {
        auto found = find_by_id_recursive(ch, id);
        if (found) return found;
    }
    return nullptr;
}

std::shared_ptr<Element> Document::get_element_by_id(const std::string& id) const {
    for (const auto& ch : m_children) {
        auto found = find_by_id_recursive(ch, id);
        if (found) return found;
    }
    return nullptr;
}

static void find_by_tag_recursive(const std::shared_ptr<Node>& node, const std::string& tag, std::vector<std::shared_ptr<Element>>& out) {
    if (auto el = std::dynamic_pointer_cast<Element>(node)) {
        if (tag == "*" || el->tag_name() == tag) {
            out.push_back(el);
        }
    }
    for (const auto& ch : node->children()) {
        find_by_tag_recursive(ch, tag, out);
    }
}

std::vector<std::shared_ptr<Element>> Document::get_elements_by_tag_name(const std::string& tag) const {
    std::vector<std::shared_ptr<Element>> result;
    for (const auto& ch : m_children) {
        find_by_tag_recursive(ch, tag, result);
    }
    return result;
}

static void query_selector_all_recursive(const std::shared_ptr<Node>& node, const std::string& sel, std::vector<std::shared_ptr<Element>>& out) {
    if (auto el = std::dynamic_pointer_cast<Element>(node)) {
        bool match = false;
        if (sel.starts_with("#")) {
            match = (el->id() == sel.substr(1));
        } else if (sel.starts_with(".")) {
            match = el->has_class(sel.substr(1));
        } else {
            match = (sel == "*" || el->tag_name() == sel);
        }
        if (match) out.push_back(el);
    }
    for (const auto& ch : node->children()) {
        query_selector_all_recursive(ch, sel, out);
    }
}

std::vector<std::shared_ptr<Element>> Document::query_selector_all(const std::string& selector) const {
    std::vector<std::shared_ptr<Element>> res;
    for (const auto& ch : m_children) {
        query_selector_all_recursive(ch, selector, res);
    }
    return res;
}

std::shared_ptr<Element> Document::query_selector(const std::string& selector) const {
    auto all = query_selector_all(selector);
    return all.empty() ? nullptr : all.front();
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

void Document::dump(std::ostream& os, int indent) const {
    std::string spaces(indent * 2, ' ');
    os << spaces << "#document";
    if (!m_title.empty()) os << " (title: \"" << m_title << "\")";
    os << "\n";
    for (const auto& ch : m_children) {
        ch->dump(os, indent + 1);
    }
}

} // namespace litebird
