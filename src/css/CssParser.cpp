#include "litebird/css/CssParser.hpp"
#include "litebird/css/CssText.hpp"
#include "litebird/dom/Selector.hpp"
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace litebird {

namespace {

std::string ascii_lower(std::string_view sv) {
    std::string out(sv);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string strip_css_comments(std::string_view text) {
    if (text.find("/*") == std::string_view::npos) return std::string(text);
    std::string out;
    out.reserve(text.size());
    char quote = '\0';
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (quote) {
            out += c;
            if (c == '\\' && i + 1 < text.size()) {
                out += text[++i];
            } else if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            out += c;
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) ++i;
            if (i + 1 < text.size()) ++i;
            if (!out.empty() && !css_is_space(out.back())) out += ' ';
            continue;
        }
        out += c;
    }
    return out;
}

// Collapses consecutive unquoted whitespace to a single ASCII space in O(N).
// Keeps quoted substrings verbatim.
std::string collapse_unquoted_space(std::string_view text) {
    std::string out;
    out.reserve(std::min<size_t>(text.size(), 4096));
    char quote = '\0';
    bool last_space = true;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (quote) {
            out += c;
            if (c == '\\' && i + 1 < text.size()) out += text[++i];
            else if (c == quote) quote = '\0';
            last_space = false;
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            out += c;
            last_space = false;
            continue;
        }
        if (css_is_space(c)) {
            if (!last_space) {
                out += ' ';
                last_space = true;
            }
            continue;
        }
        out += c;
        last_space = false;
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

float parse_media_length(std::string_view raw) {
    std::string_view sv = css_trim_view(raw);
    if (sv.empty()) return -1.0f;
    std::string s(sv);
    std::size_t consumed = 0;
    float number = 0.0f;
    try {
        number = std::stof(s, &consumed);
    } catch (...) {
        return -1.0f;
    }
    std::string unit = ascii_lower(css_trim_view(std::string_view(s).substr(consumed)));
    if (unit.empty() || unit == "px") return number;
    if (unit == "em" || unit == "rem") return number * 16.0f;
    if (unit == "pt") return number * (96.0f / 72.0f);
    if (unit == "pc") return number * 16.0f;
    if (unit == "in") return number * 96.0f;
    if (unit == "cm") return number * (96.0f / 2.54f);
    if (unit == "mm") return number * (96.0f / 25.4f);
    if (unit == "q") return number * (96.0f / 101.6f);
    return number;
}

std::vector<std::string_view> split_top_level(std::string_view text, char sep) {
    std::vector<std::string_view> parts;
    int depth = 0;
    char quote = '\0';
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (quote) {
            if (c == '\\' && i + 1 < text.size()) ++i;
            else if (c == quote) quote = '\0';
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '(' || c == '[') { ++depth; continue; }
        if ((c == ')' || c == ']') && depth > 0) { --depth; continue; }
        if (depth == 0 && c == sep) {
            parts.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.push_back(text.substr(start));
    return parts;
}

void register_layer_name(std::vector<std::string>& layers, const std::string& name) {
    if (name.empty()) return;
    if (std::find(layers.begin(), layers.end(), name) == layers.end()) {
        layers.push_back(name);
    }
}

} // namespace

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
        if (css_is_space(peek())) {
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

void CssParser::skip_at_rule_remainder() {
    char quote = '\0';
    while (m_cursor < m_css.size()) {
        char c = peek();
        if (quote) {
            advance();
            if (c == '\\' && m_cursor < m_css.size()) advance();
            else if (c == quote) quote = '\0';
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            advance();
            continue;
        }
        if (c == '/' && peek(1) == '*') {
            advance(); advance();
            while (m_cursor < m_css.size() && !(peek() == '*' && peek(1) == '/')) advance();
            if (m_cursor < m_css.size()) { advance(); advance(); }
            continue;
        }
        if (c == ';') {
            advance();
            return;
        }
        if (c == '{') {
            int depth = 1;
            advance();
            while (m_cursor < m_css.size() && depth > 0) {
                char bc = advance();
                if (quote) {
                    if (bc == '\\' && m_cursor < m_css.size()) advance();
                    else if (bc == quote) quote = '\0';
                } else if (bc == '\'' || bc == '"') {
                    quote = bc;
                } else if (bc == '/' && peek() == '*') {
                    advance();
                    while (m_cursor < m_css.size() && !(peek() == '*' && peek(1) == '/')) advance();
                    if (m_cursor < m_css.size()) { advance(); advance(); }
                } else if (bc == '{') {
                    depth++;
                } else if (bc == '}') {
                    depth--;
                }
            }
            return;
        }
        advance();
    }
}

std::string CssParser::read_prelude_before_brace_or_semicolon() {
    std::string out;
    char quote = '\0';
    int paren = 0;
    while (m_cursor < m_css.size()) {
        char c = peek();
        if (quote) {
            out += advance();
            if (c == '\\' && m_cursor < m_css.size()) out += advance();
            else if (c == quote) quote = '\0';
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            out += advance();
            continue;
        }
        if (c == '/' && peek(1) == '*') {
            advance(); advance();
            while (m_cursor < m_css.size() && !(peek() == '*' && peek(1) == '/')) advance();
            if (m_cursor < m_css.size()) { advance(); advance(); }
            if (!out.empty() && !css_is_space(out.back())) out += ' ';
            continue;
        }
        if (c == '(') { ++paren; out += advance(); continue; }
        if (c == ')' && paren > 0) { --paren; out += advance(); continue; }
        if (paren == 0 && (c == '{' || c == ';')) break;
        out += advance();
    }
    return out;
}

bool CssParser::supports_declaration(std::string_view decl) const {
    auto colon = decl.find(':');
    if (colon == std::string_view::npos) return false;
    std::string prop = ascii_lower(css_trim_view(decl.substr(0, colon)));
    std::string val = ascii_lower(css_trim_view(decl.substr(colon + 1)));
    if (prop.empty() || val.empty()) return false;
    if (prop.starts_with("--")) return true;

    static const std::unordered_set<std::string> supported_props = {
        "display", "position", "float", "clear", "overflow", "overflow-x", "overflow-y",
        "box-sizing", "width", "height", "min-width", "max-width", "min-height", "max-height",
        "margin", "margin-top", "margin-right", "margin-bottom", "margin-left",
        "padding", "padding-top", "padding-right", "padding-bottom", "padding-left",
        "border", "border-top", "border-right", "border-bottom", "border-left",
        "border-width", "border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
        "border-style", "border-top-style", "border-right-style", "border-bottom-style", "border-left-style",
        "border-color", "border-top-color", "border-right-color", "border-bottom-color", "border-left-color",
        "border-radius", "border-top-left-radius", "border-top-right-radius",
        "border-bottom-right-radius", "border-bottom-left-radius",
        "border-collapse", "border-spacing", "box-shadow", "outline", "outline-width", "outline-style", "outline-color",
        "top", "right", "bottom", "left", "inset", "z-index", "visibility", "opacity",
        "color", "background", "background-color", "background-image", "background-repeat",
        "background-position", "background-size",
        "font", "font-size", "font-weight", "font-style", "font-variant", "font-family",
        "line-height", "letter-spacing", "text-align", "text-decoration", "text-decoration-line",
        "text-decoration-style", "text-decoration-color", "text-transform", "white-space",
        "list-style", "list-style-type", "list-style-position", "list-style-image",
        "flex", "flex-flow", "flex-direction", "flex-wrap", "flex-grow", "flex-shrink", "flex-basis",
        "justify-content", "align-items", "align-content", "align-self", "order",
        "gap", "row-gap", "column-gap", "grid-gap", "grid-row-gap", "grid-column-gap",
        "grid-template-columns", "grid-template-rows", "grid-template-areas", "grid-area",
        "grid-column", "grid-row", "grid-column-start", "grid-column-end", "grid-row-start", "grid-row-end",
        "transform", "transform-origin", "filter", "backdrop-filter", "-webkit-backdrop-filter",
        "animation", "animation-name", "animation-duration", "animation-timing-function", "animation-iteration-count",
        "transition", "transition-property", "transition-duration", "transition-timing-function", "transition-delay",
        "contain", "cursor", "content", "pointer-events", "user-select", "vertical-align", "all"
    };
    if (!supported_props.count(prop)) return false;

    if (prop == "display") {
        // Strip !important if present
        auto bang = val.find('!');
        if (bang != std::string::npos) val = std::string(css_trim_view(std::string_view(val).substr(0, bang)));
        static const std::unordered_set<std::string> disp = {
            "block", "inline", "inline-block", "flex", "inline-flex", "grid", "inline-grid",
            "table", "table-row", "table-cell", "table-row-group", "table-header-group",
            "table-footer-group", "none", "list-item", "contents", "inherit", "initial", "unset"
        };
        return disp.count(val) > 0;
    }
    return true;
}

bool CssParser::evaluate_supports_condition(std::string_view cond, int depth) const {
    if (depth > 64) return false;
    std::string cleaned = strip_css_comments(cond);
    std::string_view s = css_trim_view(cleaned);
    if (s.empty()) return false;

    // Strip redundant outer parentheses if one pair spans the entire string.
    while (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        int paren_depth = 0;
        char quote = '\0';
        bool wraps_all = true;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (quote) {
                if (c == '\\' && i + 1 < s.size()) ++i;
                else if (c == quote) quote = '\0';
                continue;
            }
            if (c == '\'' || c == '"') { quote = c; continue; }
            if (c == '(') ++paren_depth;
            else if (c == ')') {
                --paren_depth;
                if (paren_depth == 0 && i + 1 < s.size()) {
                    wraps_all = false;
                    break;
                }
            }
        }
        if (!wraps_all) break;
        std::string_view inner = css_trim_view(s.substr(1, s.size() - 2));
        // Only unwrap if inner itself is a boolean combination or another parens/not/selector,
        // NOT a bare "prop: val" declaration!
        if (inner.empty()) return false;
        const bool has_top_colon = [&] {
            int d = 0;
            char q = '\0';
            for (size_t i = 0; i < inner.size(); ++i) {
                char c = inner[i];
                if (q) {
                    if (c == '\\' && i + 1 < inner.size()) ++i;
                    else if (c == q) q = '\0';
                    continue;
                }
                if (c == '\'' || c == '"') { q = c; continue; }
                if (c == '(') ++d;
                else if (c == ')') --d;
                else if (d == 0 && c == ':') return true;
            }
            return false;
        }();
        if (has_top_colon) return supports_declaration(inner);
        s = inner;
    }

    // Split top-level " or " / " and "
    auto split_bool_op = [&](std::string_view text, std::string_view op_word) {
        std::vector<std::string_view> parts;
        int paren_depth = 0;
        char quote = '\0';
        size_t start = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (quote) {
                if (c == '\\' && i + 1 < text.size()) ++i;
                else if (c == quote) quote = '\0';
                continue;
            }
            if (c == '\'' || c == '"') { quote = c; continue; }
            if (c == '(') { ++paren_depth; continue; }
            if (c == ')' && paren_depth > 0) { --paren_depth; continue; }
            if (paren_depth == 0 && i > start && i + op_word.size() < text.size() &&
                css_is_space(text[i - 1]) && css_is_space(text[i + op_word.size()])) {
                bool eq = true;
                for (size_t k = 0; k < op_word.size(); ++k) {
                    if ((text[i + k] | 0x20) != op_word[k]) { eq = false; break; }
                }
                if (eq) {
                    parts.push_back(text.substr(start, i - start));
                    start = i + op_word.size();
                    i = start - 1;
                }
            }
        }
        parts.push_back(text.substr(start));
        return parts;
    };

    auto or_parts = split_bool_op(s, "or");
    if (or_parts.size() > 1) {
        for (auto part : or_parts) {
            if (evaluate_supports_condition(part, depth + 1)) return true;
        }
        return false;
    }

    auto and_parts = split_bool_op(s, "and");
    if (and_parts.size() > 1) {
        for (auto part : and_parts) {
            if (!evaluate_supports_condition(part, depth + 1)) return false;
        }
        return true;
    }

    // Leading "not "
    if (s.size() >= 4 && ((s[0] | 0x20) == 'n') && ((s[1] | 0x20) == 'o') &&
        ((s[2] | 0x20) == 't') && (css_is_space(s[3]) || s[3] == '(')) {
        return !evaluate_supports_condition(s.substr(3), depth + 1);
    }

    // selector(...)
    if (s.size() >= 10 && ascii_lower(s.substr(0, 9)) == "selector(" && s.back() == ')') {
        std::string_view inner = css_trim_view(s.substr(9, s.size() - 10));
        if (inner == ":host" || inner.starts_with(":host(")) return true;
        SelectorList tmp;
        return SelectorList::parse(inner, tmp);
    }

    // Fallback: direct prop: val if parens were stripped externally
    if (s.find(':') != std::string_view::npos) {
        return supports_declaration(s);
    }
    return false;
}

StyleSheet CssParser::parse_stylesheet() {
    StyleSheet sheet;
    parse_stylesheet_blocks(sheet);
    return sheet;
}

void CssParser::parse_stylesheet_blocks(StyleSheet& sheet) {
    struct BlockFrame {
        std::string layer;
        bool emit_rules{true};
        bool is_layer{false};
        bool stop_on_rbrace{true};
        size_t media_len_before{0};
        std::string added_media_key;
    };

    std::vector<BlockFrame> frames;
    frames.push_back({"", true, false, false, 0, ""});
    std::vector<MediaQuery> active_media;
    std::vector<std::string> active_media_keys;

    auto pop_frame = [&]() {
        if (frames.size() <= 1) return;
        const BlockFrame frame = std::move(frames.back());
        frames.pop_back();
        if (frame.is_layer && frame.emit_rules && !frame.layer.empty()) {
            register_layer_name(sheet.layers, frame.layer);
        }
        if (active_media.size() > frame.media_len_before) {
            active_media.resize(frame.media_len_before);
            active_media_keys.resize(frame.media_len_before);
        }
    };

    while (m_cursor < m_css.size()) {
        consume_whitespace_and_comments();
        if (m_cursor >= m_css.size()) break;
        if (peek() == '}') {
            advance();
            pop_frame();
            continue;
        }

        const bool emit_rules = frames.back().emit_rules;
        const std::string current_layer = frames.back().layer;

        if (peek() == '@') {
            advance(); // consume '@'
            std::string at_keyword;
            while (m_cursor < m_css.size() && (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '-')) {
                at_keyword += static_cast<char>(std::tolower(static_cast<unsigned char>(advance())));
            }

            if (at_keyword == "keyframes" || at_keyword == "-webkit-keyframes") {
                consume_whitespace_and_comments();
                auto kf = parse_keyframes();
                if (emit_rules) sheet.keyframes.push_back(std::move(kf));
                continue;
            }

            if (at_keyword == "font-face") {
                consume_whitespace_and_comments();
                FontFaceRule face = parse_font_face();
                // A rule inside an `@media` that does not match, or a `@layer`
                // that is not in the layer order, declares nothing -- the same
                // gate every other rule here goes through.
                if (emit_rules && !face.family.empty() && !face.sources.empty()) {
                    sheet.font_faces.push_back(std::move(face));
                }
                continue;
            }

            if (at_keyword == "media") {
                consume_whitespace_and_comments();
                std::string media_query_str = read_prelude_before_brace_or_semicolon();
                if (match('{')) {
                    const size_t before_len = active_media.size();
                    std::string key(css_trim_view(media_query_str));
                    bool already = std::find(active_media_keys.begin(), active_media_keys.end(), key) != active_media_keys.end();
                    if (!already && active_media.size() < 64) {
                        active_media.push_back(parse_media_query(media_query_str));
                        active_media_keys.push_back(key);
                    }
                    frames.push_back({current_layer, emit_rules, false, true, before_len, already ? "" : std::move(key)});
                } else if (peek() == ';') {
                    advance();
                }
                continue;
            }

            if (at_keyword == "supports") {
                consume_whitespace_and_comments();
                std::string cond_str = read_prelude_before_brace_or_semicolon();
                if (match('{')) {
                    const bool ok = emit_rules && evaluate_supports_condition(cond_str);
                    frames.push_back({current_layer, ok, false, true, active_media.size(), ""});
                } else if (peek() == ';') {
                    advance();
                }
                continue;
            }

            if (at_keyword == "layer") {
                consume_whitespace_and_comments();
                std::string prelude = read_prelude_before_brace_or_semicolon();
                if (match('{')) {
                    std::string layer_ident = std::string(css_trim_view(prelude));
                    if (layer_ident.empty()) {
                        layer_ident = "__anon_" + std::to_string(m_anon_layer_counter++);
                    }
                    std::string qualified = current_layer.empty()
                                                ? layer_ident
                                                : current_layer + "." + layer_ident;
                    frames.push_back({std::move(qualified), emit_rules, true, true, active_media.size(), ""});
                } else {
                    if (peek() == ';') advance();
                    if (emit_rules) {
                        for (auto part : split_top_level(prelude, ',')) {
                            std::string name(css_trim_view(part));
                            if (!name.empty()) {
                                std::string qualified = current_layer.empty()
                                                            ? name
                                                            : current_layer + "." + name;
                                register_layer_name(sheet.layers, qualified);
                            }
                        }
                    }
                }
                continue;
            }

            // Skip other @rules like @import, @charset, @font-face, @container
            skip_at_rule_remainder();
            continue;
        }

        Rule rule = parse_rule();
        if (emit_rules && !rule.selectors.empty() && !rule.declarations.empty()) {
            rule.layer_name = current_layer;
            if (!active_media.empty()) {
                rule.media_queries = active_media;
            }
            sheet.rules.push_back(std::move(rule));
        }
    }

    while (frames.size() > 1) pop_frame();
}

Rule CssParser::parse_rule() {
    Rule rule;

    // Parse selector list (separated by top-level comma)
    while (m_cursor < m_css.size()) {
        consume_whitespace_and_comments();
        if (peek() == '}' || m_cursor >= m_css.size()) break;
        Selector s = parse_selector();
        if (!s.parts.empty() || s.compiled || s.has_host) {
            rule.selectors.push_back(std::move(s));
        }
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
        // Skip stray unexpected tokens until '{' or '}'
        if (peek() != '{' && peek() != ',') {
            advance();
        }
    }

    // Parse declarations
    while (m_cursor < m_css.size()) {
        consume_whitespace_and_comments();
        if (peek() == '}' || m_cursor >= m_css.size()) {
            if (peek() == '}') advance();
            break;
        }
        Declaration d = parse_declaration();
        if (!d.property.empty()) {
            rule.declarations.push_back(std::move(d));
        }
    }

    return rule;
}

Selector CssParser::parse_selector() {
    Selector sel;
    Combinator pending_comb = Combinator::Descendant;
    bool need_comb = false;

    while (m_cursor < m_css.size() && peek() != '{' && peek() != ',' && peek() != '}') {
        const size_t ws_before = m_cursor;
        consume_whitespace_and_comments();
        const bool had_ws = (m_cursor > ws_before);
        if (peek() == '{' || peek() == ',' || peek() == '}' || m_cursor >= m_css.size()) break;

        if (peek() == '>' || peek() == '+' || peek() == '~') {
            char c = advance();
            if (c == '>') pending_comb = Combinator::Child;
            else if (c == '+') pending_comb = Combinator::AdjacentSibling;
            else pending_comb = Combinator::GeneralSibling;
            need_comb = !sel.parts.empty();
            continue;
        }

        if (had_ws && !sel.parts.empty() && !need_comb) {
            pending_comb = Combinator::Descendant;
            need_comb = true;
        }

        size_t cursor_before = m_cursor;
        std::string simple_raw;
        auto simple = parse_simple_selector(simple_raw, sel.has_host, sel.has_pseudo_element);
        if (m_cursor == cursor_before) {
            // Unexpected character that simple selector cannot consume
            advance();
            continue;
        }
        if (need_comb && !sel.parts.empty()) {
            sel.combinators.push_back(pending_comb);
        }
        sel.parts.push_back(std::move(simple));
        need_comb = false;
    }

    sel.finalize_metadata();
    return sel;
}

SimpleSelector CssParser::parse_simple_selector(std::string& raw_out, bool& has_host, bool& has_pseudo_element) {
    SimpleSelector s;
    auto is_ident_char = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
               static_cast<unsigned char>(c) >= 0x80;
    };

    // Universal namespace prefix *| or |
    if (peek() == '*' && peek(1) == '|') {
        raw_out += advance();
        raw_out += advance();
    } else if (peek() == '|' && peek(1) != '=' && peek(1) != '|') {
        raw_out += advance();
    }

    // Check for tag name or *
    if (peek() == '*') {
        advance();
        s.tag_name = "*";
        raw_out += '*';
    } else if (std::isalpha(static_cast<unsigned char>(peek())) ||
               static_cast<unsigned char>(peek()) >= 0x80 || peek() == '_' ||
               (peek() == '-' && (is_ident_char(peek(1)) || peek(1) == '\\'))) {
        while (m_cursor < m_css.size() && is_ident_char(peek())) {
            char c = advance();
            raw_out += c;
            s.tag_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    // Check for #id, .class, [attr=val], or :pseudo attached
    while (m_cursor < m_css.size()) {
        if (peek() == '#') {
            advance();
            std::string id;
            while (m_cursor < m_css.size() && (is_ident_char(peek()) || peek() == '\\')) {
                if (peek() == '\\' && peek(1) != '\0') {
                    raw_out += advance();
                    char esc = advance();
                    raw_out += esc;
                    id += esc;
                } else {
                    char c = advance();
                    id += c;
                }
            }
            if (!id.empty()) {
                raw_out += '#';
                raw_out += id;
                s.id = std::move(id);
            }
        } else if (peek() == '.') {
            advance();
            std::string cls;
            std::string cls_raw;
            while (m_cursor < m_css.size() && (is_ident_char(peek()) || peek() == '\\')) {
                if (peek() == '\\' && peek(1) != '\0') {
                    cls_raw += advance();
                    char esc = advance();
                    cls_raw += esc;
                    cls += esc;
                } else {
                    char c = advance();
                    cls_raw += c;
                    cls += c;
                }
            }
            if (!cls.empty()) {
                raw_out += '.';
                raw_out += cls_raw;
                s.classes.push_back(std::move(cls));
            }
        } else if (peek() == '[') {
            advance();
            std::string attr_str;
            char quote = '\0';
            while (m_cursor < m_css.size()) {
                char c = peek();
                if (quote) {
                    attr_str += advance();
                    if (c == '\\' && m_cursor < m_css.size()) {
                        attr_str += advance();
                    } else if (c == quote) {
                        quote = '\0';
                    }
                    continue;
                }
                if (c == '\'' || c == '"') {
                    quote = c;
                    attr_str += advance();
                    continue;
                }
                if (c == ']') {
                    advance();
                    break;
                }
                attr_str += advance();
            }

            std::string compact = collapse_unquoted_space(attr_str);
            raw_out += '[';
            raw_out += compact;
            raw_out += ']';

            // Split around operator (=, ~=, |=, ^=, $=, *=) outside quotes
            std::string_view cv(compact);
            size_t op_pos = std::string_view::npos;
            size_t op_len = 0;
            quote = '\0';
            for (size_t i = 0; i < cv.size(); ++i) {
                char c = cv[i];
                if (quote) {
                    if (c == '\\' && i + 1 < cv.size()) ++i;
                    else if (c == quote) quote = '\0';
                    continue;
                }
                if (c == '\'' || c == '"') { quote = c; continue; }
                if (c == '=') {
                    op_pos = i;
                    op_len = 1;
                    break;
                }
                if ((c == '~' || c == '|' || c == '^' || c == '$' || c == '*') &&
                    i + 1 < cv.size() && cv[i + 1] == '=') {
                    op_pos = i;
                    op_len = 2;
                    break;
                }
            }

            if (op_pos != std::string_view::npos) {
                std::string k(css_trim_view(cv.substr(0, op_pos)));
                std::string op(cv.substr(op_pos, op_len));
                std::string_view rhs = css_trim_view(cv.substr(op_pos + op_len));
                bool ignore_case = false;
                // Check trailing case flag i or s (separated by space or closing quote)
                if (rhs.size() >= 2 && (rhs.back() == 'i' || rhs.back() == 'I' || rhs.back() == 's' || rhs.back() == 'S')) {
                    char prev = rhs[rhs.size() - 2];
                    if (css_is_space(prev) || prev == '"' || prev == '\'') {
                        ignore_case = (rhs.back() == 'i' || rhs.back() == 'I');
                        rhs = css_trim_view(rhs.substr(0, rhs.size() - 1));
                    }
                }
                if (rhs.size() >= 2 && ((rhs.front() == '"' && rhs.back() == '"') ||
                                        (rhs.front() == '\'' && rhs.back() == '\''))) {
                    rhs = rhs.substr(1, rhs.size() - 2);
                }
                std::string v(rhs);
                if (op == "=") {
                    s.attributes.emplace_back(k, v);
                } else {
                    s.attributes.emplace_back(k, "");
                }
                s.attribute_matches.push_back({std::move(k), std::move(op), std::move(v), ignore_case});
            } else {
                std::string k(css_trim_view(cv));
                s.attributes.emplace_back(k, "");
                s.attribute_matches.push_back({std::move(k), "", "", false});
            }
        } else if (peek() == ':') {
            advance(); // consume ':'
            bool double_colon = false;
            if (peek() == ':') {
                advance();
                double_colon = true;
            }
            std::string pseudo_name;
            while (m_cursor < m_css.size() && is_ident_char(peek())) {
                pseudo_name += static_cast<char>(std::tolower(static_cast<unsigned char>(advance())));
            }
            if (double_colon || pseudo_name == "before" || pseudo_name == "after" ||
                pseudo_name == "first-line" || pseudo_name == "first-letter") {
                has_pseudo_element = true;
            }
            if (pseudo_name == "host" || pseudo_name == "host-context") {
                has_host = true;
            }

            std::string pseudo_arg;
            bool has_paren = false;
            if (peek() == '(') {
                has_paren = true;
                advance(); // consume '('
                int paren_depth = 1;
                char quote = '\0';
                while (m_cursor < m_css.size() && paren_depth > 0) {
                    char c = peek();
                    if (quote) {
                        pseudo_arg += advance();
                        if (c == '\\' && m_cursor < m_css.size()) pseudo_arg += advance();
                        else if (c == quote) quote = '\0';
                        continue;
                    }
                    if (c == '\'' || c == '"') {
                        quote = c;
                        pseudo_arg += advance();
                        continue;
                    }
                    if (c == '/' && peek(1) == '*') {
                        advance(); advance();
                        while (m_cursor < m_css.size() && !(peek() == '*' && peek(1) == '/')) advance();
                        if (m_cursor < m_css.size()) { advance(); advance(); }
                        if (!pseudo_arg.empty() && !css_is_space(pseudo_arg.back())) pseudo_arg += ' ';
                        continue;
                    }
                    if (c == '(') paren_depth++;
                    else if (c == ')') paren_depth--;
                    if (paren_depth > 0) {
                        pseudo_arg += advance();
                    } else {
                        advance(); // consume ')'
                        break;
                    }
                }
            }
            if (!pseudo_name.empty()) {
                std::string compact_arg = collapse_unquoted_space(pseudo_arg);
                if (raw_out.size() <= 1024) {
                    raw_out += double_colon ? "::" : ":";
                    raw_out += pseudo_name;
                    if (has_paren) {
                        raw_out += '(';
                        raw_out += compact_arg;
                        raw_out += ')';
                    }
                }
                std::shared_ptr<SelectorList> compiled_arg;
                if (has_paren && pseudo_name == "has") {
                    // Keep the :has() wrapper: its relative list is anchored
                    // at the subject, and may begin with a combinator.
                    s.has_dynamic_specificity = true;
                    if (compact_arg.size() <= 512) {
                        auto parsed = std::make_shared<SelectorList>();
                        if (SelectorList::parse(":has(" + compact_arg + ")", *parsed)) compiled_arg = std::move(parsed);
                    }
                } else if (has_paren && !compact_arg.empty() && compact_arg.size() <= 512 &&
                    (pseudo_name == "is" || pseudo_name == "where" || pseudo_name == "matches" ||
                     pseudo_name == "not" || pseudo_name == "host")) {
                    if (pseudo_name != "where" && pseudo_name != "host") {
                        s.has_dynamic_specificity = true;
                    }
                    size_t comb_count = 0;
                    for (char c : compact_arg) {
                        if (c == ' ' || c == '>' || c == '+' || c == '~') ++comb_count;
                    }
                    if (comb_count <= 16) {
                        auto parsed = std::make_shared<SelectorList>();
                        if (SelectorList::parse(compact_arg, *parsed)) {
                            compiled_arg = std::move(parsed);
                        }
                    }
                } else if (has_paren && (pseudo_name == "nth-child" || pseudo_name == "nth-last-child") &&
                           compact_arg.find(" of ") != std::string::npos) {
                    s.has_dynamic_specificity = true;
                }
                s.pseudo_classes.push_back({std::move(pseudo_name), std::move(compact_arg), std::move(compiled_arg)});
            }
        } else {
            break;
        }
    }

    if (s.has_dynamic_specificity && !has_host && !has_pseudo_element && !raw_out.empty() && raw_out.size() <= 512) {
        size_t comb_count = 0;
        for (char c : raw_out) {
            if (c == ' ' || c == '>' || c == '+' || c == '~') ++comb_count;
        }
        if (comb_count <= 16) {
            auto parsed = std::make_shared<SelectorList>();
            if (SelectorList::parse(raw_out, *parsed)) {
                s.compiled = std::move(parsed);
            }
        }
    }

    return s;
}

Declaration CssParser::parse_declaration() {
    Declaration decl;

    consume_whitespace_and_comments();
    while (m_cursor < m_css.size() && peek() != ':' && peek() != ';' && peek() != '}' && !css_is_space(peek())) {
        decl.property += advance();
    }
    std::transform(decl.property.begin(), decl.property.end(), decl.property.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    consume_whitespace_and_comments();
    if (!match(':')) {
        // Malformed declaration without ':': skip to ';' or '}'
        while (m_cursor < m_css.size() && peek() != ';' && peek() != '}') advance();
        if (peek() == ';') advance();
        decl.property.clear();
        return decl;
    }
    consume_whitespace_and_comments();

    int paren_depth = 0;
    int bracket_depth = 0;
    char quote = '\0';
    while (m_cursor < m_css.size()) {
        char c = peek();
        if (quote) {
            decl.value += advance();
            if (c == '\\' && m_cursor < m_css.size()) {
                decl.value += advance();
            } else if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            decl.value += advance();
            continue;
        }
        if (c == '/' && peek(1) == '*') {
            advance(); advance();
            while (m_cursor < m_css.size() && !(peek() == '*' && peek(1) == '/')) advance();
            if (m_cursor < m_css.size()) { advance(); advance(); }
            if (!decl.value.empty() && !css_is_space(decl.value.back())) decl.value += ' ';
            continue;
        }
        if (c == '(') { ++paren_depth; decl.value += advance(); continue; }
        if (c == ')' && paren_depth > 0) { --paren_depth; decl.value += advance(); continue; }
        if (c == '[') { ++bracket_depth; decl.value += advance(); continue; }
        if (c == ']' && bracket_depth > 0) { --bracket_depth; decl.value += advance(); continue; }
        if (paren_depth == 0 && bracket_depth == 0 && (c == ';' || c == '}')) break;
        decl.value += advance();
    }

    css_trim(decl.value);

    // Strip trailing "!important" (with optional whitespace between '!' and 'important')
    {
        std::string_view v(decl.value);
        size_t end = v.size();
        while (end > 0 && css_is_space(v[end - 1])) --end;
        const std::string_view imp = "important";
        if (end >= imp.size()) {
            bool is_imp = true;
            for (size_t k = 0; k < imp.size(); ++k) {
                if ((v[end - imp.size() + k] | 0x20) != imp[k]) {
                    is_imp = false;
                    break;
                }
            }
            if (is_imp) {
                size_t bang = end - imp.size();
                while (bang > 0 && css_is_space(v[bang - 1])) --bang;
                if (bang > 0 && v[bang - 1] == '!') {
                    --bang;
                    decl.value = std::string(css_trim_view(v.substr(0, bang)));
                    decl.important = true;
                }
            }
        }
    }

    if (peek() == ';') advance();

    return decl;
}

MediaQuery CssParser::parse_single_media_query(std::string_view str) {
    MediaQuery mq;
    std::string s = ascii_lower(css_trim_view(str));
    if (s.empty()) return mq;

    // Leading "not " / "only "
    if (s.rfind("not ", 0) == 0 || s.rfind("not(", 0) == 0) {
        mq.negated = true;
        s = std::string(css_trim_view(std::string_view(s).substr(3)));
    } else if (s.rfind("only ", 0) == 0) {
        s = std::string(css_trim_view(std::string_view(s).substr(4)));
    }

    // Media type before first '(' or "and"
    size_t paren_start = s.find('(');
    std::string prefix = std::string(css_trim_view(
        paren_start == std::string::npos ? std::string_view(s) : std::string_view(s).substr(0, paren_start)));
    if (!prefix.empty()) {
        // Take first word
        size_t sp = prefix.find_first_of(" \t\r\n");
        std::string first_word = sp == std::string::npos ? prefix : prefix.substr(0, sp);
        if (first_word == "screen" || first_word == "print" || first_word == "all") {
            mq.media_type = first_word;
        } else if (first_word != "and") {
            // Unknown media types like "speech" or "aural" do not match screen
            mq.always_false = true;
        }
    }

    auto apply_width_bound = [&](bool is_min, bool exclusive, float val) {
        if (val < 0.0f) return;
        if (is_min) {
            if (mq.min_width < 0.0f || val > mq.min_width) {
                mq.min_width = val;
                mq.min_width_exclusive = exclusive;
            }
        } else {
            if (mq.max_width < 0.0f || val < mq.max_width) {
                mq.max_width = val;
                mq.max_width_exclusive = exclusive;
            }
        }
    };
    auto apply_height_bound = [&](bool is_min, bool exclusive, float val) {
        if (val < 0.0f) return;
        if (is_min) {
            if (mq.min_height < 0.0f || val > mq.min_height) {
                mq.min_height = val;
                mq.min_height_exclusive = exclusive;
            }
        } else {
            if (mq.max_height < 0.0f || val < mq.max_height) {
                mq.max_height = val;
                mq.max_height_exclusive = exclusive;
            }
        }
    };

    // Parse every "(...)" feature expression
    size_t pos = 0;
    while (pos < s.size()) {
        size_t open = s.find('(', pos);
        if (open == std::string::npos) break;
        int depth = 1;
        size_t i = open + 1;
        for (; i < s.size() && depth > 0; ++i) {
            if (s[i] == '(') ++depth;
            else if (s[i] == ')') --depth;
        }
        if (depth != 0) break;
        std::string_view expr = css_trim_view(std::string_view(s).substr(open + 1, (i - 1) - (open + 1)));
        pos = i;

        auto colon = expr.find(':');
        if (colon != std::string_view::npos) {
            std::string_view name = css_trim_view(expr.substr(0, colon));
            std::string_view val = css_trim_view(expr.substr(colon + 1));
            if (name == "max-width") apply_width_bound(false, false, parse_media_length(val));
            else if (name == "min-width") apply_width_bound(true, false, parse_media_length(val));
            else if (name == "width") {
                float px = parse_media_length(val);
                apply_width_bound(true, false, px);
                apply_width_bound(false, false, px);
            } else if (name == "max-height") apply_height_bound(false, false, parse_media_length(val));
            else if (name == "min-height") apply_height_bound(true, false, parse_media_length(val));
            else if (name == "height") {
                float px = parse_media_length(val);
                apply_height_bound(true, false, px);
                apply_height_bound(false, false, px);
            } else if (name == "prefers-color-scheme") {
                if (val == "dark") mq.always_false = true;
            } else if (name == "prefers-reduced-motion") {
                if (val == "reduce") mq.always_false = true;
            } else if (name == "pointer" || name == "any-pointer") {
                if (val == "coarse" || val == "none") mq.always_false = true;
            } else if (name == "hover" || name == "any-hover") {
                if (val == "none") mq.always_false = true;
            } else if (name == "orientation") {
                if (val == "portrait") mq.orientation = 1;
                else if (val == "landscape") mq.orientation = 2;
            }
            continue;
        }

        // Media Queries Level 4 range comparisons: (width >= 768px), (400px <= width <= 800px), etc.
        std::string es(expr);
        std::vector<std::string> tokens;
        for (size_t ti = 0; ti < es.size();) {
            while (ti < es.size() && css_is_space(es[ti])) ++ti;
            if (ti >= es.size()) break;
            if (es[ti] == '<' || es[ti] == '>' || es[ti] == '=') {
                std::string op(1, es[ti++]);
                if (ti < es.size() && es[ti] == '=') op += es[ti++];
                tokens.push_back(std::move(op));
            } else {
                size_t start = ti;
                while (ti < es.size() && !css_is_space(es[ti]) && es[ti] != '<' && es[ti] != '>' && es[ti] != '=') ++ti;
                tokens.push_back(es.substr(start, ti - start));
            }
        }
        auto apply_range = [&](const std::string& dim, const std::string& op, const std::string& val_tok) {
            float px = parse_media_length(val_tok);
            if (px < 0.0f) return;
            bool is_w = (dim == "width");
            bool is_h = (dim == "height");
            if (!is_w && !is_h) return;
            auto set_bound = [&](bool is_min, bool excl) {
                if (is_w) apply_width_bound(is_min, excl, px);
                else apply_height_bound(is_min, excl, px);
            };
            if (op == ">=" || op == ">") set_bound(true, op == ">");
            else if (op == "<=" || op == "<") set_bound(false, op == "<");
            else if (op == "=") { set_bound(true, false); set_bound(false, false); }
        };
        auto invert_op = [](const std::string& op) -> std::string {
            if (op == "<=") return ">=";
            if (op == "<") return ">";
            if (op == ">=") return "<=";
            if (op == ">") return "<";
            return op;
        };
        if (tokens.size() == 3) {
            if (tokens[0] == "width" || tokens[0] == "height") apply_range(tokens[0], tokens[1], tokens[2]);
            else if (tokens[2] == "width" || tokens[2] == "height") apply_range(tokens[2], invert_op(tokens[1]), tokens[0]);
        } else if (tokens.size() == 5 && (tokens[2] == "width" || tokens[2] == "height")) {
            apply_range(tokens[2], invert_op(tokens[1]), tokens[0]);
            apply_range(tokens[2], tokens[3], tokens[4]);
        }
    }

    return mq;
}

MediaQuery CssParser::parse_media_query(std::string_view str) {
    std::string cleaned = strip_css_comments(str);
    auto parts = split_top_level(cleaned, ',');
    if (parts.size() <= 1) {
        return parse_single_media_query(parts.empty() ? std::string_view{} : parts[0]);
    }
    MediaQuery list;
    for (auto part : parts) {
        list.alternatives.push_back(parse_single_media_query(part));
    }
    return list;
}

namespace {

/// Reads `fn(...)` at the start of `text`, skipping leading spaces.
///
/// `consumed` is how far to advance to get past the closing `)` -- the caller
/// is walking a whole source item, because `url(x) format("woff")` and
/// `format("woff") url(x)` are both written. `ok` is false when `text` does
/// not start with that function at all.
struct Function {
    std::string argument;     ///< the text between the parens, quotes removed
    std::size_t consumed{0};
    bool ok{false};
};

Function read_function(std::string_view text, std::string_view fn) {
    Function f;
    std::size_t i = 0;
    while (i < text.size() && css_is_space(text[i])) ++i;
    if (text.size() - i < fn.size() + 2) return f;
    if (!text.substr(i).starts_with(fn)) return f;
    i += fn.size();
    while (i < text.size() && css_is_space(text[i])) ++i;
    if (i >= text.size() || text[i] != '(') return f;
    ++i;

    std::string out;
    char quote = '\0';
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (quote) {
            if (c == '\\' && i + 1 < text.size()) {
                out += text[++i];
            } else if (c == quote) {
                quote = '\0';
            } else {
                out += c;
            }
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == ')') { ++i; break; }
        if (css_is_space(c) && out.empty()) continue;   // url( "x.ttf" )
        out += c;
    }
    if (quote) return f;                                 // an argument that never closed
    f.argument = std::string(css_trim_view(out));
    f.consumed = i;
    f.ok = true;
    return f;
}

/// A `font-family` descriptor value with its quotes taken off. The descriptor
/// names *one* family, so an unquoted `My Font` is two words of one name and
/// not a two-entry list; consecutive spaces collapse to one, as they do in the
/// property. Case is left as written, because nothing here compares -- the
/// registry lower-cases both sides when it matches.
std::string font_face_family_name(std::string_view value) {
    std::string out;
    char quote = '\0';
    bool pending_space = false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (quote) {
            if (c == '\\' && i + 1 < value.size()) {
                out += value[++i];
            } else if (c == quote) {
                quote = '\0';
            } else {
                out += c;
            }
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (css_is_space(c)) { pending_space = !out.empty(); continue; }
        if (pending_space) { out += ' '; pending_space = false; }
        out += c;
    }
    return std::string(css_trim_view(out));
}

/// A `font-weight` descriptor: `normal`, `bold`, one number, or a range of
/// two. Anything else is `normal`, which is what CSS says an unparseable
/// descriptor becomes rather than what makes the rule invalid.
void parse_font_face_weight(std::string_view value, int& min_out, int& max_out) {
    min_out = 400;
    max_out = 400;
    std::vector<std::string> words;
    std::string word;
    for (const char c : css_trim_view(value)) {
        if (css_is_space(c)) {
            if (!word.empty()) words.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) words.push_back(word);
    if (words.empty()) return;

    const auto number = [](const std::string& s, int& out) {
        if (s.empty() || s.size() > 4) return false;   // 1..1000, and no sign
        for (const char c : s) {
            if (c < '0' || c > '9') return false;
        }
        out = std::stoi(s);
        return true;
    };

    int first = 0;
    if (!number(words[0], first)) {
        if (words[0] == "bold") min_out = max_out = 700;
        return;
    }
    if (words.size() >= 2) {
        // `font-weight: 100 400` is a range. The ends may be written in either
        // order by a hand that mistyped, and sorting costs nothing.
        int second = 0;
        if (number(words[1], second)) {
            min_out = std::min(first, second);
            max_out = std::max(first, second);
        }
        return;
    }
    min_out = max_out = std::clamp(first, 1, 1000);
}

std::vector<FontFaceSrc> parse_font_face_src(std::string_view value) {
    std::vector<FontFaceSrc> out;
    for (const std::string_view part : split_top_level(value, ',')) {
        const std::string_view item = css_trim_view(part);
        if (item.empty()) continue;

        FontFaceSrc src;
        std::size_t at = 0;
        while (at < item.size()) {
            if (const Function f = read_function(item.substr(at), "url"); f.ok) {
                src.url = f.argument;
                at += f.consumed;
                continue;
            }
            if (const Function f = read_function(item.substr(at), "local"); f.ok) {
                src.local = f.argument;
                at += f.consumed;
                continue;
            }
            if (const Function f = read_function(item.substr(at), "format"); f.ok) {
                src.format = ascii_lower(f.argument);   // the file-scope helper at the top
                at += f.consumed;
                continue;
            }
            ++at;
        }

        if (!src.url.empty() || !src.local.empty()) {
            out.push_back(std::move(src));
        }
    }
    return out;
}

} // namespace

FontFaceRule CssParser::parse_font_face() {
    FontFaceRule face;
    consume_whitespace_and_comments();
    if (!match('{')) {
        // `@font-face` with no block: skip to the next `;` or `}` so the rest
        // of the sheet is still read.
        while (m_cursor < m_css.size() && peek() != ';' && peek() != '}') advance();
        if (peek() == ';') advance();
        return face;
    }

    while (m_cursor < m_css.size()) {
        consume_whitespace_and_comments();
        if (m_cursor >= m_css.size() || peek() == '}') {
            if (peek() == '}') advance();
            break;
        }
        if (peek() == ';') { advance(); continue; }

        const Declaration decl = parse_declaration();
        if (decl.property.empty()) continue;
        const std::string value = std::string(css_trim_view(decl.value));

        if (decl.property == "font-family") {
            face.family = font_face_family_name(value);
        } else if (decl.property == "src") {
            face.sources = parse_font_face_src(value);
        } else if (decl.property == "font-weight") {
            parse_font_face_weight(value, face.weight_min, face.weight_max);
        } else if (decl.property == "font-style") {
            // `oblique` is accepted and treated as italic: the engine has no
            // slant of its own to synthesise, so asking for one is asking for
            // the italic face, and handing out an upright one is worse.
            const std::string v = value.substr(0, 6);
            face.italic = (v == "italic") || value.starts_with("oblique");
        }
    }

    return face;
}

KeyframesRule CssParser::parse_keyframes() {
    KeyframesRule kr;
    consume_whitespace_and_comments();
    while (m_cursor < m_css.size() && !css_is_space(peek()) && peek() != '{') {
        kr.name += advance();
    }
    consume_whitespace_and_comments();
    if (!match('{')) return kr;

    while (m_cursor < m_css.size()) {
        consume_whitespace_and_comments();
        if (peek() == '}' || m_cursor >= m_css.size()) {
            if (peek() == '}') advance();
            break;
        }

        // Parse keyframe selector: e.g. "0%", "100%", "from", "to", "50%"
        std::string sel_str;
        while (m_cursor < m_css.size() && peek() != '{') {
            sel_str += advance();
        }
        css_trim(sel_str);

        float offset = 0.0f;
        if (sel_str == "from" || sel_str == "0%") {
            offset = 0.0f;
        } else if (sel_str == "to" || sel_str == "100%") {
            offset = 1.0f;
        } else if (!sel_str.empty() && sel_str.back() == '%') {
            try {
                offset = std::stof(sel_str.substr(0, sel_str.size() - 1)) / 100.0f;
            } catch (...) {}
        }

        if (match('{')) {
            Keyframe kf;
            kf.offset = offset;
            while (m_cursor < m_css.size()) {
                consume_whitespace_and_comments();
                if (peek() == '}' || m_cursor >= m_css.size()) {
                    if (peek() == '}') advance();
                    break;
                }
                Declaration d = parse_declaration();
                if (!d.property.empty()) {
                    kf.declarations.push_back(std::move(d));
                }
            }
            kr.keyframes.push_back(std::move(kf));
        }
    }

    // Sort keyframes by offset ascending
    std::sort(kr.keyframes.begin(), kr.keyframes.end(), [](const Keyframe& a, const Keyframe& b) {
        return a.offset < b.offset;
    });

    return kr;
}

} // namespace litebird
