#include "litebird/dom/Document.hpp"
#include "litebird/common/TerminalText.hpp"
#include "litebird/css/DocumentStyles.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/layout/LayoutTree.hpp"
#include "litebird/render/ImageDecoder.hpp"
#include <algorithm>
#include <queue>

namespace litebird {

Document::Document() : Node(NodeType::Document) {}
Document::~Document() = default;

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

// A modal dialog puts everything outside it out of reach. The walk up is the
// dialog's own subtree test, and it is bounded by how deep the document is,
// which TreeWalk's callers already accept: a click or a Tab asks this once, not
// once per node.
bool Document::is_inert(const Node& node) const {
    std::shared_ptr<Element> dialog = m_modal_dialog.lock();
    if (!dialog || !dialog->is_connected()) return false;
    for (const Node* ancestor = &node; ancestor; ancestor = ancestor->parent().get()) {
        if (ancestor == dialog.get()) return false;
    }
    return true;
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

void Document::adopt_doctype_node(const std::shared_ptr<DocumentTypeNode>& node, std::size_t preceding) {
    auto self = std::static_pointer_cast<Document>(shared_from_this());
    if (m_doctype_node && m_doctype_node != node) m_doctype_node->set_document(nullptr);
    Doctype record;
    record.present = true;
    record.name = node->name();
    record.public_id = node->public_id();
    record.system_id = node->system_id();
    record.preceding_children = std::min(preceding, children().size());
    m_doctype = std::move(record);
    m_doctype_node = node;
    node->set_document(self);
    node->set_owner_document(self);
    bump_tree_version();
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

void Document::ensure_layout() {
    if (!m_layout_updater || m_in_layout_update) return;
    const uint64_t current_ver = tree_version();
    if (m_layout_tree_version == current_ver && current_ver != 0) return;

    m_in_layout_update = true;
    m_layout_updater(*this);
    m_layout_tree_version = tree_version();
    m_in_layout_update = false;
}

void Document::install_default_layout_updater() {
    if (m_layout_updater) return;
    m_layout_updater = [](Document& doc) {
        StyleResolver resolver = DocumentStyles::build_resolver(
            doc, &doc.stylesheet_cache(), doc.viewport_width(), doc.viewport_height()
        );
        LayoutTree::build(
            doc.shared_from_this(), resolver, doc.viewport_width(), doc.viewport_height()
        );
    };
}

StyleSheetCache& Document::stylesheet_cache() {
    if (!m_stylesheet_cache) {
        m_stylesheet_cache = std::make_unique<StyleSheetCache>();
    }
    return *m_stylesheet_cache;
}

std::shared_ptr<ImageData> Document::find_cached_image(const std::string& resolved_url, bool& in_cache) const {
    auto it = m_image_cache.find(resolved_url);
    if (it == m_image_cache.end()) {
        in_cache = false;
        return nullptr;
    }
    in_cache = true;
    return it->second;
}

void Document::store_cached_image(const std::string& resolved_url, std::shared_ptr<ImageData> img) {
    m_image_cache[resolved_url] = std::move(img);
}

bool Document::allow_external_image_fetch() {
    if (m_external_image_fetches >= kMaxExternalImageFetches) return false;
    ++m_external_image_fetches;
    return true;
}

void Document::clear_subresource_caches() {
    if (m_stylesheet_cache) m_stylesheet_cache->clear();
    m_image_cache.clear();
    m_external_image_fetches = 0;
    m_layout_tree_version = 0;
}

void Document::dump_open(std::ostream& os, const std::string& indent, bool) const {
    os << indent << "#document";
    if (!m_title.empty()) os << " (title: \"" << terminal_safe(m_title) << "\")";
    os << "\n";
}

} // namespace litebird
