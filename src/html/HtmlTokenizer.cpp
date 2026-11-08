#include "litebird/html/HtmlTokenizer.hpp"
#include "litebird/html/EntityTable.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace litebird {

namespace {

bool is_html_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

bool is_ascii_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_ascii_alnum(char c) {
    return is_ascii_alpha(c) || (c >= '0' && c <= '9');
}

char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string ascii_lowercase(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), ascii_lower);
    return out;
}

/// The WHATWG numeric character reference overrides for the C1 range.
const char32_t kC1Replacements[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

char32_t numeric_reference_value(uint32_t value) {
    if (value == 0 || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        return 0xFFFD;
    }
    if (value >= 0x80 && value <= 0x9F) {
        return kC1Replacements[value - 0x80];
    }
    return static_cast<char32_t>(value);
}

} // namespace

void append_utf8(std::string& out, char32_t codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

HtmlTokenizer::HtmlTokenizer(std::string_view input) : m_input(input) {
    // The spec's input stream preprocessing: normalise newlines up front so
    // every downstream state only has to deal with '\n'.
    if (input.find('\r') != std::string_view::npos) {
        m_storage.reserve(input.size());
        for (std::size_t i = 0; i < input.size(); ++i) {
            if (input[i] == '\r') {
                m_storage.push_back('\n');
                if (i + 1 < input.size() && input[i + 1] == '\n') ++i;
            } else {
                m_storage.push_back(input[i]);
            }
        }
        m_input = m_storage;
    }
}

bool HtmlTokenizer::starts_with_ci(std::string_view text) const {
    if (m_cursor + text.size() > m_input.size()) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (ascii_lower(m_input[m_cursor + i]) != ascii_lower(text[i])) return false;
    }
    return true;
}

void HtmlTokenizer::skip_whitespace() {
    while (!eof() && is_html_whitespace(peek())) advance();
}

void HtmlTokenizer::set_content_model(ContentModel model, std::string tag_name) {
    m_content_model = model;
    m_text_tag = ascii_lowercase(tag_name);
}

std::string HtmlTokenizer::consume_numeric_char_ref(bool& ok) {
    ok = false;
    // The '&#' has already been consumed.
    bool hex = false;
    if (peek() == 'x' || peek() == 'X') {
        hex = true;
        advance();
    }
    std::uint32_t value = 0;
    bool any_digits = false;
    while (!eof()) {
        const char c = peek();
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (hex && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (hex && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else break;
        any_digits = true;
        // Clamp to keep the accumulator well inside 32 bits.
        if (value <= 0x10FFFF) value = value * static_cast<std::uint32_t>(hex ? 16 : 10) + static_cast<std::uint32_t>(digit);
        advance();
    }
    if (!any_digits) {
        // absence-of-digits: the consumed characters stay literal text.
        return hex ? std::string("x") : std::string();
    }
    ok = true;
    if (peek() == ';') advance();
    std::string out;
    append_utf8(out, numeric_reference_value(value));
    return out;
}

std::string HtmlTokenizer::consume_char_ref(bool in_attribute) {
    // The '&' has already been consumed.
    if (eof()) return "&";
    if (peek() == '#') {
        advance();
        bool ok = false;
        std::string decoded = consume_numeric_char_ref(ok);
        if (!ok) return "&#" + decoded;
        return decoded;
    }

    const std::string_view rest = m_input.substr(m_cursor);
    std::size_t consumed = 0;
    std::string decoded = decode_named_character_reference(rest, consumed);
    if (consumed == 0) return "&";

    // Spec rule 8: inside an attribute, a reference without a trailing
    // semicolon followed by '=' or an alphanumeric is left alone.
    const bool has_semicolon = rest[consumed - 1] == ';';
    const char next = (consumed < rest.size()) ? rest[consumed] : '\0';
    if (in_attribute && !has_semicolon && (next == '=' || is_ascii_alnum(next))) {
        return "&";
    }

    m_cursor += consumed;
    return decoded;
}

bool HtmlTokenizer::consume_tag(HtmlToken& token) {
    // '<' (and '/' for an end tag) already consumed.
    std::string name;
    while (!eof()) {
        const char c = peek();
        if (is_html_whitespace(c) || c == '/' || c == '>') break;
        name.push_back(ascii_lower(c));
        advance();
    }
    token.name = std::move(name);

    bool expect_value = false;
    std::string attribute_name;
    std::string attribute_value;

    auto flush_attribute = [&]() {
        if (attribute_name.empty()) return;
        const bool duplicate = std::any_of(
            token.attributes.begin(), token.attributes.end(),
            [&](const auto& pair) { return pair.first == attribute_name; });
        if (!duplicate) token.attributes.emplace_back(attribute_name, attribute_value);
        attribute_name.clear();
        attribute_value.clear();
    };

    while (!eof()) {
        if (expect_value) {
            expect_value = false;
            skip_whitespace();
            const char quote = peek();
            if (quote == '"' || quote == '\'') {
                advance();
                while (!eof() && peek() != quote) {
                    if (peek() == '&') { advance(); attribute_value += consume_char_ref(true); }
                    else { attribute_value.push_back(peek()); advance(); }
                }
                if (!eof()) advance();  // closing quote
            } else {
                while (!eof() && !is_html_whitespace(peek()) && peek() != '>') {
                    if (peek() == '&') { advance(); attribute_value += consume_char_ref(true); }
                    else { attribute_value.push_back(peek()); advance(); }
                }
            }
            flush_attribute();
            continue;
        }

        skip_whitespace();
        if (eof()) break;

        const char c = peek();
        if (c == '>') {
            advance();
            return true;
        }
        if (c == '/') {
            advance();
            if (peek() == '>') {
                advance();
                token.self_closing = true;
                return true;
            }
            continue;  // unexpected-solidus-in-tag: reconsume in before-attribute-name
        }

        // Attribute name.
        while (!eof()) {
            const char a = peek();
            if (is_html_whitespace(a) || a == '/' || a == '>' || a == '=') break;
            attribute_name.push_back(ascii_lower(a));
            advance();
        }
        skip_whitespace();
        if (peek() == '=') {
            advance();
            expect_value = true;
        } else {
            // Attribute with no value at all.
            flush_attribute();
        }
    }
    // EOF inside the tag: the token is discarded (eof-in-tag).
    return false;
}

void HtmlTokenizer::consume_comment(HtmlToken& token) {
    // "<!--" already consumed.
    token.type = HtmlTokenType::Comment;
    std::string data;

    if (peek() == '>') {  // <!-->
        advance();
        token.data = std::move(data);
        return;
    }
    if (peek() == '-' && peek(1) == '>') {  // <!--->
        advance();
        advance();
        token.data = std::move(data);
        return;
    }

    while (!eof()) {
        if (peek() == '-') {
            if (peek(1) == '-') {
                if (peek(2) == '>') {
                    m_cursor += 3;
                    token.data = std::move(data);
                    return;
                }
                if (peek(2) == '!' && peek(3) == '>') {
                    m_cursor += 4;
                    token.data = std::move(data);
                    return;
                }
            }
            data.push_back('-');
            advance();
            continue;
        }
        data.push_back(peek());
        advance();
    }
    token.data = std::move(data);
}

void HtmlTokenizer::consume_bogus_comment(HtmlToken& token, std::string_view prefix) {
    token.type = HtmlTokenType::Comment;
    std::string data(prefix);
    while (!eof() && peek() != '>') {
        if (peek() == '\0') {
            // NUL in a comment is replaced by U+FFFD.
            advance();
            data += "\xEF\xBF\xBD";
            continue;
        }
        data.push_back(peek());
        advance();
    }
    if (!eof()) advance();
    token.data = std::move(data);
}

void HtmlTokenizer::consume_doctype(HtmlToken& token) {
    // "<!DOCTYPE" already consumed.
    token.type = HtmlTokenType::Doctype;
    skip_whitespace();

    if (eof()) {
        token.force_quirks = true;
        return;
    }
    if (peek() == '>') {
        advance();
        token.force_quirks = true;
        return;
    }

    // Doctype name.
    std::string name;
    while (!eof() && !is_html_whitespace(peek()) && peek() != '>') {
        name.push_back(ascii_lower(peek()));
        advance();
    }
    token.name = std::move(name);

    skip_whitespace();
    if (eof()) {
        token.force_quirks = true;
        return;
    }
    if (peek() == '>') {
        advance();
        return;
    }

    bool expect_public = false;
    bool expect_system = false;
    if (starts_with_ci("PUBLIC")) {
        m_cursor += 6;
        expect_public = true;
    } else if (starts_with_ci("SYSTEM")) {
        m_cursor += 6;
        expect_system = true;
    } else {
        // Bogus doctype: consume to '>'.
        while (!eof() && peek() != '>') advance();
        if (!eof()) advance();
        token.force_quirks = true;
        return;
    }

    auto read_identifier = [&](std::string& out) {
        skip_whitespace();
        const char quote = peek();
        if (quote != '"' && quote != '\'') return false;
        advance();
        while (!eof() && peek() != quote) {
            out.push_back(peek());
            advance();
        }
        if (!eof()) advance();
        return true;
    };

    if (expect_public) {
        if (!read_identifier(token.public_id)) {
            token.force_quirks = true;
            while (!eof() && peek() != '>') advance();
            if (!eof()) advance();
            return;
        }
        // Optional system identifier after the public one.
        skip_whitespace();
        if (!eof() && peek() != '>') {
            if (peek() == '"' || peek() == '\'') {
                read_identifier(token.system_id);
            }
        }
    } else if (expect_system) {
        if (!read_identifier(token.system_id)) {
            token.force_quirks = true;
            while (!eof() && peek() != '>') advance();
            if (!eof()) advance();
            return;
        }
    }

    while (!eof() && peek() != '>') advance();
    if (!eof()) advance();
}

void HtmlTokenizer::consume_cdata(HtmlToken& token) {
    // "<![CDATA[" already consumed; emit the contents as character data.
    token.type = HtmlTokenType::Character;
    std::string data;
    while (!eof()) {
        if (peek() == ']' && peek(1) == ']' && peek(2) == '>') {
            m_cursor += 3;
            break;
        }
        data.push_back(peek());
        advance();
    }
    token.data = std::move(data);
}

bool HtmlTokenizer::scan_text_run(HtmlToken& token) {
    token.type = HtmlTokenType::Character;
    std::string data;

    if (m_content_model == ContentModel::Plaintext) {
        while (!eof()) {
            if (peek() == '\0') { advance(); data.push_back(0xEF); data.push_back(0xBF); data.push_back(0xBD); continue; }
            data.push_back(peek());
            advance();
        }
        token.data = std::move(data);
        return !token.data.empty();
    }

    const bool decode_entities = (m_content_model == ContentModel::Rcdata);

    while (!eof()) {
        // An appropriate end tag ends the raw text run.
        if (peek() == '<' && peek(1) == '/') {
            const std::size_t after_slash = m_cursor + 2;
            bool matches = after_slash + m_text_tag.size() <= m_input.size();
            if (matches) {
                for (std::size_t i = 0; i < m_text_tag.size(); ++i) {
                    if (ascii_lower(m_input[after_slash + i]) != m_text_tag[i]) { matches = false; break; }
                }
            }
            if (matches) {
                const char terminator = (after_slash + m_text_tag.size() < m_input.size())
                                            ? m_input[after_slash + m_text_tag.size()]
                                            : '\0';
                if (is_html_whitespace(terminator) || terminator == '/' || terminator == '>') {
                    m_content_model = ContentModel::Data;
                    m_text_tag.clear();
                    break;
                }
            }
        }

        if (decode_entities && peek() == '&') {
            advance();
            data += consume_char_ref(false);
            continue;
        }

        if (peek() == '\0') {
            advance();
            append_utf8(data, 0xFFFD);
            continue;
        }
        data.push_back(peek());
        advance();
    }

    token.data = std::move(data);
    // An empty run means we stopped at the end tag: let next_token() parse it.
    return !token.data.empty();
}


// ---------------------------------------------------------------------------
// Script data: the escaped and double-escaped states (WHATWG 13.2.5.20-24)
//
// A plain text-run scan is not enough here: `<script><!--<script </script -->`
// must not end the script, because `</script` inside a double-escaped run only
// leaves the double-escaped state. These states are what make `document.write`
// style inline scripts with HTML comments in them parse the way browsers do.
// ---------------------------------------------------------------------------
bool HtmlTokenizer::scan_script_data(HtmlToken& token) {
    enum class State {
        Data, Lt, EndTagOpen, EndTagName,
        EscapeStart, EscapeStartDash,
        Escaped, EscapedDash, EscapedDashDash,
        EscapedLt, EscapedEndTagOpen, EscapedEndTagName,
        DoubleEscapeStart, DoubleEscaped, DoubleEscapedDash,
        DoubleEscapedDashDash, DoubleEscapedLt, DoubleEscapeEnd
    };

    State state = State::Data;
    std::string data;
    std::string temp;            // tag name / "script" buffer
    std::size_t tag_start = 0;   // cursor of the '<' opening a candidate end tag
    bool ended = false;

    auto is_alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    auto is_ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r'; };
    auto lower = [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); };
    auto append = [&data](char c) { data.push_back(c); };
    auto append_replacement = [&data]() { data += "\xEF\xBF\xBD"; };
    auto is_script = [&temp]() { return !temp.empty(); };
    auto tag_matches = [&]() {
        if (temp.size() != m_text_tag.size()) return false;
        for (std::size_t i = 0; i < temp.size(); ++i) {
            if (lower(temp[i]) != m_text_tag[i]) return false;
        }
        const char term = (m_cursor < m_input.size()) ? m_input[m_cursor] : '\0';
        return is_ws(term) || term == '/' || term == '>';
    };

    while (!eof()) {
        const char c = peek();

        switch (state) {
            case State::Data:
                if (c == '<') { tag_start = m_cursor; advance(); state = State::Lt; }
                else if (c == '\0') { advance(); append_replacement(); }
                else { append(c); advance(); }
                break;

            case State::Lt:
                if (c == '/') { temp.clear(); advance(); state = State::EndTagOpen; }
                else if (c == '!') { append('<'); append('!'); advance(); state = State::EscapeStart; }
                else { append('<'); state = State::Data; }
                break;

            case State::EndTagOpen:
                if (is_alpha(c)) { temp.clear(); state = State::EndTagName; }
                else { append('<'); append('/'); state = State::Data; }
                break;

            case State::EndTagName:
                if (is_alpha(c)) { temp.push_back(c); advance(); }
                else if (tag_matches()) { ended = true; }
                else {
                    append('<'); append('/');
                    for (char t : temp) append(t);
                    temp.clear();
                    state = State::Data;
                }
                break;

            case State::EscapeStart:
                if (c == '-') { append('-'); advance(); state = State::EscapeStartDash; }
                else state = State::Data;
                break;

            case State::EscapeStartDash:
                if (c == '-') { append('-'); advance(); state = State::EscapedDashDash; }
                else state = State::Data;
                break;

            case State::Escaped:
                if (c == '-') { append('-'); advance(); state = State::EscapedDash; }
                else if (c == '<') { tag_start = m_cursor; advance(); state = State::EscapedLt; }
                else if (c == '\0') { advance(); append_replacement(); }
                else { append(c); advance(); }
                break;

            case State::EscapedDash:
                if (c == '-') { append('-'); advance(); state = State::EscapedDashDash; }
                else if (c == '<') { tag_start = m_cursor; advance(); state = State::EscapedLt; }
                else if (c == '\0') { advance(); append_replacement(); state = State::Escaped; }
                else { append(c); advance(); state = State::Escaped; }
                break;

            case State::EscapedDashDash:
                if (c == '-') { append('-'); advance(); }
                else if (c == '<') { tag_start = m_cursor; advance(); state = State::EscapedLt; }
                else if (c == '>') { append('>'); advance(); state = State::Data; }
                else if (c == '\0') { advance(); append_replacement(); state = State::Escaped; }
                else { append(c); advance(); state = State::Escaped; }
                break;

            case State::EscapedLt:
                if (is_alpha(c)) { append('<'); temp.clear(); state = State::DoubleEscapeStart; }
                else if (c == '/') { temp.clear(); advance(); state = State::EscapedEndTagOpen; }
                else { append('<'); state = State::Escaped; }
                break;

            case State::EscapedEndTagOpen:
                if (is_alpha(c)) { temp.clear(); state = State::EscapedEndTagName; }
                else { append('<'); append('/'); state = State::Escaped; }
                break;

            case State::EscapedEndTagName:
                if (is_alpha(c)) { temp.push_back(c); advance(); }
                else if (tag_matches()) { ended = true; }
                else {
                    append('<'); append('/');
                    for (char t : temp) append(t);
                    temp.clear();
                    state = State::Escaped;
                }
                break;

            case State::DoubleEscapeStart:
                if (is_ws(c) || c == '/' || c == '>') {
                    append(c);
                    advance();
                    state = is_script() && temp.size() == 6 ? State::DoubleEscaped : State::Escaped;
                    // Compare case-insensitively against "script".
                    if (state == State::DoubleEscaped) {
                        for (std::size_t i = 0; i < temp.size(); ++i) {
                            if (lower(temp[i]) != "script"[i]) { state = State::Escaped; break; }
                        }
                    }
                } else if (is_alpha(c)) {
                    append(c);
                    temp.push_back(c);
                    advance();
                } else {
                    state = State::Escaped;
                }
                break;

            case State::DoubleEscaped:
                if (c == '-') { append('-'); advance(); state = State::DoubleEscapedDash; }
                else if (c == '<') { append('<'); advance(); state = State::DoubleEscapedLt; }
                else if (c == '\0') { advance(); append_replacement(); }
                else { append(c); advance(); }
                break;

            case State::DoubleEscapedDash:
                if (c == '-') { append('-'); advance(); state = State::DoubleEscapedDashDash; }
                else if (c == '<') { append('<'); advance(); state = State::DoubleEscapedLt; }
                else if (c == '\0') { append_replacement(); advance(); state = State::DoubleEscaped; }
                else { append(c); advance(); state = State::DoubleEscaped; }
                break;

            case State::DoubleEscapedDashDash:
                if (c == '-') { append('-'); advance(); }
                else if (c == '<') { append('<'); advance(); state = State::DoubleEscapedLt; }
                else if (c == '>') { append('>'); advance(); state = State::Data; }
                else if (c == '\0') { append_replacement(); advance(); state = State::DoubleEscaped; }
                else { append(c); advance(); state = State::DoubleEscaped; }
                break;

            case State::DoubleEscapedLt:
                if (c == '/') { append('/'); temp.clear(); advance(); state = State::DoubleEscapeEnd; }
                else { state = State::DoubleEscaped; }
                break;

            case State::DoubleEscapeEnd:
                if (is_ws(c) || c == '/' || c == '>') {
                    append(c);
                    advance();
                    bool is_script_end = temp.size() == 6;
                    if (is_script_end) {
                        for (std::size_t i = 0; i < temp.size(); ++i) {
                            if (lower(temp[i]) != "script"[i]) { is_script_end = false; break; }
                        }
                    }
                    state = is_script_end ? State::Escaped : State::DoubleEscaped;
                } else if (is_alpha(c)) {
                    append(c);
                    temp.push_back(c);
                    advance();
                } else {
                    state = State::DoubleEscaped;
                }
                break;
        }

        if (ended) break;
    }

    if (!ended) {
        // EOF with characters still buffered by a partially recognised
        // construct: flush them as text (spec: "emit ... as character tokens").
        switch (state) {
            case State::Lt: append('<'); break;
            case State::EndTagOpen: append('<'); append('/'); break;
            case State::EndTagName:
                append('<'); append('/');
                for (char t : temp) append(t);
                break;
            case State::EscapedLt: append('<'); break;
            case State::EscapedEndTagOpen: append('<'); append('/'); break;
            case State::EscapedEndTagName:
                append('<'); append('/');
                for (char t : temp) append(t);
                break;
            case State::DoubleEscapeStart:
            case State::DoubleEscapeEnd:
                break;
            default:
                break;
        }
    }

    if (ended) {
        // Hand the end tag back to the main tokenizer.
        m_cursor = tag_start;
        m_content_model = ContentModel::Data;
        m_text_tag.clear();
    }

    token.type = HtmlTokenType::Character;
    token.data = std::move(data);
    return !token.data.empty();
}

HtmlToken HtmlTokenizer::next_token() {
    HtmlToken token;

    if (m_content_model == ContentModel::ScriptData) {
        if (scan_script_data(token)) return token;
    } else if (m_content_model != ContentModel::Data) {
        if (scan_text_run(token)) return token;
    }

    if (eof()) return HtmlToken::make_eof();

    if (peek() != '<') {
        // Character data.
        token.type = HtmlTokenType::Character;
        std::string data;
        while (!eof() && peek() != '<') {
            if (peek() == '&') {
                advance();
                data += consume_char_ref(false);
                continue;
            }
            data.push_back(peek());
            advance();
        }
        token.data = std::move(data);
        return token;
    }

    // Markup. Decide which construct this '<' opens.
    const char next = peek(1);

    if (next == '!') {
        if (m_input.compare(m_cursor, 4, "<!--") == 0) {
            m_cursor += 4;
            consume_comment(token);
            return token;
        }
        if (starts_with_ci("<!DOCTYPE")) {
            m_cursor += 9;
            consume_doctype(token);
            return token;
        }
        if (m_allow_cdata && m_input.compare(m_cursor, 9, "<![CDATA[") == 0) {
            m_cursor += 9;
            consume_cdata(token);
            return token;
        }
        // Bogus comment: the "!" opened the markup declaration and is not part
        // of the comment data (spec: reconsume the character after it).
        m_cursor += 2;  // "<!"
        consume_bogus_comment(token, "");
        return token;
    }

    if (next == '?') {
        advance();  // '<'
        consume_bogus_comment(token, "");
        return token;
    }

    if (next == '/') {
        const char after = peek(2);
        if (is_ascii_alpha(after)) {
            m_cursor += 2;
            token.type = HtmlTokenType::EndTag;
            if (!consume_tag(token)) return HtmlToken::make_eof();
            return token;
        }
        if (after == '>') {
            m_cursor += 3;  // "</>" is dropped entirely
            return next_token();
        }
        if (peek(2) == '\0' && m_cursor + 2 >= m_input.size()) {
            // eof-before-tag-name: "</" is emitted as text.
            m_cursor += 2;
            token.type = HtmlTokenType::Character;
            token.data = "</";
            return token;
        }
        m_cursor += 2;  // "</" + anything else -> bogus comment
        consume_bogus_comment(token, "");
        return token;
    }

    if (is_ascii_alpha(next)) {
        m_cursor += 1;
        token.type = HtmlTokenType::StartTag;
        if (!consume_tag(token)) return HtmlToken::make_eof();
        return token;
    }

    // A lone '<' is literal character data.
    token.type = HtmlTokenType::Character;
    token.data = "<";
    advance();
    return token;
}

} // namespace litebird
