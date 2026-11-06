#include "litebird/js/JsLexer.hpp"
#include <cctype>
#include <unordered_map>

namespace litebird {

JsLexer::JsLexer(std::string_view source) : m_source(source) {}

char JsLexer::peek(size_t offset) const {
    if (m_cursor + offset >= m_source.size()) return '\0';
    return m_source[m_cursor + offset];
}

char JsLexer::advance() {
    if (m_cursor >= m_source.size()) return '\0';
    char c = m_source[m_cursor++];
    if (c == '\n') m_line++;
    return c;
}

bool JsLexer::match(char expected) {
    if (peek() == expected) {
        advance();
        return true;
    }
    return false;
}

void JsLexer::skip_whitespace_and_comments() {
    while (m_cursor < m_source.size()) {
        char c = peek();
        if (std::isspace(c)) {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            // Line comment
            advance(); advance();
            while (m_cursor < m_source.size() && peek() != '\n') {
                advance();
            }
        } else if (c == '/' && peek(1) == '*') {
            // Block comment
            advance(); advance();
            while (m_cursor < m_source.size() && !(peek() == '*' && peek(1) == '/')) {
                advance();
            }
            if (m_cursor < m_source.size()) {
                advance(); advance();
            }
        } else {
            break;
        }
    }
}

JsToken JsLexer::scan_number() {
    size_t start = m_cursor;
    while (std::isdigit(peek())) {
        advance();
    }
    if (peek() == '.' && std::isdigit(peek(1))) {
        advance(); // consume '.'
        while (std::isdigit(peek())) {
            advance();
        }
    }

    std::string text(m_source.substr(start, m_cursor - start));
    JsToken t;
    t.type = JsTokenType::Number;
    t.text = text;
    t.number_value = std::stod(text);
    t.line = m_line;
    return t;
}

JsToken JsLexer::scan_string(char quote) {
    std::string result;
    while (m_cursor < m_source.size() && peek() != quote) {
        if (peek() == '\\' && m_cursor + 1 < m_source.size()) {
            advance();
            char esc = advance();
            switch (esc) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case '\\': result += '\\'; break;
                case '"': result += '"'; break;
                case '\'': result += '\''; break;
                default: result += esc; break;
            }
        } else {
            result += advance();
        }
    }
    if (peek() == quote) advance();

    JsToken t;
    t.type = JsTokenType::String;
    t.text = result;
    t.line = m_line;
    return t;
}

JsToken JsLexer::scan_identifier() {
    size_t start = m_cursor;
    while (std::isalnum(peek()) || peek() == '_' || peek() == '$') {
        advance();
    }

    std::string text(m_source.substr(start, m_cursor - start));
    static const std::unordered_map<std::string, JsTokenType> keywords = {
        {"let", JsTokenType::Let},
        {"var", JsTokenType::Var},
        {"const", JsTokenType::Const},
        {"function", JsTokenType::Function},
        {"return", JsTokenType::Return},
        {"if", JsTokenType::If},
        {"else", JsTokenType::Else},
        {"while", JsTokenType::While},
        {"new", JsTokenType::New},
        {"true", JsTokenType::True},
        {"false", JsTokenType::False},
        {"null", JsTokenType::Null},
        {"undefined", JsTokenType::Undefined},
        {"class", JsTokenType::Class},
        {"extends", JsTokenType::Extends},
        {"super", JsTokenType::Super},
        {"static", JsTokenType::Static},
        {"instanceof", JsTokenType::InstanceOf},
        {"async", JsTokenType::Async},
        {"await", JsTokenType::Await}
    };

    JsToken t;
    auto it = keywords.find(text);
    if (it != keywords.end()) {
        t.type = it->second;
    } else {
        t.type = JsTokenType::Identifier;
    }
    t.text = text;
    t.line = m_line;
    return t;
}

JsToken JsLexer::scan_template_start() {
    JsToken t;
    t.line = m_line;
    std::string str;

    while (m_cursor < m_source.size()) {
        char ch = peek();
        if (ch == '`') {
            advance();
            t.type = JsTokenType::NoSubstitutionTemplate;
            t.text = str;
            return t;
        }
        if (ch == '$' && peek(1) == '{') {
            advance(); advance();
            m_template_brace_stack.push_back(m_brace_depth);
            t.type = JsTokenType::TemplateHead;
            t.text = str;
            return t;
        }
        if (ch == '\\') {
            advance();
            char esc = advance();
            switch (esc) {
                case 'n': str += '\n'; break;
                case 't': str += '\t'; break;
                case 'r': str += '\r'; break;
                case '\\': str += '\\'; break;
                case '`': str += '`'; break;
                case '$': str += '$'; break;
                case '"': str += '"'; break;
                case '\'': str += '\''; break;
                default: str += esc; break;
            }
        } else {
            str += advance();
        }
    }

    t.type = JsTokenType::NoSubstitutionTemplate;
    t.text = str;
    return t;
}

JsToken JsLexer::scan_template_continuation() {
    JsToken t;
    t.line = m_line;
    std::string str;

    while (m_cursor < m_source.size()) {
        char ch = peek();
        if (ch == '`') {
            advance();
            t.type = JsTokenType::TemplateTail;
            t.text = str;
            return t;
        }
        if (ch == '$' && peek(1) == '{') {
            advance(); advance();
            m_template_brace_stack.push_back(m_brace_depth);
            t.type = JsTokenType::TemplateMiddle;
            t.text = str;
            return t;
        }
        if (ch == '\\') {
            advance();
            char esc = advance();
            switch (esc) {
                case 'n': str += '\n'; break;
                case 't': str += '\t'; break;
                case 'r': str += '\r'; break;
                case '\\': str += '\\'; break;
                case '`': str += '`'; break;
                case '$': str += '$'; break;
                case '"': str += '"'; break;
                case '\'': str += '\''; break;
                default: str += esc; break;
            }
        } else {
            str += advance();
        }
    }

    t.type = JsTokenType::TemplateTail;
    t.text = str;
    return t;
}

JsToken JsLexer::next_token() {
    skip_whitespace_and_comments();

    if (m_cursor >= m_source.size()) {
        JsToken t;
        t.type = JsTokenType::Eof;
        t.line = m_line;
        return t;
    }

    char c = peek();

    if (std::isdigit(c)) {
        return scan_number();
    }

    if (c == '"' || c == '\'') {
        advance();
        return scan_string(c);
    }

    if (c == '`') {
        advance();
        return scan_template_start();
    }

    if (std::isalpha(c) || c == '_' || c == '$') {
        return scan_identifier();
    }

    // Punctuation and Operators
    advance();
    JsToken t;
    t.line = m_line;
    t.text = std::string(1, c);

    switch (c) {
        case '(': t.type = JsTokenType::OpenParen; return t;
        case ')': t.type = JsTokenType::CloseParen; return t;
        case '{':
            m_brace_depth++;
            t.type = JsTokenType::OpenBrace;
            return t;
        case '}':
            if (!m_template_brace_stack.empty() && m_brace_depth == m_template_brace_stack.back()) {
                m_template_brace_stack.pop_back();
                return scan_template_continuation();
            }
            if (m_brace_depth > 0) m_brace_depth--;
            t.type = JsTokenType::CloseBrace;
            return t;
        case '[': t.type = JsTokenType::OpenBracket; return t;
        case ']': t.type = JsTokenType::CloseBracket; return t;
        case ';': t.type = JsTokenType::Semicolon; return t;
        case ':': t.type = JsTokenType::Colon; return t;
        case ',': t.type = JsTokenType::Comma; return t;
        case '.': t.type = JsTokenType::Dot; return t;
        case '+': t.type = JsTokenType::Plus; return t;
        case '-': t.type = JsTokenType::Minus; return t;
        case '*': t.type = JsTokenType::Star; return t;
        case '/': t.type = JsTokenType::Slash; return t;
        case '%': t.type = JsTokenType::Percent; return t;
        case '=':
            if (match('=')) {
                t.type = JsTokenType::EqualEqual;
                t.text = "==";
                if (match('=')) t.text = "==="; // handle ===
            } else if (match('>')) {
                t.type = JsTokenType::Arrow;
                t.text = "=>";
            } else {
                t.type = JsTokenType::Equal;
            }
            return t;
        case '!':
            if (match('=')) {
                t.type = JsTokenType::NotEqual;
                t.text = "!=";
                if (match('=')) t.text = "!==";
            } else {
                t.type = JsTokenType::Bang;
            }
            return t;
        case '<':
            if (match('=')) {
                t.type = JsTokenType::LessEqual;
                t.text = "<=";
            } else {
                t.type = JsTokenType::Less;
            }
            return t;
        case '>':
            if (match('=')) {
                t.type = JsTokenType::GreaterEqual;
                t.text = ">=";
            } else {
                t.type = JsTokenType::Greater;
            }
            return t;
        case '&':
            if (match('&')) {
                t.type = JsTokenType::AmpAmp;
                t.text = "&&";
                return t;
            }
            break;
        case '|':
            if (match('|')) {
                t.type = JsTokenType::PipePipe;
                t.text = "||";
                return t;
            }
            break;
    }

    // Default fallback
    t.type = JsTokenType::Eof;
    return t;
}

std::vector<JsToken> JsLexer::tokenize_all() {
    std::vector<JsToken> tokens;
    while (true) {
        auto t = next_token();
        tokens.push_back(t);
        if (t.type == JsTokenType::Eof) break;
    }
    return tokens;
}

} // namespace litebird
