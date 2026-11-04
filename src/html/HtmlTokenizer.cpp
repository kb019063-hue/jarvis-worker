#include "litebird/html/HtmlTokenizer.hpp"
#include <cctype>
#include <algorithm>

namespace litebird {

HtmlTokenizer::HtmlTokenizer(std::string_view input) : m_input(input) {}

char HtmlTokenizer::peek(size_t offset) const {
    if (m_cursor + offset >= m_input.size()) return '\0';
    return m_input[m_cursor + offset];
}

char HtmlTokenizer::advance() {
    if (m_cursor >= m_input.size()) return '\0';
    return m_input[m_cursor++];
}

void HtmlTokenizer::consume_whitespace() {
    while (m_cursor < m_input.size() && std::isspace(peek())) {
        advance();
    }
}

void HtmlTokenizer::switch_to_raw_text(std::string tag_name) {
    m_raw_text_mode = true;
    m_raw_text_tag = std::move(tag_name);
    std::transform(m_raw_text_tag.begin(), m_raw_text_tag.end(), m_raw_text_tag.begin(), ::tolower);
}

HtmlToken HtmlTokenizer::consume_raw_text() {
    std::string content;
    std::string closing_pattern = "</" + m_raw_text_tag + ">";

    while (m_cursor < m_input.size()) {
        if (peek() == '<' && m_cursor + closing_pattern.size() <= m_input.size()) {
            std::string sub(m_input.substr(m_cursor, closing_pattern.size()));
            std::string sub_lower = sub;
            std::transform(sub_lower.begin(), sub_lower.end(), sub_lower.begin(), ::tolower);
            if (sub_lower == closing_pattern) {
                m_raw_text_mode = false;
                m_raw_text_tag.clear();
                break;
            }
        }
        content += advance();
    }

    HtmlToken token;
    token.type = HtmlTokenType::Character;
    token.data = std::move(content);
    return token;
}

HtmlToken HtmlTokenizer::consume_comment() {
    // Already seen "<!--"
    std::string comment;
    while (m_cursor < m_input.size()) {
        if (peek() == '-' && peek(1) == '-' && peek(2) == '>') {
            advance(); advance(); advance();
            break;
        }
        comment += advance();
    }
    HtmlToken t;
    t.type = HtmlTokenType::Comment;
    t.data = std::move(comment);
    return t;
}

HtmlToken HtmlTokenizer::consume_doctype() {
    // Seen "<!"
    std::string doctype;
    while (m_cursor < m_input.size() && peek() != '>') {
        doctype += advance();
    }
    if (peek() == '>') advance();
    HtmlToken t;
    t.type = HtmlTokenType::Doctype;
    t.data = std::move(doctype);
    return t;
}

HtmlToken HtmlTokenizer::consume_tag() {
    // Seen '<'
    bool is_closing = false;
    if (peek() == '/') {
        is_closing = true;
        advance();
    }

    std::string tag_name;
    while (m_cursor < m_input.size() && !std::isspace(peek()) && peek() != '>' && peek() != '/') {
        tag_name += advance();
    }
    std::transform(tag_name.begin(), tag_name.end(), tag_name.begin(), ::tolower);

    HtmlToken token;
    token.type = is_closing ? HtmlTokenType::EndTag : HtmlTokenType::StartTag;
    token.tag_name = tag_name;

    // Attributes
    while (m_cursor < m_input.size()) {
        consume_whitespace();
        if (peek() == '>') {
            advance();
            break;
        }
        if (peek() == '/' && peek(1) == '>') {
            token.self_closing = true;
            advance(); advance();
            break;
        }

        std::string attr_name;
        while (m_cursor < m_input.size() && !std::isspace(peek()) && peek() != '=' && peek() != '>' && peek() != '/') {
            attr_name += advance();
        }
        std::transform(attr_name.begin(), attr_name.end(), attr_name.begin(), ::tolower);

        consume_whitespace();
        std::string attr_val;
        if (peek() == '=') {
            advance();
            consume_whitespace();
            if (peek() == '"' || peek() == '\'') {
                char quote = advance();
                while (m_cursor < m_input.size() && peek() != quote) {
                    attr_val += advance();
                }
                if (peek() == quote) advance();
            } else {
                while (m_cursor < m_input.size() && !std::isspace(peek()) && peek() != '>') {
                    attr_val += advance();
                }
            }
        }

        if (!attr_name.empty()) {
            token.attributes[attr_name] = attr_val;
        }
    }

    return token;
}

HtmlToken HtmlTokenizer::next_token() {
    if (m_cursor >= m_input.size()) {
        return HtmlToken::make_eof();
    }

    if (m_raw_text_mode) {
        return consume_raw_text();
    }

    if (peek() == '<') {
        if (peek(1) == '!' && peek(2) == '-' && peek(3) == '-') {
            m_cursor += 4;
            return consume_comment();
        }
        if (peek(1) == '!') {
            m_cursor += 2;
            return consume_doctype();
        }
        advance(); // consume '<'
        return consume_tag();
    }

    // Text content
    std::string text;
    while (m_cursor < m_input.size() && peek() != '<') {
        if (peek() == '&') {
            // Simple entity decode
            if (m_cursor + 4 <= m_input.size() && m_input.substr(m_cursor, 4) == "&lt;") {
                text += '<'; m_cursor += 4; continue;
            }
            if (m_cursor + 4 <= m_input.size() && m_input.substr(m_cursor, 4) == "&gt;") {
                text += '>'; m_cursor += 4; continue;
            }
            if (m_cursor + 5 <= m_input.size() && m_input.substr(m_cursor, 5) == "&amp;") {
                text += '&'; m_cursor += 5; continue;
            }
            if (m_cursor + 6 <= m_input.size() && m_input.substr(m_cursor, 6) == "&quot;") {
                text += '"'; m_cursor += 6; continue;
            }
            if (m_cursor + 6 <= m_input.size() && m_input.substr(m_cursor, 6) == "&nbsp;") {
                text += ' '; m_cursor += 6; continue;
            }
        }
        text += advance();
    }

    HtmlToken token;
    token.type = HtmlTokenType::Character;
    token.data = std::move(text);
    return token;
}

} // namespace litebird
