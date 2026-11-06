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

        if (peek() == '@') {
            advance(); // consume '@'
            std::string at_keyword;
            while (m_cursor < m_css.size() && std::isalpha(peek())) {
                at_keyword += advance();
            }
            if (at_keyword == "media") {
                consume_whitespace_and_comments();
                std::string media_query_str;
                while (m_cursor < m_css.size() && peek() != '{') {
                    media_query_str += advance();
                }
                if (match('{')) {
                    MediaQuery mq = parse_media_query(media_query_str);
                    while (m_cursor < m_css.size()) {
                        consume_whitespace_and_comments();
                        if (peek() == '}') {
                            advance();
                            break;
                        }
                        if (peek() == '@') {
                            // Skip nested @rule
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
                        } else {
                            Rule inner_rule = parse_rule();
                            inner_rule.media_queries.push_back(mq);
                            sheet.rules.push_back(std::move(inner_rule));
                        }
                    }
                }
                continue;
            }

            // Skip other @rules like @import or @keyframes
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

        size_t cursor_before = m_cursor;
        auto simple = parse_simple_selector();
        sel.parts.push_back(std::move(simple));

        if (m_cursor == cursor_before) {
            advance();
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

    // Check for #id, .class, [attr=val], or :pseudo attached
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
        } else if (peek() == '[') {
            advance();
            std::string attr_str;
            while (m_cursor < m_css.size() && peek() != ']') {
                attr_str += advance();
            }
            if (peek() == ']') advance();

            auto eq = attr_str.find('=');
            if (eq != std::string::npos) {
                std::string k = attr_str.substr(0, eq);
                std::string v = attr_str.substr(eq + 1);
                auto strip = [](std::string& str) {
                    while (!str.empty() && (std::isspace(str.front()) || str.front() == '"' || str.front() == '\'')) str.erase(str.begin());
                    while (!str.empty() && (std::isspace(str.back()) || str.back() == '"' || str.back() == '\'')) str.pop_back();
                };
                strip(k);
                strip(v);
                s.attributes.emplace_back(std::move(k), std::move(v));
            } else {
                while (!attr_str.empty() && std::isspace(attr_str.front())) attr_str.erase(attr_str.begin());
                while (!attr_str.empty() && std::isspace(attr_str.back())) attr_str.pop_back();
                s.attributes.emplace_back(std::move(attr_str), "");
            }
        } else if (peek() == ':') {
            advance();
            while (std::isalnum(peek()) || peek() == '-') {
                advance();
            }
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

MediaQuery CssParser::parse_media_query(std::string_view str) {
    MediaQuery mq;
    std::string s(str);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    if (s.find("screen") != std::string::npos) mq.media_type = "screen";
    else if (s.find("print") != std::string::npos) mq.media_type = "print";

    auto parse_feature = [&](const std::string& feat_name, float& target) {
        auto pos = s.find(feat_name);
        if (pos != std::string::npos) {
            auto colon = s.find(':', pos);
            if (colon != std::string::npos) {
                size_t num_start = colon + 1;
                while (num_start < s.size() && std::isspace(s[num_start])) num_start++;
                size_t num_end = num_start;
                while (num_end < s.size() && (std::isdigit(s[num_end]) || s[num_end] == '.')) num_end++;
                if (num_end > num_start) {
                    try {
                        target = std::stof(s.substr(num_start, num_end - num_start));
                    } catch (...) {}
                }
            }
        }
    };

    parse_feature("max-width", mq.max_width);
    parse_feature("min-width", mq.min_width);
    return mq;
}

} // namespace litebird
