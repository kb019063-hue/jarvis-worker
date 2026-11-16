#include "litebird/html/HtmlParser.hpp"

#include "litebird/dom/Text.hpp"
#include "litebird/net/TextEncoding.hpp"

#include <algorithm>
#include <unordered_set>

namespace litebird {

namespace {

bool is_prescan_space(unsigned char b) {
    return b == 0x09u || b == 0x0Au || b == 0x0Cu || b == 0x0Du || b == 0x20u;
}

bool is_ascii_alpha_byte(unsigned char b) {
    return (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z');
}

char lower_ascii_byte(unsigned char b) {
    if (b >= 'A' && b <= 'Z') {
        return static_cast<char>(b + ('a' - 'A'));
    }
    return static_cast<char>(b);
}

// WHATWG HTML §13.2.3.2 "get an attribute".
bool prescan_get_attribute(std::string_view s, std::size_t& pos, std::string& attr_name, std::string& attr_value) {
    attr_name.clear();
    attr_value.clear();

    while (pos < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[pos]);
        if (is_prescan_space(b) || b == 0x2Fu) {
            ++pos;
        } else {
            break;
        }
    }
    if (pos >= s.size()) {
        return false;
    }
    if (static_cast<unsigned char>(s[pos]) == 0x3Eu) {
        // Note: "get an attribute" leaves `pos` AT `>` on early `>`; caller advances.
        return false;
    }

    // Step 4-5: read attribute name.
    while (pos < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[pos]);
        if (b == 0x3Du && !attr_name.empty()) {
            ++pos;
            goto value_step;
        }
        if (is_prescan_space(b)) {
            goto spaces_step;
        }
        if (b == 0x2Fu || b == 0x3Eu) {
            return true;
        }
        attr_name.push_back(lower_ascii_byte(b));
        ++pos;
    }
    return true;

spaces_step:
    while (pos < s.size() && is_prescan_space(static_cast<unsigned char>(s[pos]))) {
        ++pos;
    }
    if (pos >= s.size() || static_cast<unsigned char>(s[pos]) != 0x3Du) {
        return true;
    }
    ++pos;

value_step:
    while (pos < s.size() && is_prescan_space(static_cast<unsigned char>(s[pos]))) {
        ++pos;
    }
    if (pos >= s.size()) {
        return true;
    }
    unsigned char quote = static_cast<unsigned char>(s[pos]);
    if (quote == 0x22u || quote == 0x27u) {
        ++pos;
        while (pos < s.size()) {
            unsigned char b = static_cast<unsigned char>(s[pos++]);
            if (b == quote) {
                return true;
            }
            attr_value.push_back(lower_ascii_byte(b));
        }
        return true;
    }
    if (quote == 0x3Eu) {
        return true;
    }
    while (pos < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[pos]);
        if (is_prescan_space(b) || b == 0x3Eu) {
            return true;
        }
        attr_value.push_back(lower_ascii_byte(b));
        ++pos;
    }
    return true;
}

// WHATWG HTML §13.2.3.2 "extract a character encoding from a meta element".
std::string extract_meta_content_encoding(std::string_view s) {
    std::size_t pos = 0;
    while (pos < s.size()) {
        // `s` is already ASCII-lowercased by `prescan_get_attribute`.
        std::size_t idx = s.find("charset", pos);
        if (idx == std::string_view::npos) {
            return {};
        }
        pos = idx + 7;
        while (pos < s.size() && is_prescan_space(static_cast<unsigned char>(s[pos]))) {
            ++pos;
        }
        if (pos >= s.size() || s[pos] != '=') {
            continue;
        }
        ++pos;
        while (pos < s.size() && is_prescan_space(static_cast<unsigned char>(s[pos]))) {
            ++pos;
        }
        if (pos >= s.size()) {
            return {};
        }
        char b = s[pos];
        if (b == '"' || b == '\'') {
            std::size_t close = s.find(b, pos + 1);
            if (close == std::string_view::npos) {
                return {};
            }
            return std::string(s.substr(pos + 1, close - (pos + 1)));
        }
        std::size_t end = pos;
        while (end < s.size() && !is_prescan_space(static_cast<unsigned char>(s[end])) && s[end] != ';') {
            ++end;
        }
        return std::string(s.substr(pos, end - pos));
    }
    return {};
}

} // namespace

std::shared_ptr<Document> HtmlParser::parse(std::string_view html, bool scripting) {
    HtmlTreeBuilder builder;
    builder.set_scripting(scripting);
    return builder.parse(html);
}

void HtmlParser::parse_into(const std::shared_ptr<Document>& document, std::string_view html, bool scripting,
                            HtmlTreeBuilder::ScriptHandler on_script) {
    HtmlTreeBuilder builder;
    builder.set_scripting(scripting);
    builder.set_document(document);
    builder.set_script_handler(std::move(on_script));
    builder.parse(html);
}

std::shared_ptr<Node> HtmlParser::parse_fragment_with_context(std::string_view html,
                                                              const std::string& context_tag,
                                                              ElementNamespace context_namespace,
                                                              bool scripting) {
    HtmlTreeBuilder builder;
    builder.set_scripting(scripting);
    return builder.parse_fragment(html, context_tag, context_namespace);
}

std::vector<std::shared_ptr<Node>> HtmlParser::parse_inner_html(const Element& context, std::string_view html) {
    HtmlTreeBuilder builder;
    return builder.parse_fragment(html, context)->take_children();
}

std::vector<std::shared_ptr<Node>> HtmlParser::parse_fragment(std::string_view html,
                                                              std::vector<std::string>* out_stylesheets) {
    auto fragment = parse_fragment_with_context(html, "body");
    // Detach so callers can re-parent the nodes freely. All at once: taking
    // them off the front one by one moved every later sibling each time, and
    // `innerHTML` with 100,000 elements in it took 7 seconds.
    std::vector<std::shared_ptr<Node>> nodes = fragment->take_children();
    if (out_stylesheets) {
        for (const auto& node : nodes) {
            if (node->is_element() && static_cast<Element*>(node.get())->tag_name() == "style") {
                out_stylesheets->push_back(node->text_content());
            }
        }
    }
    return nodes;
}

std::string HtmlParser::prescan_meta_charset(std::string_view bytes) {
    const std::string_view head = bytes.substr(0, std::min<std::size_t>(bytes.size(), 1024));
    std::size_t pos = 0;

    while (pos < head.size()) {
        if (static_cast<unsigned char>(head[pos]) != 0x3Cu) {
            ++pos;
            continue;
        }

        // Comment: `<!--` ... `-->` (where `>` is at index >= pos + 4 preceded by `--`).
        if (pos + 4 <= head.size() && head.compare(pos, 4, "<!--") == 0) {
            std::size_t end = pos + 4;
            bool closed = false;
            while (end < head.size()) {
                if (head[end] == '>' && head[end - 1] == '-' && head[end - 2] == '-') {
                    pos = end + 1;
                    closed = true;
                    break;
                }
                ++end;
            }
            if (!closed) {
                return {};
            }
            continue;
        }

        // `<meta` followed by ASCII whitespace or `/`.
        if (pos + 6 <= head.size() &&
            lower_ascii_byte(static_cast<unsigned char>(head[pos + 1])) == 'm' &&
            lower_ascii_byte(static_cast<unsigned char>(head[pos + 2])) == 'e' &&
            lower_ascii_byte(static_cast<unsigned char>(head[pos + 3])) == 't' &&
            lower_ascii_byte(static_cast<unsigned char>(head[pos + 4])) == 'a' &&
            (is_prescan_space(static_cast<unsigned char>(head[pos + 5])) ||
             static_cast<unsigned char>(head[pos + 5]) == 0x2Fu)) {
            pos += 5;
            std::unordered_set<std::string> seen_attrs;
            bool got_pragma = false;
            enum class NeedPragma { Null, True, False } need_pragma = NeedPragma::Null;
            std::string charset;

            std::string attr_name;
            std::string attr_value;
            while (prescan_get_attribute(head, pos, attr_name, attr_value)) {
                if (!seen_attrs.insert(attr_name).second) {
                    continue;
                }
                if (attr_name == "http-equiv") {
                    if (attr_value == "content-type") {
                        got_pragma = true;
                    }
                } else if (attr_name == "content") {
                    if (charset.empty()) {
                        std::string extracted = extract_meta_content_encoding(attr_value);
                        if (!extracted.empty()) {
                            charset = std::move(extracted);
                            need_pragma = NeedPragma::True;
                        }
                    }
                } else if (attr_name == "charset") {
                    charset = attr_value;
                    need_pragma = NeedPragma::False;
                }
            }
            if (pos < head.size() && head[pos] == '>') {
                ++pos;
            }

            if (need_pragma == NeedPragma::Null) {
                continue;
            }
            if (need_pragma == NeedPragma::True && !got_pragma) {
                continue;
            }

            EncodingId enc = EncodingId::Utf8;
            if (!get_encoding_from_label(charset, enc)) {
                continue;
            }
            if (enc == EncodingId::Utf16BE || enc == EncodingId::Utf16LE) {
                enc = EncodingId::Utf8;
            } else if (enc == EncodingId::XUserDefined) {
                enc = EncodingId::Windows1252;
            }
            return std::string(canonical_encoding_name(enc));
        }

        // `<` + ASCII alpha, or `</` + ASCII alpha: skip tag name and attributes.
        if ((pos + 2 <= head.size() && is_ascii_alpha_byte(static_cast<unsigned char>(head[pos + 1]))) ||
            (pos + 3 <= head.size() && head[pos + 1] == '/' &&
             is_ascii_alpha_byte(static_cast<unsigned char>(head[pos + 2])))) {
            pos += (head[pos + 1] == '/') ? 2 : 1;
            while (pos < head.size()) {
                unsigned char b = static_cast<unsigned char>(head[pos]);
                if (is_prescan_space(b) || b == 0x3Eu) {
                    break;
                }
                ++pos;
            }
            std::string attr_name;
            std::string attr_value;
            while (prescan_get_attribute(head, pos, attr_name, attr_value)) {
            }
            if (pos < head.size() && head[pos] == '>') {
                ++pos;
            }
            continue;
        }

        // `<!`, `</`, or `<?`: advance past next `>`.
        if (pos + 2 <= head.size() &&
            (head[pos + 1] == '!' || head[pos + 1] == '/' || head[pos + 1] == '?')) {
            std::size_t gt = head.find('>', pos + 2);
            if (gt == std::string_view::npos) {
                return {};
            }
            pos = gt + 1;
            continue;
        }

        ++pos;
    }

    return {};
}

std::string HtmlParser::decode_bytes(std::string_view bytes,
                                     std::string_view transport_charset_label,
                                     std::string_view fallback_encoding_label,
                                     std::string* resolved_encoding_out) {
    EncodingId chosen = EncodingId::Utf8;
    std::size_t bom_len = 0;

    if (sniff_bom_encoding(bytes, chosen, bom_len)) {
        bytes.remove_prefix(bom_len);
    } else {
        bool resolved = false;
        if (!transport_charset_label.empty() && get_encoding_from_label(transport_charset_label, chosen)) {
            resolved = true;
        }
        if (!resolved) {
            std::string meta_cs = prescan_meta_charset(bytes);
            if (!meta_cs.empty() && get_encoding_from_label(meta_cs, chosen)) {
                resolved = true;
            }
        }
        if (!resolved) {
            if (fallback_encoding_label.empty() || !get_encoding_from_label(fallback_encoding_label, chosen)) {
                chosen = EncodingId::Utf8;
            }
        }
    }

    if (resolved_encoding_out) {
        *resolved_encoding_out = std::string(canonical_encoding_name(chosen));
    }

    std::string out_utf8;
    decode_with_encoding(bytes, chosen, out_utf8, false);
    return out_utf8;
}

} // namespace litebird
