#include "litebird/js/JsLexer.hpp"
#include "litebird/js/JsIdentifierTables.hpp"
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace litebird {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_hex_digit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
int hex_value(char c) {
    if (is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}
bool is_ascii_ident_start(unsigned char u) {
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_' || u == '$';
}
bool is_ascii_ident_part(unsigned char u) {
    return is_ascii_ident_start(u) || (u >= '0' && u <= '9');
}

// Decodes one well-formed UTF-8 code point at src[offset]. Returns its byte
// length (1..4) and writes the code point to `cp`, or returns 0 on invalid UTF-8.
size_t decode_utf8(std::string_view src, size_t offset, uint32_t& cp) {
    if (offset >= src.size()) return 0;
    unsigned char b0 = static_cast<unsigned char>(src[offset]);
    if (b0 < 0x80) {
        cp = b0;
        return 1;
    }
    if ((b0 & 0xE0) == 0xC0) {
        if (offset + 1 >= src.size()) return 0;
        unsigned char b1 = static_cast<unsigned char>(src[offset + 1]);
        if ((b1 & 0xC0) != 0x80) return 0;
        uint32_t v = ((b0 & 0x1Fu) << 6) | (b1 & 0x3Fu);
        if (v < 0x80u) return 0;
        cp = v;
        return 2;
    }
    if ((b0 & 0xF0) == 0xE0) {
        if (offset + 2 >= src.size()) return 0;
        unsigned char b1 = static_cast<unsigned char>(src[offset + 1]);
        unsigned char b2 = static_cast<unsigned char>(src[offset + 2]);
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return 0;
        uint32_t v = ((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu);
        if (v < 0x800u || (v >= 0xD800u && v <= 0xDFFFu)) return 0;
        cp = v;
        return 3;
    }
    if ((b0 & 0xF8) == 0xF0) {
        if (offset + 3 >= src.size()) return 0;
        unsigned char b1 = static_cast<unsigned char>(src[offset + 1]);
        unsigned char b2 = static_cast<unsigned char>(src[offset + 2]);
        unsigned char b3 = static_cast<unsigned char>(src[offset + 3]);
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return 0;
        uint32_t v = ((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12) |
                     ((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu);
        if (v < 0x10000u || v > 0x10FFFFu) return 0;
        cp = v;
        return 4;
    }
    return 0;
}

bool is_unicode_whitespace(uint32_t cp) {
    return cp == 0x00A0u || cp == 0x1680u || (cp >= 0x2000u && cp <= 0x200Au) ||
           cp == 0x202Fu || cp == 0x205Fu || cp == 0x3000u || cp == 0xFEFFu;
}

bool is_unicode_line_terminator(uint32_t cp) {
    return cp == 0x2028u || cp == 0x2029u;
}

const std::unordered_map<std::string, JsTokenType>& keyword_table() {
    static const std::unordered_map<std::string, JsTokenType> keywords = {
        {"let", JsTokenType::Let},
        {"var", JsTokenType::Var},
        {"const", JsTokenType::Const},
        {"function", JsTokenType::Function},
        {"return", JsTokenType::Return},
        {"if", JsTokenType::If},
        {"else", JsTokenType::Else},
        {"while", JsTokenType::While},
        {"do", JsTokenType::Do},
        {"for", JsTokenType::For},
        {"in", JsTokenType::In},
        {"new", JsTokenType::New},
        {"true", JsTokenType::True},
        {"false", JsTokenType::False},
        {"null", JsTokenType::Null},
        {"typeof", JsTokenType::Typeof},
        {"instanceof", JsTokenType::InstanceOf},
        {"switch", JsTokenType::Switch},
        {"case", JsTokenType::Case},
        {"default", JsTokenType::Default},
        {"break", JsTokenType::Break},
        {"continue", JsTokenType::Continue},
        {"class", JsTokenType::Class},
        {"extends", JsTokenType::Extends},
        {"super", JsTokenType::Super},
        {"this", JsTokenType::This},
        {"await", JsTokenType::Await},
        {"yield", JsTokenType::Yield},
        {"try", JsTokenType::Try},
        {"catch", JsTokenType::Catch},
        {"finally", JsTokenType::Finally},
        {"throw", JsTokenType::Throw},
        {"delete", JsTokenType::Delete},
        {"void", JsTokenType::Void},
        {"debugger", JsTokenType::Debugger},
        {"with", JsTokenType::With},
        {"import", JsTokenType::Import},
        {"export", JsTokenType::Export},
        {"enum", JsTokenType::Enum},
    };
    return keywords;
}

} // namespace

JsLexer::JsLexer(std::string_view source) : m_source(source) {
    // A hashbang line is only legal at the very start of the source.
    if (m_source.size() >= 2 && m_source[0] == '#' && m_source[1] == '!') {
        while (m_cursor < m_source.size()) {
            char c = m_source[m_cursor];
            if (c == '\n' || c == '\r') break;
            if (static_cast<unsigned char>(c) >= 0x80) {
                uint32_t cp = 0;
                size_t len = decode_utf8(m_source, m_cursor, cp);
                if (len > 0 && is_unicode_line_terminator(cp)) break;
            }
            m_cursor++;
        }
    }
}

void JsLexer::fail(const std::string& message) const {
    throw JsSyntaxError(message, m_line);
}

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
    if (m_cursor < m_source.size() && m_source[m_cursor] == expected) {
        advance();
        return true;
    }
    return false;
}

void JsLexer::skip_whitespace_and_comments() {
    auto at_line_terminator = [&]() {
        char ch = peek();
        if (ch == '\n' || ch == '\r') return true;
        if (static_cast<unsigned char>(ch) >= 0x80) {
            uint32_t cp = 0;
            size_t len = decode_utf8(m_source, m_cursor, cp);
            return len > 0 && is_unicode_line_terminator(cp);
        }
        return false;
    };

    while (m_cursor < m_source.size()) {
        unsigned char c = static_cast<unsigned char>(peek());
        if (c == '\n' || c == '\r') {
            m_newline_before = true;
            advance();
        } else if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
            advance();
        } else if (c >= 0x80) {
            uint32_t cp = 0;
            size_t len = decode_utf8(m_source, m_cursor, cp);
            if (len > 0 && is_unicode_whitespace(cp)) {
                m_cursor += len;
            } else if (len > 0 && is_unicode_line_terminator(cp)) {
                m_cursor += len;
                m_newline_before = true;
            } else {
                break;
            }
        } else if (c == '/' && peek(1) == '/') {
            while (m_cursor < m_source.size() && !at_line_terminator()) advance();
        } else if (c == '/' && peek(1) == '*') {
            advance(); advance();
            bool closed = false;
            while (m_cursor < m_source.size()) {
                if (peek() == '*' && peek(1) == '/') {
                    advance(); advance();
                    closed = true;
                    break;
                }
                if (at_line_terminator()) m_newline_before = true;
                advance();
            }
            if (!closed) fail("Unterminated comment");
        } else if (c == '<' && peek(1) == '!' && peek(2) == '-' && peek(3) == '-') {
            // HTML-like comment opener: legacy pages still wrap scripts in these.
            while (m_cursor < m_source.size() && !at_line_terminator()) advance();
        } else if (c == '-' && peek(1) == '-' && peek(2) == '>' && (m_newline_before || !m_has_prev)) {
            // HTML-like comment closer, only at the start of a line.
            while (m_cursor < m_source.size() && !at_line_terminator()) advance();
        } else {
            break;
        }
    }
}

// Decides whether a '/' starts a regular expression or is a division sign. The
// grammar is context dependent; the standard approximation is to look at the
// previous token: after something that ends an expression it is division.
bool JsLexer::regex_allowed() const {
    if (!m_has_prev) return true;
    switch (m_prev_type) {
        case JsTokenType::Number:
        case JsTokenType::String:
        case JsTokenType::Identifier:
        case JsTokenType::EscapedKeyword:
        case JsTokenType::Regex:
        case JsTokenType::PrivateName:
        case JsTokenType::NoSubstitutionTemplate:
        case JsTokenType::TemplateTail:
        case JsTokenType::CloseParen:
        case JsTokenType::CloseBracket:
        case JsTokenType::PlusPlus:
        case JsTokenType::MinusMinus:
        case JsTokenType::This:
        case JsTokenType::Super:
        case JsTokenType::True:
        case JsTokenType::False:
        case JsTokenType::Null:
        case JsTokenType::Undefined:
            return false;
        default:
            return true;
    }
}

void JsLexer::append_code_point(std::string& out, unsigned long cp) {
    // Generalised UTF-8: lone surrogates are encoded as three bytes so that a
    // string built from "\uD83D" survives until its other half is appended.
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Called with the cursor just past a backslash.
void JsLexer::scan_escape(std::string& cooked, bool in_template) {
    if (m_cursor >= m_source.size()) fail("Invalid or unexpected token");
    if (static_cast<unsigned char>(peek()) >= 0x80) {
        uint32_t cp = 0;
        size_t len = decode_utf8(m_source, m_cursor, cp);
        if (len > 0 && is_unicode_line_terminator(cp)) {
            m_cursor += len;                   // line continuation (U+2028 / U+2029)
            return;
        }
    }
    char esc = advance();
    switch (esc) {
        case 'n': cooked += '\n'; return;
        case 't': cooked += '\t'; return;
        case 'r': cooked += '\r'; return;
        case 'b': cooked += '\b'; return;
        case 'f': cooked += '\f'; return;
        case 'v': cooked += '\v'; return;
        case '\r':
            if (peek() == '\n') advance();     // line continuation, CRLF
            return;
        case '\n':
            return;                            // line continuation
        case 'x': {
            if (!is_hex_digit(peek()) || !is_hex_digit(peek(1))) fail("Invalid hexadecimal escape sequence");
            unsigned long v = static_cast<unsigned long>(hex_value(advance())) * 16;
            v += static_cast<unsigned long>(hex_value(advance()));
            append_code_point(cooked, v);
            return;
        }
        case 'u': {
            unsigned long cp = 0;
            if (peek() == '{') {
                advance();
                bool has_digit = false;
                while (is_hex_digit(peek())) {
                    has_digit = true;
                    cp = cp * 16 + static_cast<unsigned long>(hex_value(advance()));
                    if (cp > 0x10FFFF) fail("Undefined Unicode code-point");
                }
                if (!has_digit || peek() != '}') fail("Invalid Unicode escape sequence");
                advance();
            } else {
                for (int i = 0; i < 4; ++i) {
                    if (!is_hex_digit(peek())) fail("Invalid Unicode escape sequence");
                    cp = cp * 16 + static_cast<unsigned long>(hex_value(advance()));
                }
                // A high surrogate escape followed by a low surrogate escape is
                // one code point; join them so the string is valid UTF-8.
                if (cp >= 0xD800 && cp <= 0xDBFF && peek() == '\\' && peek(1) == 'u' &&
                    is_hex_digit(peek(2)) && is_hex_digit(peek(3)) && is_hex_digit(peek(4)) && is_hex_digit(peek(5))) {
                    unsigned long low = 0;
                    for (int i = 2; i < 6; ++i) low = low * 16 + static_cast<unsigned long>(hex_value(peek(static_cast<size_t>(i))));
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        m_cursor += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                }
            }
            append_code_point(cooked, cp);
            return;
        }
        default:
            break;
    }
    if (esc >= '0' && esc <= '7') {
        // "\0" is NUL; longer forms are legacy octal escapes.
        unsigned long v = static_cast<unsigned long>(esc - '0');
        if (!in_template) {
            int max_more = (esc <= '3') ? 2 : 1;
            while (max_more-- > 0 && peek() >= '0' && peek() <= '7') v = v * 8 + static_cast<unsigned long>(advance() - '0');
        }
        append_code_point(cooked, v);
        return;
    }
    cooked += esc;      // \' \" \\ \` \$ and every other character stand for themselves
}

JsToken JsLexer::scan_number() {
    JsToken t;
    t.type = JsTokenType::Number;
    t.line = m_line;
    size_t start = m_cursor;

    auto read_radix = [&](int radix) {
        m_cursor += 2;
        double value = 0.0;
        int digits = 0;
        while (true) {
            char c = peek();
            if (c == '_') { advance(); continue; }
            int d = -1;
            if (is_hex_digit(c)) d = hex_value(c);
            if (d < 0 || d >= radix) break;
            value = value * radix + d;
            digits++;
            advance();
        }
        if (digits == 0) fail("Invalid or unexpected token");
        return value;
    };

    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        t.number_value = read_radix(16);
    } else if (peek() == '0' && (peek(1) == 'o' || peek(1) == 'O')) {
        t.number_value = read_radix(8);
    } else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
        t.number_value = read_radix(2);
    } else {
        std::string digits;
        bool is_float = false;
        while (is_digit(peek()) || peek() == '_') {
            char c = advance();
            if (c != '_') digits += c;
        }
        if (peek() == '.') {
            is_float = true;
            digits += advance();
            while (is_digit(peek()) || peek() == '_') {
                char c = advance();
                if (c != '_') digits += c;
            }
        }
        if ((peek() == 'e' || peek() == 'E') &&
            (is_digit(peek(1)) || ((peek(1) == '+' || peek(1) == '-') && is_digit(peek(2))))) {
            is_float = true;
            digits += advance();
            if (peek() == '+' || peek() == '-') digits += advance();
            while (is_digit(peek())) digits += advance();
        }
        // Legacy octal: a leading zero followed only by octal digits.
        bool legacy_octal = !is_float && digits.size() > 1 && digits[0] == '0';
        if (legacy_octal) {
            for (char c : digits) if (c > '7') legacy_octal = false;
        }
        if (legacy_octal) {
            double value = 0.0;
            for (char c : digits) value = value * 8 + (c - '0');
            t.number_value = value;
        } else {
            t.number_value = std::strtod(digits.c_str(), nullptr);
        }
    }

    if (peek() == 'n') fail("BigInt literals are not supported");
    if (starts_identifier_at(m_cursor) || is_digit(peek())) fail("Invalid or unexpected token");
    t.text = std::string(m_source.substr(start, m_cursor - start));
    return t;
}

JsToken JsLexer::scan_string(char quote) {
    JsToken t;
    t.type = JsTokenType::String;
    t.line = m_line;
    std::string result;
    while (true) {
        if (m_cursor >= m_source.size()) fail("Unterminated string literal");
        char c = peek();
        if (c == quote) {
            advance();
            break;
        }
        if (c == '\n' || c == '\r') fail("Unterminated string literal");
        if (c == '\\') {
            advance();
            scan_escape(result, false);
        } else {
            result += advance();
        }
    }
    t.text = std::move(result);
    return t;
}

bool JsLexer::starts_identifier_at(size_t pos) const {
    if (pos >= m_source.size()) return false;
    unsigned char c = static_cast<unsigned char>(m_source[pos]);
    if (c < 0x80) return is_ascii_ident_start(c) || c == '\\';
    uint32_t cp = 0;
    size_t len = decode_utf8(m_source, pos, cp);
    return len > 0 && is_js_id_start(cp);
}

uint32_t JsLexer::scan_identifier_unicode_escape() {
    advance(); // consume '\\'
    if (peek() != 'u') fail("Invalid or unexpected token");
    advance(); // consume 'u'
    uint32_t cp = 0;
    if (peek() == '{') {
        advance();
        bool has_digit = false;
        while (is_hex_digit(peek())) {
            has_digit = true;
            cp = cp * 16u + static_cast<uint32_t>(hex_value(advance()));
            if (cp > 0x10FFFFu) fail("Undefined Unicode code-point");
        }
        if (!has_digit || peek() != '}') fail("Invalid Unicode escape sequence");
        advance();
    } else {
        for (int i = 0; i < 4; ++i) {
            if (!is_hex_digit(peek())) fail("Invalid Unicode escape sequence");
            cp = cp * 16u + static_cast<uint32_t>(hex_value(advance()));
        }
    }
    return cp;
}

JsToken JsLexer::scan_identifier() {
    size_t start = m_cursor;
    std::string text;
    bool has_escape = false;
    bool slow_path = false;

    // Fast path for the common case: pure ASCII identifier with no escapes.
    unsigned char first = static_cast<unsigned char>(peek());
    if (first < 0x80 && is_ascii_ident_start(first)) {
        advance();
        while (m_cursor < m_source.size()) {
            unsigned char c = static_cast<unsigned char>(peek());
            if (c < 0x80 && is_ascii_ident_part(c)) {
                advance();
            } else if (c == '\\' || c >= 0x80) {
                text.assign(m_source.substr(start, m_cursor - start));
                slow_path = true;
                break;
            } else {
                break;
            }
        }
    } else {
        slow_path = true;
        if (first == '\\') {
            uint32_t cp = scan_identifier_unicode_escape();
            if (!is_js_id_start(cp)) fail("Invalid or unexpected token");
            append_code_point(text, cp);
            has_escape = true;
        } else {
            uint32_t cp = 0;
            size_t len = decode_utf8(m_source, m_cursor, cp);
            if (len == 0 || !is_js_id_start(cp)) fail("Invalid or unexpected token");
            text.append(m_source.substr(m_cursor, len));
            m_cursor += len;
        }
    }

    if (slow_path) {
        while (m_cursor < m_source.size()) {
            unsigned char c = static_cast<unsigned char>(peek());
            if (c < 0x80) {
                if (is_ascii_ident_part(c)) {
                    text += advance();
                    continue;
                }
                if (c == '\\') {
                    uint32_t cp = scan_identifier_unicode_escape();
                    if (!is_js_id_continue(cp)) fail("Invalid or unexpected token");
                    append_code_point(text, cp);
                    has_escape = true;
                    continue;
                }
                break;
            }
            uint32_t cp = 0;
            size_t len = decode_utf8(m_source, m_cursor, cp);
            if (len == 0 || !is_js_id_continue(cp)) break;
            text.append(m_source.substr(m_cursor, len));
            m_cursor += len;
        }
    } else {
        text.assign(m_source.substr(start, m_cursor - start));
    }

    JsToken t;
    t.line = m_line;
    t.text = std::move(text);
    t.has_escape = has_escape;
    const auto& keywords = keyword_table();
    auto it = keywords.find(t.text);
    if (it != keywords.end()) {
        if (!has_escape) {
            t.type = it->second;
            t.is_keyword = true;
        } else if (it->second == JsTokenType::Let ||
                   it->second == JsTokenType::Yield ||
                   it->second == JsTokenType::Await) {
            // Contextually reserved words with escapes never act as keywords,
            // but remain valid identifiers outside contexts that reserve them.
            t.type = JsTokenType::Identifier;
        } else {
            // Unconditionally reserved words with escapes are valid only as
            // IdentifierName (property / member names), never as keywords or
            // BindingIdentifier / IdentifierReference / LabelIdentifier.
            t.type = JsTokenType::EscapedKeyword;
            t.is_keyword = true;
        }
    } else {
        t.type = JsTokenType::Identifier;
    }
    return t;
}

// Called with the cursor just past the opening '/'.
JsToken JsLexer::scan_regex() {
    JsToken t;
    t.type = JsTokenType::Regex;
    t.line = m_line;
    std::string pattern;
    bool in_class = false;
    auto at_line_terminator = [&]() {
        char ch = peek();
        if (ch == '\n' || ch == '\r') return true;
        if (static_cast<unsigned char>(ch) >= 0x80) {
            uint32_t cp = 0;
            size_t len = decode_utf8(m_source, m_cursor, cp);
            return len > 0 && is_unicode_line_terminator(cp);
        }
        return false;
    };
    while (true) {
        if (m_cursor >= m_source.size()) fail("Invalid regular expression: missing /");
        char c = peek();
        if (at_line_terminator()) fail("Invalid regular expression: missing /");
        if (c == '\\') {
            pattern += advance();
            if (m_cursor >= m_source.size() || at_line_terminator()) fail("Invalid regular expression: missing /");
            pattern += advance();
            continue;
        }
        if (c == '[') in_class = true;
        else if (c == ']') in_class = false;
        else if (c == '/' && !in_class) {
            advance();
            break;
        }
        pattern += advance();
    }
    std::string flags;
    while (m_cursor < m_source.size()) {
        unsigned char fc = static_cast<unsigned char>(peek());
        if (fc < 0x80) {
            if (!is_ascii_ident_part(fc)) break;
            flags += advance();
        } else {
            uint32_t cp = 0;
            size_t len = decode_utf8(m_source, m_cursor, cp);
            if (len == 0 || !is_js_id_continue(cp)) break;
            flags.append(m_source.substr(m_cursor, len));
            m_cursor += len;
        }
    }
    t.text = std::move(pattern);
    t.flags = std::move(flags);
    return t;
}

// Scans template characters up to the next "${" or the closing backtick.
// `is_start` says whether this follows the opening backtick or a "}".
JsToken JsLexer::scan_template(bool is_start) {
    JsToken t;
    t.line = m_line;
    std::string cooked;
    std::string raw;

    while (true) {
        if (m_cursor >= m_source.size()) fail("Unterminated template literal");
        char ch = peek();
        if (ch == '`') {
            advance();
            t.type = is_start ? JsTokenType::NoSubstitutionTemplate : JsTokenType::TemplateTail;
            break;
        }
        if (ch == '$' && peek(1) == '{') {
            advance(); advance();
            m_template_brace_stack.push_back(m_brace_depth);
            t.type = is_start ? JsTokenType::TemplateHead : JsTokenType::TemplateMiddle;
            break;
        }
        if (ch == '\\') {
            size_t esc_start = m_cursor;
            advance();
            scan_escape(cooked, true);
            raw.append(m_source.substr(esc_start, m_cursor - esc_start));
        } else if (ch == '\r') {
            // Line terminators in templates are normalised to LF.
            advance();
            if (peek() == '\n') advance();
            cooked += '\n';
            raw += '\n';
        } else {
            char c = advance();
            cooked += c;
            raw += c;
        }
    }
    t.text = std::move(cooked);
    t.raw = std::move(raw);
    return t;
}

JsToken JsLexer::next_token() {
    JsToken t = scan_token();
    t.start = static_cast<uint32_t>(m_token_start);
    t.end = static_cast<uint32_t>(m_cursor);
    return t;
}

JsToken JsLexer::scan_token() {
    m_newline_before = false;
    skip_whitespace_and_comments();
    m_token_start = m_cursor;

    JsToken t;
    t.line = m_line;

    auto finish = [&](JsToken tok) {
        tok.newline_before = m_newline_before;
        m_has_prev = true;
        m_prev_type = tok.type;
        m_prev_is_keyword = tok.is_keyword;
        return tok;
    };

    if (m_cursor >= m_source.size()) {
        t.type = JsTokenType::Eof;
        t.newline_before = m_newline_before;
        return t;
    }

    char c = peek();

    if (is_digit(c) || (c == '.' && is_digit(peek(1)))) return finish(scan_number());

    if (c == '"' || c == '\'') {
        advance();
        return finish(scan_string(c));
    }

    if (c == '`') {
        advance();
        return finish(scan_template(true));
    }

    if (starts_identifier_at(m_cursor)) return finish(scan_identifier());

    if (c == '#' && starts_identifier_at(m_cursor + 1)) {
        advance();
        JsToken name = scan_identifier();
        name.type = JsTokenType::PrivateName;
        name.is_keyword = false;
        name.text = "#" + name.text;
        return finish(name);
    }

    if (c == '/' && regex_allowed()) {
        advance();
        return finish(scan_regex());
    }

    advance();
    t.text = std::string(1, c);
    auto op = [&](JsTokenType type, const char* text) {
        t.type = type;
        t.text = text;
        return finish(t);
    };

    switch (c) {
        case '(': return op(JsTokenType::OpenParen, "(");
        case ')': return op(JsTokenType::CloseParen, ")");
        case '{':
            m_brace_depth++;
            return op(JsTokenType::OpenBrace, "{");
        case '}':
            if (!m_template_brace_stack.empty() && m_brace_depth == m_template_brace_stack.back()) {
                m_template_brace_stack.pop_back();
                return finish(scan_template(false));
            }
            if (m_brace_depth > 0) m_brace_depth--;
            return op(JsTokenType::CloseBrace, "}");
        case '[': return op(JsTokenType::OpenBracket, "[");
        case ']': return op(JsTokenType::CloseBracket, "]");
        case ';': return op(JsTokenType::Semicolon, ";");
        case ':': return op(JsTokenType::Colon, ":");
        case ',': return op(JsTokenType::Comma, ",");
        case '~': return op(JsTokenType::Tilde, "~");
        case '.':
            if (peek() == '.' && peek(1) == '.') {
                advance(); advance();
                return op(JsTokenType::Ellipsis, "...");
            }
            return op(JsTokenType::Dot, ".");
        // Longest match first: `+=` must win over `+`, `++` over `+`.
        case '+':
            if (match('=')) return op(JsTokenType::PlusEqual, "+=");
            if (match('+')) return op(JsTokenType::PlusPlus, "++");
            return op(JsTokenType::Plus, "+");
        case '-':
            if (match('=')) return op(JsTokenType::MinusEqual, "-=");
            if (match('-')) return op(JsTokenType::MinusMinus, "--");
            return op(JsTokenType::Minus, "-");
        case '*':
            if (match('*')) {
                if (match('=')) return op(JsTokenType::PowerEqual, "**=");
                return op(JsTokenType::Power, "**");
            }
            if (match('=')) return op(JsTokenType::StarEqual, "*=");
            return op(JsTokenType::Star, "*");
        case '/':
            if (match('=')) return op(JsTokenType::SlashEqual, "/=");
            return op(JsTokenType::Slash, "/");
        case '%':
            if (match('=')) return op(JsTokenType::PercentEqual, "%=");
            return op(JsTokenType::Percent, "%");
        case '=':
            if (match('=')) {
                // `===` is strict equality and MUST stay distinct from `==`.
                if (match('=')) return op(JsTokenType::StrictEqual, "===");
                return op(JsTokenType::EqualEqual, "==");
            }
            if (match('>')) return op(JsTokenType::Arrow, "=>");
            return op(JsTokenType::Equal, "=");
        case '!':
            if (match('=')) {
                if (match('=')) return op(JsTokenType::StrictNotEqual, "!==");
                return op(JsTokenType::NotEqual, "!=");
            }
            return op(JsTokenType::Bang, "!");
        case '<':
            if (match('<')) {
                if (match('=')) return op(JsTokenType::ShiftLeftEqual, "<<=");
                return op(JsTokenType::ShiftLeft, "<<");
            }
            if (match('=')) return op(JsTokenType::LessEqual, "<=");
            return op(JsTokenType::Less, "<");
        case '>':
            if (match('>')) {
                if (match('>')) {
                    if (match('=')) return op(JsTokenType::ShiftRightUnsignedEqual, ">>>=");
                    return op(JsTokenType::ShiftRightUnsigned, ">>>");
                }
                if (match('=')) return op(JsTokenType::ShiftRightEqual, ">>=");
                return op(JsTokenType::ShiftRight, ">>");
            }
            if (match('=')) return op(JsTokenType::GreaterEqual, ">=");
            return op(JsTokenType::Greater, ">");
        case '&':
            if (match('&')) {
                if (match('=')) return op(JsTokenType::AmpAmpEqual, "&&=");
                return op(JsTokenType::AmpAmp, "&&");
            }
            if (match('=')) return op(JsTokenType::AmpEqual, "&=");
            return op(JsTokenType::Amp, "&");
        case '|':
            if (match('|')) {
                if (match('=')) return op(JsTokenType::PipePipeEqual, "||=");
                return op(JsTokenType::PipePipe, "||");
            }
            if (match('=')) return op(JsTokenType::PipeEqual, "|=");
            return op(JsTokenType::Pipe, "|");
        case '^':
            if (match('=')) return op(JsTokenType::CaretEqual, "^=");
            return op(JsTokenType::Caret, "^");
        case '?':
            if (match('?')) {
                if (match('=')) return op(JsTokenType::NullishEqual, "?\?=");
                return op(JsTokenType::Nullish, "??");
            }
            // `a?.5:1` is a conditional with a decimal, not optional chaining.
            if (peek() == '.' && !is_digit(peek(1))) {
                advance();
                return op(JsTokenType::QuestionDot, "?.");
            }
            return op(JsTokenType::Question, "?");
        default:
            break;
    }

    // An unknown character used to become end-of-file here, which silently
    // dropped the rest of the script. It is a syntax error.
    fail("Invalid or unexpected token");
}

std::vector<JsToken> JsLexer::tokenize_all() {
    std::vector<JsToken> tokens;
    while (true) {
        auto t = next_token();
        bool done = t.type == JsTokenType::Eof;
        tokens.push_back(std::move(t));
        if (done) break;
    }
    return tokens;
}

} // namespace litebird
