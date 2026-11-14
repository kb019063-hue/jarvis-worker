#pragma once

#include "litebird/dom/Node.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>

namespace litebird {

/// Document type information retained from the tokenizer.
struct Doctype {
    bool present{false};
    std::string name;
    std::string public_id;
    std::string system_id;
    /// How many children of the document come before the doctype: the
    /// comments written ahead of it. The doctype is kept here and not among
    /// the children, and this is its place among them.
    std::size_t preceding_children{0};
};

/// Document mode, which changes a handful of parser and CSS behaviours.
enum class QuirksMode {
    NoQuirks,
    Quirks,
    LimitedQuirks
};

class Document;

/// `document.doctype`: the doctype as a node. Made on demand from the
/// document's Doctype record and never one of its children(); see
/// NodeType::DocumentType.
class DocumentTypeNode : public Node {
public:
    DocumentTypeNode(std::string name, std::string public_id, std::string system_id)
        : Node(NodeType::DocumentType), m_name(std::move(name)), m_public_id(std::move(public_id)),
          m_system_id(std::move(system_id)) {}

    const std::string& name() const { return m_name; }
    const std::string& public_id() const { return m_public_id; }
    const std::string& system_id() const { return m_system_id; }

    /// The document this is the doctype of; empty once it was removed.
    std::shared_ptr<Document> document() const { return m_document.lock(); }
    void set_document(const std::shared_ptr<Document>& document) { m_document = document; }

    std::string text_content() const override { return std::string(); }

private:
    std::string m_name;
    std::string m_public_id;
    std::string m_system_id;
    std::weak_ptr<Document> m_document;
};

class Document : public Node {
public:
    Document() : Node(NodeType::Document) {}

    void set_doctype(Doctype d) { m_doctype = std::move(d); m_doctype_node.reset(); }
    const Doctype& doctype() const { return m_doctype; }
    bool has_doctype() const { return m_doctype.present; }
    /// The doctype as a node, the same one every time; null without a doctype.
    std::shared_ptr<DocumentTypeNode> doctype_node();
    /// The same, only if it has been made already.
    DocumentTypeNode* existing_doctype_node() const { return m_doctype_node.get(); }
    /// Takes the doctype out of the document (`doctype.remove()`).
    void remove_doctype();
    /// Makes `node`, which a script made, the doctype: `document.doctype`
    /// is that same object from now on. `preceding` is how many of the
    /// children stand before it.
    void adopt_doctype_node(const std::shared_ptr<DocumentTypeNode>& node, std::size_t preceding);
    /// The doctype's place among the children moves with what is put in
    /// and taken out before it. Called by Node for every such change.
    void note_child_inserted(std::size_t index) {
        if (m_doctype.present && index < m_doctype.preceding_children) ++m_doctype.preceding_children;
    }
    void note_child_removed(std::size_t index) {
        if (m_doctype.present && index < m_doctype.preceding_children) --m_doctype.preceding_children;
    }
    void note_children_taken() { m_doctype.preceding_children = 0; }
    /// For a node inserted "before the doctype": it went in where the
    /// doctype stood, and the doctype now stands after it.
    void move_doctype_after(std::size_t more_children) {
        if (m_doctype.present) m_doctype.preceding_children += more_children;
    }

    // --- What a script asks a document besides its tree ------------------------
    /// The address the document was loaded from; "about:blank" for one a
    /// script made.
    const std::string& url() const { return m_url; }
    void set_url(std::string url) { m_url = std::move(url); }
    /// "loading", "interactive" or "complete".
    const std::string& ready_state() const { return m_ready_state; }
    void set_ready_state(std::string state) { m_ready_state = std::move(state); }
    /// The element that has focus; null when it is the body's by default.
    std::shared_ptr<Element> focused_element() const { return m_focused.lock(); }
    void set_focused_element(const std::shared_ptr<Element>& element) { m_focused = element; }
    /// The <script> element being run, for `document.currentScript`.
    std::shared_ptr<Element> current_script() const { return m_current_script.lock(); }
    void set_current_script(const std::shared_ptr<Element>& script) { m_current_script = script; }
    /// The document that owns the contents of this document's <template>
    /// elements: a separate one with no window, so that nothing in a template
    /// runs or loads. Made on first use.
    std::shared_ptr<Document> template_document();
    /// True for the one document a window shows; false for those a script
    /// creates (createHTMLDocument, DOMParser) and for template_document().
    bool has_window() const { return m_has_window; }
    void set_has_window(bool value) { m_has_window = value; }

    /// "text/html" for a document the HTML parser made and for one made by
    /// createHTMLDocument(); an XML type ("application/xml",
    /// "application/xhtml+xml", "image/svg+xml", ...) for one the XML parser
    /// made, createDocument() made, or `new Document()` made.
    const std::string& content_type() const { return m_content_type; }
    void set_content_type(std::string type) {
        m_content_type = std::move(type);
        m_is_html = m_content_type == "text/html";
    }
    /// The DOM standard's "HTML document". In one, the names of HTML
    /// elements and of their attributes are matched without regard to case
    /// and shown in upper case; in an XML document a name is what it is.
    bool is_html_document() const { return m_is_html; }
    /// True for what createDocument() returns: the one kind of document
    /// whose script object is an XMLDocument.
    bool is_xml_document_object() const { return m_xml_document_object; }
    void set_xml_document_object(bool value) { m_xml_document_object = value; }

    void set_quirks_mode(QuirksMode m) { m_quirks_mode = m; }
    QuirksMode quirks_mode() const { return m_quirks_mode; }

    const std::string& title() const { return m_title; }
    void set_title(std::string t) { m_title = std::move(t); }

    std::shared_ptr<Element> document_element() const;
    std::shared_ptr<Element> head() const;
    std::shared_ptr<Element> body() const;

    std::shared_ptr<Element> get_element_by_id(const std::string& id) const;
    std::vector<std::shared_ptr<Element>> get_elements_by_tag_name(const std::string& tag) const;
    std::vector<std::shared_ptr<Element>> query_selector_all(const std::string& selector) const;
    std::shared_ptr<Element> query_selector(const std::string& selector) const;

    std::shared_ptr<Element> create_element(const std::string& tag_name);
    std::shared_ptr<Text> create_text_node(const std::string& text);
    std::shared_ptr<Node> create_document_fragment();

protected:
    void dump_open(std::ostream& os, const std::string& indent, bool has_content) const override;

private:
    std::string m_title;
    Doctype m_doctype;
    QuirksMode m_quirks_mode{QuirksMode::NoQuirks};
    std::shared_ptr<DocumentTypeNode> m_doctype_node;
    std::string m_url{"about:blank"};
    std::string m_ready_state{"complete"};
    std::weak_ptr<Element> m_focused;
    std::weak_ptr<Element> m_current_script;
    std::shared_ptr<Document> m_template_document;
    bool m_has_window{false};
    std::string m_content_type{"text/html"};
    bool m_is_html{true};
    bool m_xml_document_object{false};

    // getElementById; see Document.cpp. The pointers are only read while the
    // tree is at the version the index was built at: an element cannot leave
    // the tree, let alone be freed, without the version moving on.
    mutable std::unordered_map<std::string, Element*> m_ids;
    mutable uint64_t m_ids_version{0};
    mutable uint64_t m_id_walk_version{0};
};

} // namespace litebird
