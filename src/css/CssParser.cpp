#include "litebird/css/CssParser.hpp"
#include <cctype>
#include <sstream>

namespace litebird {

CssParser::CssParser(std::string_view css) : m_css(css) {}

char CssParser::peek(size_t offset) const {
    if (m_cursor + offset >= m_css.size()) return '\0';
    return m_css[m_cursor + offset];
}

char CssParser::advance() {
    if (m_cursor >= m_css.size()) return '\0';
    return m_css[m_cursor++];
}

bool CssParser::match(char expected) {
    if (peek() == expected) {
        advance();
        return true;
    }
    return false;
}

void CssParser::consume_whitespace_and_comments() {
    while (m_cursor < m_css.size()) {
        if (std::isspace(peek())) {
            advance();
        } else if (peek() == '/' && peek(1) == '*') {
            advance(); advance();
            while (m_cursor < m_css.size() && !(peek() == '*' && peek(1) == '/')) {
                advance();
            }
            if (m_cursor < m_css.size()) {
                advance(); advance();
            }
        } else {
            break;
        }
    }
}

StyleSheet CssParser::parse(std::string_view css) {
    CssParser parser(css);
    return parser.parse_stylesheet();
}

StyleSheet CssParser::parse_stylesheet() {
    StyleSheet sheet;
    consume_whitespace_and_comments();

    while (m_cursor < m_css.size()) {
        consume_whitespace_and_comments();
        if (m_cursor >= m_css.size()) break;

        // Skip @rules like @media or @import for now if encountered
        if (peek() == '@') {
            while (m_cursor < m_css.size() && peek() != ';' && peek() != '{') advance();
            if (peek() == '{') {
                int depth = 1;
                advance();
                while (m_cursor < m_css.size() && depth > 0) {
                    if (peek() == '{') depth++;
                    else if (peek() == '}') depth--;
                    advance();
                }
            } else if (peek() == ';') {
                advance();
            }
            continue;
        }

        sheet.rules.push_back(parse_rule());
    }

    return sheet;
}

Rule CssParser::parse_rule() {
    Rule rule;

    // Parse selector list (separated by comma)
    while (m_cursor < m_css.size()) {
        consume_whitespace_and_comments();
        rule.selectors.push_back(parse_selector());
        consume_whitespace_and_comments();

        if (peek() == ',') {
            advance();
            continue;
        }
        if (peek() == '{') {
            advance();
            break;
        }
        if (m_cursor >= m_css.size()) break;
    }

    // Parse declarations
    while (m_cursor < m_css.size()) {
        consume_whitespace_and_comments();
        if (peek() == '}' || m_cursor >= m_css.size()) {
            if (peek() == '}') advance();
            break;
        }
        rule.declarations.push_back(parse_declaration());
    }

    return rule;
}

Selector CssParser::parse_selector() {
    Selector sel;

    while (m_cursor < m_css.size() && peek() != '{' && peek() != ',') {
        consume_whitespace_and_comments();
        if (peek() == '{' || peek() == ',' || m_cursor >= m_css.size()) break;

        sel.parts.push_back(parse_simple_selector());

        // Check if there is whitespace separating simple selectors (descendant combinator)
        if (peek() != '{' && peek() != ',' && !std::isspace(peek(-1))) {
            // Keep going
        }
    }

    return sel;
}

SimpleSelector CssParser::parse_simple_selector() {
    SimpleSelector s;

    // Check for tag name
    if (std::isalpha(peek()) || peek() == '*') {
        while (std::isalnum(peek()) || peek() == '-' || peek() == '*' || peek() == '_') {
            s.tag_name += advance();
        }
        std::transform(s.tag_name.begin(), s.tag_name.end(), s.tag_name.begin(), ::tolower);
    }

    // Check for #id or .class attached
    while (m_cursor < m_css.size()) {
        if (peek() == '#') {
            advance();
            while (std::isalnum(peek()) || peek() == '-' || peek() == '_') {
                s.id += advance();
            }
        } else if (peek() == '.') {
            advance();
            std::string cls;
            while (std::isalnum(peek()) || peek() == '-' || peek() == '_') {
                cls += advance();
            }
            if (!cls.empty()) s.classes.push_back(cls);
        } else {
            break;
        }
    }

    return s;
}

Declaration CssParser::parse_declaration() {
    Declaration decl;

    consume_whitespace_and_comments();
    while (m_cursor < m_css.size() && peek() != ':' && !std::isspace(peek())) {
        decl.property += advance();
    }
    std::transform(decl.property.begin(), decl.property.end(), decl.property.begin(), ::tolower);

    consume_whitespace_and_comments();
    match(':');
    consume_whitespace_and_comments();

    while (m_cursor < m_css.size() && peek() != ';' && peek() != '}') {
        decl.value += advance();
    }

    // Trim trailing whitespace from value
    while (!decl.value.empty() && std::isspace(decl.value.back())) {
        decl.value.pop_back();
    }

    if (peek() == ';') advance();

    return decl;
}

} // namespace litebird
