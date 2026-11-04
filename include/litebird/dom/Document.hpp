#pragma once

#include "litebird/dom/Node.hpp"
#include <string>
#include <memory>

namespace litebird {

class Document : public Node {
public:
    Document() : Node(NodeType::Document) {}

    const std::string& title() const { return m_title; }
    void set_title(std::string t) { m_title = std::move(t); }

    std::shared_ptr<Element> document_element() const;
    std::shared_ptr<Element> head() const;
    std::shared_ptr<Element> body() const;

    std::shared_ptr<Element> get_element_by_id(const std::string& id) const;
    std::vector<std::shared_ptr<Element>> get_elements_by_tag_name(const std::string& tag) const;
    std::vector<std::shared_ptr<Element>> query_selector_all(const std::string& selector) const;
    std::shared_ptr<Element> query_selector(const std::string& selector) const;

    void dump(std::ostream& os, int indent = 0) const override;

private:
    std::string m_title;
};

} // namespace litebird
