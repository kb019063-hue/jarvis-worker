#pragma once

#include "litebird/dom/Document.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/Text.hpp"
#include "litebird/html/HtmlTokenizer.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace litebird {

class HtmlParser {
public:
    static std::shared_ptr<Document> parse(std::string_view html);
    static std::vector<std::shared_ptr<Node>> parse_fragment(std::string_view html, std::vector<std::string>* out_stylesheets = nullptr);

    const std::vector<std::string>& stylesheets() const { return m_stylesheets; }
    const std::vector<std::string>& scripts() const { return m_scripts; }

private:
    explicit HtmlParser(std::string_view html);
    std::shared_ptr<Document> parse_internal();

    bool is_void_element(const std::string& tag) const;

    HtmlTokenizer m_tokenizer;
    std::vector<std::shared_ptr<Element>> m_element_stack;
    std::shared_ptr<Document> m_document;
    std::vector<std::string> m_stylesheets;
    std::vector<std::string> m_scripts;
};

} // namespace litebird
