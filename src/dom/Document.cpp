#include "litebird/dom/Document.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/dom/TreeWalk.hpp"
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
//
// getElementById was a walk over the whole document for every call, so a page
// that looks up each of its 20,000 rows by id took 5.6 seconds to do it. The
// second lookup with nothing changed in between builds an index; the first
// still walks, so a script that changes the page between one lookup and the
// next pays what it always paid and not for an index it would use once.
std::shared_ptr<Element> Document::get_element_by_id(const std::string& id) const {
    if (id.empty()) return nullptr;
    const uint64_t version = tree_version();
    if (m_ids_version != version) {
        if (m_id_walk_version != version) {
            m_id_walk_version = version;
            return descendant_with_id(id);
        }
        m_ids.clear();
        for_each_descendant(*this, [&](Node& node) {
            if (node.is_element()) {
                auto& element = static_cast<Element&>(node);
                const std::string* value = element.attribute("id");
                // emplace keeps the one that is there: the first in tree order.
                if (value && !value->empty()) m_ids.emplace(*value, &element);
            }
            return true;
        });
        m_ids_version = version;
    }
    auto found = m_ids.find(id);
    if (found == m_ids.end()) return nullptr;
    return std::static_pointer_cast<Element>(found->second->shared_from_this());
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
    el->set_owner_document(std::static_pointer_cast<Document>(shared_from_this()));
    return el;
}

std::shared_ptr<Text> Document::create_text_node(const std::string& text) {
    auto node = std::make_shared<Text>(text);
    node->set_owner_document(std::static_pointer_cast<Document>(shared_from_this()));
    return node;
}

std::shared_ptr<Node> Document::create_document_fragment() {
    auto node = std::make_shared<Node>(NodeType::DocumentFragment);
    node->set_owner_document(std::static_pointer_cast<Document>(shared_from_this()));
    return node;
}

std::shared_ptr<DocumentTypeNode> Document::doctype_node() {
    if (!m_doctype.present) return nullptr;
    if (!m_doctype_node) {
        auto self = std::static_pointer_cast<Document>(shared_from_this());
        m_doctype_node = std::make_shared<DocumentTypeNode>(m_doctype.name, m_doctype.public_id, m_doctype.system_id);
        m_doctype_node->set_document(self);
        m_doctype_node->set_owner_document(self);
    }
    return m_doctype_node;
}

void Document::remove_doctype() {
    if (m_doctype_node) m_doctype_node->set_document(nullptr);
    m_doctype_node.reset();
    m_doctype = Doctype{};
    bump_tree_version();
}

std::shared_ptr<Document> Document::template_document() {
    if (!m_template_document) {
        m_template_document = std::make_shared<Document>();
        m_template_document->set_quirks_mode(m_quirks_mode);
        // An XML document's templates hold XML: names keep their case there too.
        m_template_document->set_content_type(m_content_type);
    }
    return m_template_document;
}

void Document::dump_open(std::ostream& os, const std::string& indent, bool) const {
    os << indent << "#document";
    if (!m_title.empty()) os << " (title: \"" << m_title << "\")";
    os << "\n";
}

} // namespace litebird
