#pragma once

#include "litebird/dom/Node.hpp"
#include <cstddef>
#include <string>
#include <memory>

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

class Document : public Node {
public:
    Document() : Node(NodeType::Document) {}

    void set_doctype(Doctype d) { m_doctype = std::move(d); }
    const Doctype& doctype() const { return m_doctype; }
    bool has_doctype() const { return m_doctype.present; }

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
};

} // namespace litebird
