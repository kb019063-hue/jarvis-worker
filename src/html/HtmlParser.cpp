#include "litebird/html/HtmlParser.hpp"
#include <unordered_set>
#include <algorithm>

namespace litebird {

static const std::unordered_set<std::string> VOID_ELEMENTS = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr"
};

bool HtmlParser::is_void_element(const std::string& tag) const {
    return VOID_ELEMENTS.find(tag) != VOID_ELEMENTS.end();
}

HtmlParser::HtmlParser(std::string_view html)
    : m_tokenizer(html), m_document(std::make_shared<Document>()) {}

std::shared_ptr<Document> HtmlParser::parse(std::string_view html) {
    HtmlParser parser(html);
    return parser.parse_internal();
}

std::shared_ptr<Document> HtmlParser::parse_internal() {
    while (m_tokenizer.has_more()) {
        HtmlToken token = m_tokenizer.next_token();
        if (token.type == HtmlTokenType::EndOfFile) {
            break;
        }

        switch (token.type) {
            case HtmlTokenType::Doctype:
                // Ignored or stored
                break;

            case HtmlTokenType::Comment:
                // Comments can be ignored or stored
                break;

            case HtmlTokenType::Character: {
                // If we have text, append to current element or document
                if (token.data.empty()) break;

                // If currently inside <style>
                if (!m_element_stack.empty() && m_element_stack.back()->tag_name() == "style") {
                    m_stylesheets.push_back(token.data);
                    m_element_stack.back()->append_child(std::make_shared<Text>(token.data));
                } else if (!m_element_stack.empty() && m_element_stack.back()->tag_name() == "script") {
                    m_scripts.push_back(token.data);
                    m_element_stack.back()->append_child(std::make_shared<Text>(token.data));
                } else {
                    auto text_node = std::make_shared<Text>(token.data);
                    if (!m_element_stack.empty()) {
                        m_element_stack.back()->append_child(text_node);
                    } else {
                        m_document->append_child(text_node);
                    }
                }
                break;
            }

            case HtmlTokenType::StartTag: {
                auto element = std::make_shared<Element>(token.tag_name);
                for (const auto& [k, v] : token.attributes) {
                    element->set_attribute(k, v);
                }

                if (!m_element_stack.empty()) {
                    m_element_stack.back()->append_child(element);
                } else {
                    m_document->append_child(element);
                }

                if (token.tag_name == "title") {
                    // We'll capture its text when EndTag or Character arrives
                }

                // If raw text tag like <style> or <script>, switch tokenizer mode
                if (token.tag_name == "style" || token.tag_name == "script") {
                    m_tokenizer.switch_to_raw_text(token.tag_name);
                }

                if (!token.self_closing && !is_void_element(token.tag_name)) {
                    m_element_stack.push_back(element);
                }
                break;
            }

            case HtmlTokenType::EndTag: {
                // Pop stack until matching tag
                if (token.tag_name == "title" && !m_element_stack.empty() && m_element_stack.back()->tag_name() == "title") {
                    m_document->set_title(m_element_stack.back()->text_content());
                }

                for (int i = static_cast<int>(m_element_stack.size()) - 1; i >= 0; --i) {
                    if (m_element_stack[i]->tag_name() == token.tag_name) {
                        m_element_stack.resize(i);
                        break;
                    }
                }
                break;
            }

            case HtmlTokenType::EndOfFile:
                break;
        }
    }

    return m_document;
}

} // namespace litebird
