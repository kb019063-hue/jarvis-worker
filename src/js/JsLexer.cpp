#include "litebird/js/JsLexer.hpp"
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
bool is_ident_start(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_' || u == '$' || u >= 0x80;
}
bool is_ident_part(char c) { return is_ident_start(c) || is_digit(c); }

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
        {"undefined", JsTokenType::Undefined},
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
        while (m_cursor < m_source.size() && m_source[m_cursor] != '\n') m_cursor++;
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
    while (m_cursor < m_source.size()) {
        unsigned char c = static_cast<unsigned char>(peek());
        if (c == '\n' || c == '\r') {
            m_newline_before = true;
            advance();
        } else if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
            advance();
        } else if (c == 0xC2 && static_cast<unsigned char>(peek(1)) == 0xA0) {
            m_cursor += 2;                       // U+00A0 no-break space
        } else if (c == 0xEF && static_cast<unsigned char>(peek(1)) == 0xBB &&
                   static_cast<unsigned char>(peek(2)) == 0xBF) {
            m_cursor += 3;                       // U+FEFF byte order mark
        } else if (c == 0xE2 && static_cast<unsigned char>(peek(1)) == 0x80 &&
                   (static_cast<unsigned char>(peek(2)) == 0xA8 ||
                    static_cast<unsigned char>(peek(2)) == 0xA9)) {
            m_cursor += 3;                       // U+2028 / U+2029 line terminators
            m_newline_before = true;
        } else if (c == '/' && peek(1) == '/') {
            while (m_cursor < m_source.size() && peek() != '\n') advance();
        } else if (c == '/' && peek(1) == '*') {
            advance(); advance();
            bool closed = false;
            while (m_cursor < m_source.size()) {
                if (peek() == '*' && peek(1) == '/') {
                    advance(); advance();
                    closed = true;
                    break;
                }
                if (peek() == '\n') m_newline_before = true;
                advance();
            }
            if (!closed) fail("Unterminated comment");
        } else if (c == '<' && peek(1) == '!' && peek(2) == '-' && peek(3) == '-') {
            // HTML-like comment opener: legacy pages still wrap scripts in these.
            while (m_cursor < m_source.size() && peek() != '\n') advance();
        } else if (c == '-' && peek(1) == '-' && peek(2) == '>' && (m_newline_before || !m_has_prev)) {
            // HTML-like comment closer, only at the start of a line.
            while (m_cursor < m_source.size() && peek() != '\n') advance();
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
                int digits = 0;
                while (is_hex_digit(peek())) {
                    cp = cp * 16 + static_cast<unsigned long>(hex_value(advance()));
                    if (++digits > 6 || cp > 0x10FFFF) fail("Undefined Unicode code-point");
                }
                if (digits == 0 || peek() != '}') fail("Invalid Unicode escape sequence");
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
    if (is_ident_start(peek()) || is_digit(peek())) fail("Invalid or unexpected token");
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

JsToken JsLexer::scan_identifier() {
    size_t start = m_cursor;
    while (m_cursor < m_source.size() && is_ident_part(peek())) {
        unsigned char c = static_cast<unsigned char>(peek());
        // Unicode white space is made of bytes >= 0x80 too; stop before it.
        if (c == 0xC2 && static_cast<unsigned char>(peek(1)) == 0xA0) break;
        if (c == 0xE2 && static_cast<unsigned char>(peek(1)) == 0x80 &&
            (static_cast<unsigned char>(peek(2)) == 0xA8 || static_cast<unsigned char>(peek(2)) == 0xA9)) break;
        if (c == 0xEF && static_cast<unsigned char>(peek(1)) == 0xBB && static_cast<unsigned char>(peek(2)) == 0xBF) break;
        advance();
    }

    JsToken t;
    t.line = m_line;
    t.text = std::string(m_source.substr(start, m_cursor - start));
    const auto& keywords = keyword_table();
    auto it = keywords.find(t.text);
    if (it != keywords.end()) {
        t.type = it->second;
        t.is_keyword = true;
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
    while (true) {
        if (m_cursor >= m_source.size()) fail("Invalid regular expression: missing /");
        char c = peek();
        if (c == '\n' || c == '\r') fail("Invalid regular expression: missing /");
        if (c == '\\') {
            pattern += advance();
            if (m_cursor >= m_source.size() || peek() == '\n') fail("Invalid regular expression: missing /");
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
    while (is_ident_part(peek())) flags += advance();
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
    m_newline_before = false;
    skip_whitespace_and_comments();

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

    if (is_ident_start(c)) return finish(scan_identifier());

    if (c == '#' && is_ident_start(peek(1))) {
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
