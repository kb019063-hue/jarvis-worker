// The XML parser. See XmlParser.hpp for what it is; the section numbers in
// the comments are those of "Extensible Markup Language (XML) 1.0 (Fifth
// Edition)" and of "Namespaces in XML 1.0 (Third Edition)".

#include "litebird/xml/XmlParser.hpp"
#include "litebird/html/EntityTable.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace litebird {
namespace xml {

namespace {

// ---------------------------------------------------------------------------
// Characters
// ---------------------------------------------------------------------------

inline bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// One code point of UTF-8 at `pos`. False for anything that is not the one
// shortest encoding of a scalar value: a stray continuation byte, a sequence
// cut short, an overlong form, a surrogate, a value past U+10FFFF.
bool decode(std::string_view s, std::size_t pos, char32_t& c, std::size_t& length) {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
    const unsigned char first = byte(pos);
    if (first < 0x80) {
        c = first;
        length = 1;
        return true;
    }
    std::size_t need = 0;
    char32_t low = 0;
    if (first >= 0xC2 && first <= 0xDF) { need = 1; c = first & 0x1F; low = 0x80; }
    else if (first >= 0xE0 && first <= 0xEF) { need = 2; c = first & 0x0F; low = 0x800; }
    else if (first >= 0xF0 && first <= 0xF4) { need = 3; c = first & 0x07; low = 0x10000; }
    else return false;
    if (pos + need >= s.size()) return false;
    for (std::size_t i = 1; i <= need; ++i) {
        const unsigned char next = byte(pos + i);
        if ((next & 0xC0) != 0x80) return false;
        c = (c << 6) | (next & 0x3F);
    }
    if (c < low || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return false;
    length = need + 1;
    return true;
}

// [2] Char, for what is not ASCII (the caller deals with the controls).
inline bool is_char(char32_t c) {
    return c != 0xFFFE && c != 0xFFFF;
}

// [4] NameStartChar and [4a] NameChar, as the Fifth Edition has them.
bool is_name_start(char32_t c) {
    if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':';
    return (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) || (c >= 0xF8 && c <= 0x2FF) ||
           (c >= 0x370 && c <= 0x37D) || (c >= 0x37F && c <= 0x1FFF) || (c >= 0x200C && c <= 0x200D) ||
           (c >= 0x2070 && c <= 0x218F) || (c >= 0x2C00 && c <= 0x2FEF) || (c >= 0x3001 && c <= 0xD7FF) ||
           (c >= 0xF900 && c <= 0xFDCF) || (c >= 0xFDF0 && c <= 0xFFFD) || (c >= 0x10000 && c <= 0xEFFFF);
}

bool is_name_char(char32_t c) {
    if (c < 0x80) return is_name_start(c) || c == '-' || c == '.' || (c >= '0' && c <= '9');
    return is_name_start(c) || c == 0xB7 || (c >= 0x300 && c <= 0x36F) || (c >= 0x203F && c <= 0x2040);
}

// The length of the name that starts at `pos`; 0 when none does.
std::size_t name_length(std::string_view s, std::size_t pos) {
    std::size_t at = pos;
    bool first = true;
    while (at < s.size()) {
        char32_t c = 0;
        std::size_t length = 0;
        if (!decode(s, at, c, length)) break;
        if (!(first ? is_name_start(c) : is_name_char(c))) break;
        at += length;
        first = false;
    }
    return at - pos;
}

// [13] PubidChar
bool is_pubid_char(char c) {
    if (c == ' ' || c == '\n' || c == '\r') return true;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
    static const std::string_view kOthers = "-'()+,./:=?;!*#@$_%";
    return kOthers.find(c) != std::string_view::npos;
}

bool equals_ignoring_case(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char x = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] + 32) : a[i];
        const char y = b[i] >= 'A' && b[i] <= 'Z' ? static_cast<char>(b[i] + 32) : b[i];
        if (x != y) return false;
    }
    return true;
}

// The public identifiers for which the HTML standard has a browser declare
// the entity names of HTML ("Parsing XML documents").
bool is_xhtml_public_id(const std::string& id) {
    static const char* const kIds[] = {
        "-//W3C//DTD XHTML 1.0 Transitional//EN",
        "-//W3C//DTD XHTML 1.1//EN",
        "-//W3C//DTD XHTML 1.0 Strict//EN",
        "-//W3C//DTD XHTML 1.0 Frameset//EN",
        "-//W3C//DTD XHTML Basic 1.0//EN",
        "-//W3C//DTD XHTML 1.1 plus MathML 2.0//EN",
        "-//W3C//DTD XHTML 1.1 plus MathML 2.0 plus SVG 1.1//EN",
        "-//W3C//DTD MathML 2.0//EN",
        "-//WAPFORUM//DTD XHTML Mobile 1.0//EN",
        "-//WAPFORUM//DTD XHTML Mobile 1.1//EN",
        "-//WAPFORUM//DTD XHTML Mobile 1.2//EN",
    };
    for (const char* known : kIds) {
        if (id == known) return true;
    }
    return false;
}

struct Failure {};

// ---------------------------------------------------------------------------
// The parser
// ---------------------------------------------------------------------------

struct Entity {
    std::string name;
    /// The replacement text (4.5) of an internal entity.
    std::string value;
    bool external{false};
    bool unparsed{false};
    /// Being included right now: a reference to it from inside is recursion.
    bool open{false};
};

struct AttributeDecl {
    std::string name;
    bool cdata{true};
    bool has_default{false};
    std::string default_value;
};

class Parser {
public:
    Parser(XmlHandler& handler, const XmlParseOptions& options) : m_handler(handler), m_options(options) {}

    bool run(std::string_view input, XmlError& error) {
        try {
            prepare(input);
            if (m_options.fragment) parse_fragment();
            else parse_document();
            return true;
        } catch (const Failure&) {
            error = m_error;
            return false;
        }
    }

private:
    using Text = const std::string;

    /// Where the parser reads from: the document, or the replacement text of
    /// an entity that is being included.
    struct Source {
        Text* text;
        std::size_t pos;
        Entity* entity;
        /// How many elements were open when the entity was entered. The
        /// elements it opens it has to close, and no others (4.3.2).
        std::size_t depth;
    };

    struct Open {
        XmlName name;
        std::size_t declared_mark;
    };

    // --- errors ---------------------------------------------------------------

    /// `p` is an offset into `s`. Inside an entity the place reported is
    /// that of the reference in the document that led there.
    [[noreturn]] void fail(Text& s, std::size_t p, const std::string& message) {
        std::size_t offset = &s == &m_doc ? std::min(p, m_doc.size()) : m_reference_pos;
        m_error.message = message;
        m_error.line = 1;
        std::size_t line_start = 0;
        for (std::size_t i = 0; i < offset; ++i) {
            if (m_doc[i] == '\n') {
                ++m_error.line;
                line_start = i + 1;
            }
        }
        m_error.column = 1;
        for (std::size_t i = line_start; i < offset; ++i) {
            if ((static_cast<unsigned char>(m_doc[i]) & 0xC0) != 0x80) ++m_error.column;
        }
        throw Failure{};
    }

    // --- the text ---------------------------------------------------------------

    // Checks that the input is UTF-8 and holds only characters XML allows
    // (2.2), and makes every line end a line feed (2.11). After this the
    // scanner can take the text for granted.
    void prepare(std::string_view input) {
        if (input.size() >= 3 && input.compare(0, 3, "\xEF\xBB\xBF") == 0) input.remove_prefix(3);
        m_doc.reserve(input.size());
        std::size_t i = 0;
        while (i < input.size()) {
            const unsigned char byte = static_cast<unsigned char>(input[i]);
            if (byte >= 0x20 && byte < 0x80) {
                std::size_t end = i + 1;
                while (end < input.size() && static_cast<unsigned char>(input[end]) >= 0x20 &&
                       static_cast<unsigned char>(input[end]) < 0x80) {
                    ++end;
                }
                m_doc.append(input.data() + i, end - i);
                i = end;
                continue;
            }
            if (byte == '\r') {
                m_doc += '\n';
                ++i;
                if (i < input.size() && input[i] == '\n') ++i;
                continue;
            }
            if (byte == '\n' || byte == '\t') {
                m_doc += static_cast<char>(byte);
                ++i;
                continue;
            }
            if (byte < 0x20) fail(m_doc, m_doc.size(), "a control character that XML does not allow");
            char32_t c = 0;
            std::size_t length = 0;
            if (!decode(input, i, c, length)) fail(m_doc, m_doc.size(), "the text is not valid UTF-8");
            if (!is_char(c)) fail(m_doc, m_doc.size(), "a character that XML does not allow");
            m_doc.append(input.data() + i, length);
            i += length;
        }
    }

    static bool looking_at(Text& s, std::size_t p, std::string_view literal) {
        return p <= s.size() && s.compare(p, literal.size(), literal) == 0;
    }

    static bool skip_space(Text& s, std::size_t& p) {
        const std::size_t start = p;
        while (p < s.size() && is_space(s[p])) ++p;
        return p != start;
    }

    void require_space(Text& s, std::size_t& p, const char* where) {
        if (!skip_space(s, p)) fail(s, p, std::string("white space is required ") + where);
    }

    void expect(Text& s, std::size_t& p, char c, const char* where) {
        if (p >= s.size() || s[p] != c) fail(s, p, std::string("'") + c + "' expected " + where);
        ++p;
    }

    void expect_literal(Text& s, std::size_t& p, std::string_view literal) {
        if (!looking_at(s, p, literal)) fail(s, p, "'" + std::string(literal) + "' expected");
        p += literal.size();
    }

    std::string parse_name(Text& s, std::size_t& p, const char* what) {
        const std::size_t length = name_length(s, p);
        if (length == 0) fail(s, p, std::string("a name is expected for ") + what);
        std::string name = s.substr(p, length);
        p += length;
        return name;
    }

    // A name in a declaration of the DTD that names an element or an
    // attribute: a qualified name there too (Namespaces, section 5).
    std::string parse_declared_name(Text& s, std::size_t& p, const char* what) {
        const std::size_t at = p;
        std::string name = parse_name(s, p, what);
        if (!is_xml_qname(name)) fail(s, at, "'" + name + "' is not a name with a namespace prefix");
        return name;
    }

    // [7] Nmtoken: name characters, with no rule for the first.
    void skip_nmtoken(Text& s, std::size_t& p) {
        const std::size_t start = p;
        while (p < s.size()) {
            char32_t c = 0;
            std::size_t length = 0;
            if (!decode(s, p, c, length) || !is_name_char(c)) break;
            p += length;
        }
        if (p == start) fail(s, p, "a name token is expected");
    }

    // [66] CharRef. `p` is at "&#".
    char32_t parse_char_ref(Text& s, std::size_t& p) {
        const std::size_t start = p;
        p += 2;
        const bool hex = p < s.size() && s[p] == 'x';
        if (hex) ++p;
        char32_t value = 0;
        std::size_t digits = 0;
        while (p < s.size()) {
            const char c = s[p];
            int digit = -1;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (hex && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (hex && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            if (digit < 0) break;
            if (value <= 0x10FFFF) value = value * (hex ? 16 : 10) + static_cast<char32_t>(digit);
            ++digits;
            ++p;
        }
        if (digits == 0 || p >= s.size() || s[p] != ';') fail(s, start, "a character reference is not finished");
        ++p;
        const bool legal = value == 0x9 || value == 0xA || value == 0xD || (value >= 0x20 && value <= 0xD7FF) ||
                           (value >= 0xE000 && value <= 0xFFFD) || (value >= 0x10000 && value <= 0x10FFFF);
        if (!legal) fail(s, start, "a character reference to a character that XML does not allow");
        return value;
    }

    static const char* predefined(const std::string& name) {
        if (name == "lt") return "<";
        if (name == "gt") return ">";
        if (name == "amp") return "&";
        if (name == "apos") return "'";
        if (name == "quot") return "\"";
        return nullptr;
    }

    // The entity `&name;` refers to, or null when the reference is to be
    // passed over. A name nobody declared is an error unless the document
    // has declarations this parser did not read (4.1, "Entity Declared").
    Entity* general_entity(Text& s, std::size_t at, const std::string& name) {
        auto found = m_entities.find(name);
        if (found != m_entities.end()) return found->second.get();
        if (m_xhtml_entities) {
            std::size_t consumed = 0;
            const std::string text = decode_named_character_reference(name + ";", consumed);
            if (consumed == name.size() + 1) {
                auto entity = std::make_unique<Entity>();
                entity->name = name;
                // The value is characters, never markup: `&lt;` of HTML has
                // been answered by predefined() before this is asked.
                for (const char c : text) {
                    if (c == '&') entity->value += "&#38;";
                    else if (c == '<') entity->value += "&#60;";
                    else entity->value += c;
                }
                Entity* raw = entity.get();
                m_entities.emplace(name, std::move(entity));
                return raw;
            }
        }
        if ((m_has_external_subset || m_has_pe_references) && !m_standalone) return nullptr;
        fail(s, at, "the entity '" + name + "' is not declared");
    }

    // Counts what entity references add to the document. Each reference is
    // legal by itself; ten levels of ten references each are ten thousand
    // million characters from a few hundred.
    void account_expansion(Text& s, std::size_t at, std::size_t bytes) {
        m_expanded += bytes;
        if (m_expanded <= m_options.expansion_threshold) return;
        const std::size_t direct = std::max<std::size_t>(m_doc.size(), 1);
        if ((m_expanded + direct) / direct > m_options.expansion_factor) {
            fail(s, at, "entity references expand to more than " + std::to_string(m_options.expansion_factor) +
                        " times the size of the document");
        }
    }

    // --- prolog and what follows the root ----------------------------------------

    void parse_document() {
        Text& s = m_doc;
        std::size_t p = 0;
        parse_xml_declaration(s, p);
        parse_misc(s, p);
        if (looking_at(s, p, "<!DOCTYPE")) {
            parse_doctype(s, p);
            parse_misc(s, p);
        }
        if (p >= s.size()) fail(s, p, "the document has no element");
        if (s[p] != '<') fail(s, p, "text is not allowed before the root element");
        m_sources.push_back({&m_doc, p, nullptr, 0});
        parse_content();
        p = m_sources.front().pos;
        m_sources.clear();
        parse_misc(s, p);
        if (p < s.size()) fail(s, p, "only comments and processing instructions may follow the root element");
    }

    void parse_fragment() {
        for (const auto& [prefix, uri] : m_options.namespaces) declare(prefix, uri);
        m_sources.push_back({&m_doc, 0, nullptr, 0});
        parse_content();
    }

    // [23] XMLDecl. Only at the very start of the document.
    void parse_xml_declaration(Text& s, std::size_t& p) {
        if (!looking_at(s, p, "<?xml") || p + 5 >= s.size() || !is_space(s[p + 5])) return;
        p += 5;
        require_space(s, p, "after '<?xml'");
        expect_literal(s, p, "version");
        const std::string version = parse_pseudo_attribute(s, p);
        // [26] VersionNum is '1.' and digits. A document that says 1.1 is
        // read by the rules of 1.0, which is what this edition asks for.
        bool good = version.size() > 2 && version.compare(0, 2, "1.") == 0;
        for (std::size_t i = 2; good && i < version.size(); ++i) good = version[i] >= '0' && version[i] <= '9';
        if (!good) fail(s, p, "the XML version '" + version + "' is not supported");
        bool had_space = skip_space(s, p);
        if (looking_at(s, p, "encoding")) {
            if (!had_space) fail(s, p, "white space is required before 'encoding'");
            p += 8;
            const std::string name = parse_pseudo_attribute(s, p);
            // [81] EncName
            bool valid = !name.empty() && ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z'));
            for (std::size_t i = 1; valid && i < name.size(); ++i) {
                const char c = name[i];
                valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-';
            }
            if (!valid) fail(s, p, "'" + name + "' is not the name of an encoding");
            had_space = skip_space(s, p);
        }
        if (looking_at(s, p, "standalone")) {
            if (!had_space) fail(s, p, "white space is required before 'standalone'");
            p += 10;
            const std::string value = parse_pseudo_attribute(s, p);
            if (value != "yes" && value != "no") fail(s, p, "standalone is 'yes' or 'no'");
            m_standalone = value == "yes";
            skip_space(s, p);
        }
        if (!looking_at(s, p, "?>")) fail(s, p, "the XML declaration is not finished");
        p += 2;
    }

    // S? '=' S? and a quoted value with no markup in it.
    std::string parse_pseudo_attribute(Text& s, std::size_t& p) {
        skip_space(s, p);
        expect(s, p, '=', "in the XML declaration");
        skip_space(s, p);
        if (p >= s.size() || (s[p] != '"' && s[p] != '\'')) fail(s, p, "a quoted value is expected");
        const char quote = s[p++];
        const std::size_t end = s.find(quote, p);
        if (end == std::string::npos) fail(s, p, "a quoted value is not finished");
        std::string value = s.substr(p, end - p);
        p = end + 1;
        return value;
    }

    // [27] Misc*: comments, processing instructions and white space.
    void parse_misc(Text& s, std::size_t& p) {
        for (;;) {
            skip_space(s, p);
            if (looking_at(s, p, "<!--")) parse_comment(s, p, true);
            else if (looking_at(s, p, "<?")) parse_pi(s, p, false);
            else return;
        }
    }

    // [15] Comment. `p` is at "<!--".
    void parse_comment(Text& s, std::size_t& p, bool report) {
        const std::size_t start = p + 4;
        const std::size_t end = s.find("--", start);
        if (end == std::string::npos) fail(s, p, "a comment is not finished");
        if (end + 2 >= s.size() || s[end + 2] != '>') fail(s, end, "'--' is not allowed inside a comment");
        if (report) m_handler.comment(std::string_view(s).substr(start, end - start));
        p = end + 3;
    }

    // [16] PI. `p` is at "<?".
    void parse_pi(Text& s, std::size_t& p, bool in_dtd) {
        const std::size_t start = p;
        p += 2;
        const std::string target = parse_name(s, p, "a processing instruction");
        if (equals_ignoring_case(target, "xml")) {
            fail(s, start, "an XML declaration is allowed only at the start of the document");
        }
        // Namespaces in XML, section 6: no colon in the target either.
        if (target.find(':') != std::string::npos) {
            fail(s, start, "the target of a processing instruction cannot hold a colon");
        }
        std::size_t data = p;
        if (!looking_at(s, p, "?>")) {
            require_space(s, p, "after the target of a processing instruction");
            data = p;
        }
        const std::size_t end = s.find("?>", data);
        if (end == std::string::npos) fail(s, start, "a processing instruction is not finished");
        const std::string_view text = std::string_view(s).substr(data, end - data);
        if (in_dtd) m_handler.dtd_processing_instruction(target, text);
        else m_handler.processing_instruction(target, text);
        p = end + 2;
    }

    // --- the DOCTYPE and its internal subset -------------------------------------

    // [11] SystemLiteral, [12] PubidLiteral
    std::string parse_quoted(Text& s, std::size_t& p, bool pubid) {
        if (p >= s.size() || (s[p] != '"' && s[p] != '\'')) fail(s, p, "a quoted literal is expected");
        const char quote = s[p++];
        const std::size_t end = s.find(quote, p);
        if (end == std::string::npos) fail(s, p, "a quoted literal is not finished");
        std::string value = s.substr(p, end - p);
        if (pubid) {
            for (const char c : value) {
                if (!is_pubid_char(c)) fail(s, p, "a character that a public identifier cannot hold");
            }
        }
        p = end + 1;
        return value;
    }

    // [75] ExternalID, and for a notation [83] PublicID, which has no system
    // literal. True when one was there.
    bool parse_external_id(Text& s, std::size_t& p, std::string& public_id, std::string& system_id, bool& has_public,
                           bool& has_system, bool system_optional) {
        has_public = has_system = false;
        if (looking_at(s, p, "SYSTEM")) {
            p += 6;
            require_space(s, p, "after SYSTEM");
            system_id = parse_quoted(s, p, false);
            has_system = true;
            return true;
        }
        if (looking_at(s, p, "PUBLIC")) {
            p += 6;
            require_space(s, p, "after PUBLIC");
            public_id = parse_quoted(s, p, true);
            has_public = true;
            std::size_t after = p;
            const bool spaced = skip_space(s, after);
            const bool literal_follows = after < s.size() && (s[after] == '"' || s[after] == '\'');
            if (!literal_follows) {
                if (!system_optional) fail(s, p, "a system identifier is expected after the public identifier");
                return true;
            }
            if (!spaced) fail(s, p, "white space is required between the two identifiers");
            p = after;
            system_id = parse_quoted(s, p, false);
            has_system = true;
            return true;
        }
        return false;
    }

    // [28] doctypedecl. `p` is at "<!DOCTYPE".
    void parse_doctype(Text& s, std::size_t& p) {
        p += 9;
        require_space(s, p, "after '<!DOCTYPE'");
        const std::string name = parse_name(s, p, "the document type");
        std::string public_id, system_id;
        bool has_public = false, has_system = false;
        const bool spaced = skip_space(s, p);
        if (parse_external_id(s, p, public_id, system_id, has_public, has_system, false)) {
            if (!spaced) fail(s, p, "white space is required after the name of the document type");
            // An external subset this parser will not read: what it might
            // declare is unknown from here on.
            m_has_external_subset = true;
            m_has_pe_references = true;
            skip_space(s, p);
        }
        m_xhtml_entities = m_options.xhtml_entities && has_public && is_xhtml_public_id(public_id);
        if (p < s.size() && s[p] == '[') {
            ++p;
            parse_internal_subset(s, p);
            skip_space(s, p);
        }
        expect(s, p, '>', "at the end of the DOCTYPE");
        m_handler.doctype(name, public_id, system_id);
    }

    // [28b] intSubset. `p` is past the '['; on return it is past the ']'.
    // A reference to an internal parameter entity between declarations is
    // replaced by its text, which is read as declarations in its turn.
    void parse_internal_subset(Text& document, std::size_t& document_pos) {
        std::vector<Source> sources;
        sources.push_back({&document, document_pos, nullptr, 0});
        for (;;) {
            Source& top = sources.back();
            Text& s = *top.text;
            std::size_t& p = top.pos;
            skip_space(s, p);
            if (p >= s.size()) {
                if (sources.size() == 1) fail(s, p, "the DOCTYPE is not finished");
                top.entity->open = false;
                sources.pop_back();
                continue;
            }
            if (sources.size() == 1) m_reference_pos = p;
            if (s[p] == ']' && sources.size() == 1) {
                document_pos = p + 1;
                return;
            }
            if (s[p] == '%') {
                const std::size_t at = p++;
                const std::string name = parse_name(s, p, "a parameter entity reference");
                expect(s, p, ';', "after the name of a parameter entity");
                auto found = m_parameter_entities.find(name);
                Entity* entity = found == m_parameter_entities.end() ? nullptr : found->second.get();
                // The first reference in a document with nothing unread in
                // it has to be to something declared (4.1).
                const bool must_exist = m_standalone ? sources.size() == 1 : !m_has_pe_references;
                m_has_pe_references = true;
                if (!entity) {
                    if (must_exist) fail(s, at, "the parameter entity '" + name + "' is not declared");
                    m_keep_declarations = m_standalone;
                    continue;
                }
                if (entity->open) fail(s, at, "the parameter entity '" + name + "' refers to itself");
                if (entity->external) {
                    // Not read: what follows may depend on it, so entities
                    // and attribute lists declared after it are not used.
                    m_keep_declarations = m_standalone;
                    continue;
                }
                account_expansion(s, at, entity->value.size());
                entity->open = true;
                sources.push_back({&entity->value, 0, entity, 0});
                continue;
            }
            if (looking_at(s, p, "<!--")) parse_comment(s, p, false);
            else if (looking_at(s, p, "<?")) parse_pi(s, p, true);
            else if (looking_at(s, p, "<!ELEMENT")) parse_element_declaration(s, p);
            else if (looking_at(s, p, "<!ATTLIST")) parse_attlist_declaration(s, p);
            else if (looking_at(s, p, "<!ENTITY")) parse_entity_declaration(s, p);
            else if (looking_at(s, p, "<!NOTATION")) parse_notation_declaration(s, p);
            else fail(s, p, "a declaration is expected in the DOCTYPE");
        }
    }

    // [45] elementdecl. Nothing is kept of it: this parser does not validate.
    // The content model is checked for its syntax, by counting parentheses.
    void parse_element_declaration(Text& s, std::size_t& p) {
        p += 9;
        require_space(s, p, "after '<!ELEMENT'");
        parse_declared_name(s, p, "an element declaration");
        require_space(s, p, "before the content model");
        if (looking_at(s, p, "EMPTY")) p += 5;
        else if (looking_at(s, p, "ANY")) p += 3;
        else if (p < s.size() && s[p] == '(') parse_content_model(s, p);
        else fail(s, p, "a content model is expected");
        skip_space(s, p);
        expect(s, p, '>', "at the end of an element declaration");
    }

    void skip_occurrence(Text& s, std::size_t& p) {
        if (p < s.size() && (s[p] == '?' || s[p] == '*' || s[p] == '+')) ++p;
    }

    // [51] Mixed and [47] children. `p` is at the first '('.
    void parse_content_model(Text& s, std::size_t& p) {
        std::size_t probe = p + 1;
        skip_space(s, probe);
        if (looking_at(s, probe, "#PCDATA")) {
            p = probe + 7;
            bool names = false;
            for (;;) {
                skip_space(s, p);
                if (p < s.size() && s[p] == ')') {
                    ++p;
                    break;
                }
                expect(s, p, '|', "in mixed content");
                skip_space(s, p);
                parse_declared_name(s, p, "mixed content");
                names = true;
            }
            if (p < s.size() && s[p] == '*') ++p;
            else if (names) fail(s, p, "'*' is required after mixed content that names elements");
            return;
        }
        // One separator for each group that is open: 0 until the first is seen.
        std::vector<char> groups;
        bool want_particle = true;
        do {
            skip_space(s, p);
            if (p >= s.size()) fail(s, p, "a content model is not finished");
            if (want_particle) {
                if (s[p] == '(') {
                    groups.push_back(0);
                    ++p;
                    continue;
                }
                parse_declared_name(s, p, "a content model");
                skip_occurrence(s, p);
                want_particle = false;
                continue;
            }
            const char c = s[p];
            if (c == ')') {
                groups.pop_back();
                ++p;
                skip_occurrence(s, p);
            } else if (c == ',' || c == '|') {
                if (groups.back() != 0 && groups.back() != c) fail(s, p, "',' and '|' cannot be mixed in one group");
                groups.back() = c;
                ++p;
                want_particle = true;
            } else {
                fail(s, p, "',', '|' or ')' is expected in a content model");
            }
        } while (!groups.empty());
    }

    // [52] AttlistDecl
    void parse_attlist_declaration(Text& s, std::size_t& p) {
        p += 9;
        require_space(s, p, "after '<!ATTLIST'");
        const std::string element = parse_declared_name(s, p, "an attribute list");
        for (;;) {
            const bool spaced = skip_space(s, p);
            if (p < s.size() && s[p] == '>') {
                ++p;
                return;
            }
            if (!spaced) fail(s, p, "white space is required in an attribute list");
            AttributeDecl declaration;
            declaration.name = parse_declared_name(s, p, "an attribute declaration");
            require_space(s, p, "after the name of an attribute");
            // [54] AttType
            static const char* const kTokenized[] = {"IDREFS", "IDREF", "ID", "ENTITY", "ENTITIES", "NMTOKENS", "NMTOKEN"};
            bool typed = false;
            if (looking_at(s, p, "CDATA")) {
                p += 5;
                typed = true;
            } else {
                declaration.cdata = false;
                for (const char* type : kTokenized) {
                    const std::string_view word(type);
                    if (!looking_at(s, p, word)) continue;
                    // "ID" must not take the start of "IDREF", nor "ENTITY" of "ENTITIES".
                    if (p + word.size() < s.size() && !is_space(s[p + word.size()])) continue;
                    p += word.size();
                    typed = true;
                    break;
                }
            }
            if (!typed) {
                if (looking_at(s, p, "NOTATION")) {
                    p += 8;
                    require_space(s, p, "after NOTATION");
                    if (p >= s.size() || s[p] != '(') fail(s, p, "'(' is expected after NOTATION");
                    parse_enumeration(s, p, true);
                } else if (p < s.size() && s[p] == '(') {
                    parse_enumeration(s, p, false);
                } else {
                    fail(s, p, "the type of an attribute is expected");
                }
            }
            require_space(s, p, "after the type of an attribute");
            // [60] DefaultDecl
            if (looking_at(s, p, "#REQUIRED")) {
                p += 9;
            } else if (looking_at(s, p, "#IMPLIED")) {
                p += 8;
            } else {
                if (looking_at(s, p, "#FIXED")) {
                    p += 6;
                    require_space(s, p, "after #FIXED");
                }
                declaration.default_value = parse_attribute_value(s, p);
                if (!declaration.cdata) collapse_spaces(declaration.default_value);
                declaration.has_default = true;
            }
            if (!m_keep_declarations) continue;
            // The first declaration of an attribute is the one that counts (3.3).
            auto& list = m_attlists[element];
            const bool known = std::any_of(list.begin(), list.end(),
                                           [&](const AttributeDecl& other) { return other.name == declaration.name; });
            if (!known) list.push_back(std::move(declaration));
        }
    }

    // [58] NotationType and [59] Enumeration. `p` is at the '('.
    void parse_enumeration(Text& s, std::size_t& p, bool names) {
        ++p;
        for (;;) {
            skip_space(s, p);
            if (names) parse_name(s, p, "a notation");
            else skip_nmtoken(s, p);
            skip_space(s, p);
            if (p < s.size() && s[p] == ')') {
                ++p;
                return;
            }
            expect(s, p, '|', "in an enumeration");
        }
    }

    // [70] EntityDecl
    void parse_entity_declaration(Text& s, std::size_t& p) {
        const std::size_t start = p;
        p += 8;
        require_space(s, p, "after '<!ENTITY'");
        bool parameter = false;
        if (p < s.size() && s[p] == '%') {
            ++p;
            require_space(s, p, "after '%'");
            parameter = true;
        }
        auto entity = std::make_unique<Entity>();
        entity->name = parse_name(s, p, "an entity declaration");
        // Namespaces in XML, section 6: no colon in the name of an entity.
        if (entity->name.find(':') != std::string::npos) fail(s, start, "the name of an entity cannot hold a colon");
        require_space(s, p, "after the name of an entity");
        if (p < s.size() && (s[p] == '"' || s[p] == '\'')) {
            entity->value = parse_entity_value(s, p);
        } else {
            std::string public_id, system_id;
            bool has_public = false, has_system = false;
            if (!parse_external_id(s, p, public_id, system_id, has_public, has_system, false)) {
                fail(s, p, "the value of an entity is expected");
            }
            entity->external = true;
            std::size_t after = p;
            const bool spaced = skip_space(s, after);
            if (looking_at(s, after, "NDATA")) {
                if (parameter) fail(s, after, "a parameter entity cannot be unparsed");
                if (!spaced) fail(s, after, "white space is required before NDATA");
                p = after + 5;
                require_space(s, p, "after NDATA");
                parse_name(s, p, "a notation");
                entity->unparsed = true;
            }
        }
        skip_space(s, p);
        expect(s, p, '>', "at the end of an entity declaration");
        if (!m_keep_declarations) return;
        // The first declaration binds (4.2), and the five names every
        // document has keep their meaning whatever a document says of them.
        auto& table = parameter ? m_parameter_entities : m_entities;
        if (!parameter && predefined(entity->name)) return;
        const std::string name = entity->name;
        table.emplace(name, std::move(entity));
    }

    // [9] EntityValue, made into the replacement text (4.5): character
    // references are replaced now, references to general entities are kept
    // for the time the entity is used.
    std::string parse_entity_value(Text& s, std::size_t& p) {
        const char quote = s[p];
        const std::size_t start = p++;
        std::string value;
        for (;;) {
            if (p >= s.size()) fail(s, start, "the value of an entity is not finished");
            const char c = s[p];
            if (c == quote) {
                ++p;
                return value;
            }
            if (c == '%') {
                // "PEs in Internal Subset": between declarations, never in one.
                fail(s, p, "a parameter entity reference is not allowed inside a declaration of the internal subset");
            }
            if (c == '&') {
                if (looking_at(s, p, "&#")) {
                    append_utf8(value, parse_char_ref(s, p));
                    continue;
                }
                const std::size_t at = p++;
                const std::string name = parse_name(s, p, "an entity reference");
                expect(s, p, ';', "after the name of an entity");
                value.append(s, at, p - at);
                continue;
            }
            value += c;
            ++p;
        }
    }

    // [82] NotationDecl
    void parse_notation_declaration(Text& s, std::size_t& p) {
        const std::size_t start = p;
        p += 10;
        require_space(s, p, "after '<!NOTATION'");
        const std::string name = parse_name(s, p, "a notation");
        if (name.find(':') != std::string::npos) fail(s, start, "the name of a notation cannot hold a colon");
        require_space(s, p, "after the name of a notation");
        std::string public_id, system_id;
        bool has_public = false, has_system = false;
        if (!parse_external_id(s, p, public_id, system_id, has_public, has_system, true)) {
            fail(s, p, "SYSTEM or PUBLIC is expected in a notation");
        }
        skip_space(s, p);
        expect(s, p, '>', "at the end of a notation declaration");
        m_handler.notation(name, has_public ? &public_id : nullptr, has_system ? &system_id : nullptr);
    }

    // --- attribute values ----------------------------------------------------------

    static void collapse_spaces(std::string& value) {
        std::string out;
        bool pending = false;
        for (const char c : value) {
            if (c == ' ') {
                pending = !out.empty();
                continue;
            }
            if (pending) out += ' ';
            pending = false;
            out += c;
        }
        value.swap(out);
    }

    // [10] AttValue, normalized (3.3.3). `p` is at the quote. Entities named
    // in the value are replaced by their text, and the entities named there
    // in turn: from a list of texts being read, not by calling itself.
    std::string parse_attribute_value(Text& document, std::size_t& document_pos) {
        if (document_pos >= document.size() || (document[document_pos] != '"' && document[document_pos] != '\'')) {
            fail(document, document_pos, "the value of an attribute is expected, in quotes");
        }
        const char quote = document[document_pos];
        const std::size_t start = document_pos;
        struct Frame {
            Text* text;
            std::size_t pos;
            Entity* entity;
        };
        std::vector<Frame> frames;
        frames.push_back({&document, document_pos + 1, nullptr});
        std::string value;
        for (;;) {
            Frame& top = frames.back();
            Text& s = *top.text;
            std::size_t& p = top.pos;
            if (p >= s.size()) {
                if (!top.entity) fail(document, start, "the value of an attribute is not finished");
                top.entity->open = false;
                frames.pop_back();
                continue;
            }
            const char c = s[p];
            if (c == quote && !top.entity) {
                document_pos = p + 1;
                return value;
            }
            if (c == '<') fail(s, p, "'<' is not allowed in the value of an attribute");
            if (c == '&') {
                if (looking_at(s, p, "&#")) {
                    append_utf8(value, parse_char_ref(s, p));
                    continue;
                }
                const std::size_t at = p++;
                const std::string name = parse_name(s, p, "an entity reference");
                expect(s, p, ';', "after the name of an entity");
                if (const char* text = predefined(name)) {
                    value += text;
                    continue;
                }
                if (frames.size() == 1 && &document == &m_doc) m_reference_pos = at;
                Entity* entity = general_entity(s, at, name);
                if (!entity) continue;
                if (entity->external) fail(s, at, "the value of an attribute cannot refer to the external entity '" + name + "'");
                if (entity->open) fail(s, at, "the entity '" + name + "' refers to itself");
                account_expansion(s, at, entity->value.size());
                entity->open = true;
                // `top` is not used after this.
                frames.push_back({&entity->value, 0, entity});
                continue;
            }
            // A run of ordinary characters, white space made spaces.
            std::size_t end = p;
            while (end < s.size()) {
                const char d = s[end];
                if (d == '<' || d == '&' || (d == quote && !top.entity)) break;
                value += is_space(d) ? ' ' : d;
                ++end;
            }
            p = end;
        }
    }

    // --- namespaces -------------------------------------------------------------------

    void declare(const std::string& prefix, const std::string& uri) {
        m_bindings[prefix].push_back(uri);
        m_declared.push_back(prefix);
    }

    void undeclare_to(std::size_t mark) {
        while (m_declared.size() > mark) {
            auto found = m_bindings.find(m_declared.back());
            found->second.pop_back();
            m_declared.pop_back();
        }
    }

    // The namespace a prefix is bound to; null for a prefix nobody declared.
    const std::string* lookup(const std::string& prefix) const {
        auto found = m_bindings.find(prefix);
        if (found == m_bindings.end() || found->second.empty()) return nullptr;
        return &found->second.back();
    }

    // Splits a qualified name (Namespaces, [7] QName): at most one colon,
    // and a name on each side of it.
    void split_qname(Text& s, std::size_t at, XmlName& name) {
        const auto colon = name.qualified.find(':');
        if (colon == std::string::npos) {
            name.local = name.qualified;
            return;
        }
        const bool one_colon = name.qualified.find(':', colon + 1) == std::string::npos;
        const bool local_is_name = name_length(name.qualified, colon + 1) == name.qualified.size() - colon - 1;
        if (colon == 0 || colon + 1 == name.qualified.size() || !one_colon || !local_is_name) {
            fail(s, at, "'" + name.qualified + "' is not a name with a namespace prefix");
        }
        name.prefix = name.qualified.substr(0, colon);
        name.local = name.qualified.substr(colon + 1);
    }

    // --- elements ---------------------------------------------------------------------

    // [40] STag or [44] EmptyElemTag. `p` is at the '<'.
    void parse_start_tag(Text& s, std::size_t& p) {
        const std::size_t start = p++;
        XmlName name;
        name.qualified = parse_name(s, p, "an element");
        std::vector<XmlAttribute> attributes;
        bool empty = false;
        for (;;) {
            const bool spaced = skip_space(s, p);
            if (p >= s.size()) fail(s, start, "a tag is not finished");
            if (s[p] == '>') {
                ++p;
                break;
            }
            if (s[p] == '/') {
                if (p + 1 >= s.size() || s[p + 1] != '>') fail(s, p, "'>' is expected after '/'");
                p += 2;
                empty = true;
                break;
            }
            if (!spaced) fail(s, p, "white space is required before an attribute");
            XmlAttribute attribute;
            attribute.name.qualified = parse_name(s, p, "an attribute");
            skip_space(s, p);
            expect(s, p, '=', "after the name of an attribute");
            skip_space(s, p);
            attribute.value = parse_attribute_value(s, p);
            attributes.push_back(std::move(attribute));
        }

        // "Unique Att Spec" (3.1), then what the DTD adds and changes.
        if (attributes.size() > 1) {
            if (attributes.size() <= 8) {
                for (std::size_t i = 1; i < attributes.size(); ++i) {
                    for (std::size_t j = 0; j < i; ++j) {
                        if (attributes[i].name.qualified == attributes[j].name.qualified) {
                            fail(s, start, "the attribute '" + attributes[i].name.qualified + "' is given twice");
                        }
                    }
                }
            } else {
                std::unordered_set<std::string_view> seen;
                for (const auto& attribute : attributes) {
                    if (!seen.insert(attribute.name.qualified).second) {
                        fail(s, start, "the attribute '" + attribute.name.qualified + "' is given twice");
                    }
                }
            }
        }
        apply_attribute_declarations(name.qualified, attributes);

        // Namespaces: the declarations of this tag first, then every name.
        const std::size_t mark = m_declared.size();
        for (auto& attribute : attributes) {
            const std::string& qualified = attribute.name.qualified;
            if (qualified == "xmlns") {
                if (attribute.value == kXmlNamespace || attribute.value == kXmlnsNamespace) {
                    fail(s, start, "the default namespace cannot be '" + attribute.value + "'");
                }
                declare(std::string(), attribute.value);
                attribute.name.local = "xmlns";
                attribute.name.namespace_uri = kXmlnsNamespace;
            } else if (qualified.compare(0, 6, "xmlns:") == 0) {
                split_qname(s, start, attribute.name);
                const std::string& prefix = attribute.name.local;
                if (prefix == "xmlns") fail(s, start, "the prefix 'xmlns' cannot be declared");
                if ((prefix == "xml") != (attribute.value == kXmlNamespace)) {
                    fail(s, start, "the prefix 'xml' and the XML namespace belong to each other only");
                }
                if (attribute.value == kXmlnsNamespace) fail(s, start, "no prefix can be bound to the namespace of 'xmlns'");
                if (attribute.value.empty()) fail(s, start, "the prefix '" + prefix + "' cannot be bound to no namespace");
                declare(prefix, attribute.value);
                attribute.name.namespace_uri = kXmlnsNamespace;
            }
        }
        split_qname(s, start, name);
        if (name.prefix.empty()) {
            if (const std::string* uri = lookup(std::string())) name.namespace_uri = *uri;
        } else {
            resolve_prefix(s, start, name);
        }
        std::size_t prefixed = 0;
        for (auto& attribute : attributes) {
            if (!attribute.name.namespace_uri.empty()) continue;      // a declaration, done above
            split_qname(s, start, attribute.name);
            if (attribute.name.prefix.empty()) continue;              // no prefix, no namespace
            resolve_prefix(s, start, attribute.name);
            ++prefixed;
        }
        // Two prefixes for one namespace make two names that are one
        // (Namespaces, section 6.3).
        if (prefixed > 1) {
            std::unordered_set<std::string> seen;
            for (const auto& attribute : attributes) {
                if (attribute.name.prefix.empty() || attribute.name.namespace_uri == kXmlnsNamespace) continue;
                if (!seen.insert(attribute.name.namespace_uri + '\n' + attribute.name.local).second) {
                    fail(s, start, "the attribute '" + attribute.name.local + "' of the namespace '" +
                                   attribute.name.namespace_uri + "' is given twice");
                }
            }
        }

        m_handler.start_element(name, attributes);
        if (empty) {
            m_handler.end_element(name);
            undeclare_to(mark);
        } else {
            m_open.push_back({std::move(name), mark});
        }
    }

    void resolve_prefix(Text& s, std::size_t at, XmlName& name) {
        if (name.prefix == "xml") {
            name.namespace_uri = kXmlNamespace;
            return;
        }
        const std::string* uri = name.prefix == "xmlns" ? nullptr : lookup(name.prefix);
        if (!uri || uri->empty()) fail(s, at, "the namespace prefix '" + name.prefix + "' is not declared");
        name.namespace_uri = *uri;
    }

    // Attribute defaults of the internal subset (3.3.2), and the trimming
    // of values whose declared type is not CDATA (3.3.3).
    void apply_attribute_declarations(const std::string& element, std::vector<XmlAttribute>& attributes) {
        if (m_attlists.empty()) return;
        auto found = m_attlists.find(element);
        if (found == m_attlists.end()) return;
        const auto& declarations = found->second;
        const std::size_t given = attributes.size();
        std::unordered_map<std::string_view, std::size_t> index;
        const bool indexed = given * declarations.size() > 64;
        if (indexed) {
            for (std::size_t i = 0; i < given; ++i) index.emplace(attributes[i].name.qualified, i);
        }
        std::vector<XmlAttribute> defaults;
        for (const auto& declaration : declarations) {
            std::size_t at = given;
            if (indexed) {
                auto hit = index.find(declaration.name);
                if (hit != index.end()) at = hit->second;
            } else {
                for (std::size_t i = 0; i < given; ++i) {
                    if (attributes[i].name.qualified == declaration.name) {
                        at = i;
                        break;
                    }
                }
            }
            if (at < given) {
                if (!declaration.cdata) collapse_spaces(attributes[at].value);
            } else if (declaration.has_default) {
                XmlAttribute attribute;
                attribute.name.qualified = declaration.name;
                attribute.value = declaration.default_value;
                attribute.specified = false;
                defaults.push_back(std::move(attribute));
            }
        }
        for (auto& attribute : defaults) attributes.push_back(std::move(attribute));
    }

    // [42] ETag. `p` is at "</".
    void parse_end_tag(Text& s, std::size_t& p) {
        const std::size_t start = p;
        p += 2;
        const std::string name = parse_name(s, p, "an end tag");
        skip_space(s, p);
        expect(s, p, '>', "at the end of an end tag");
        const Source& source = m_sources.back();
        if (m_open.size() <= source.depth) {
            if (m_open.empty()) fail(s, start, "the end tag '</" + name + ">' has no start tag");
            fail(s, start, "an entity cannot end an element that it did not start");
        }
        if (m_open.back().name.qualified != name) {
            fail(s, start, "the end tag '</" + name + ">' does not match the start tag '<" +
                           m_open.back().name.qualified + ">'");
        }
        m_handler.end_element(m_open.back().name);
        undeclare_to(m_open.back().declared_mark);
        m_open.pop_back();
    }

    // [43] content, of the root element or of a fragment. In a document it
    // starts at the root's start tag and returns when the root has ended.
    void parse_content() {
        const bool fragment = m_options.fragment;
        for (;;) {
            Source& source = m_sources.back();
            Text& s = *source.text;
            std::size_t& p = source.pos;
            if (m_sources.size() == 1) m_reference_pos = p;
            if (p >= s.size()) {
                if (m_open.size() != source.depth) {
                    if (source.entity) fail(s, p, "the entity '" + source.entity->name + "' leaves an element open");
                    fail(s, p, "the element '" + m_open.back().name.qualified + "' is not closed");
                }
                if (!source.entity) return;
                source.entity->open = false;
                m_sources.pop_back();
                continue;
            }
            const char c = s[p];
            if (c == '<') {
                if (m_open.empty() && !fragment && p + 1 < s.size() && (s[p + 1] == '!' || s[p + 1] == '?')) {
                    // Only the root element can stand here: comments and
                    // processing instructions before it were read already.
                    fail(s, p, "only the root element is allowed here");
                }
                if (looking_at(s, p, "</")) {
                    parse_end_tag(s, p);
                    if (m_open.empty() && !fragment) return;
                } else if (looking_at(s, p, "<!--")) {
                    parse_comment(s, p, true);
                } else if (looking_at(s, p, "<![CDATA[")) {
                    const std::size_t end = s.find("]]>", p + 9);
                    if (end == std::string::npos) fail(s, p, "a CDATA section is not finished");
                    m_handler.cdata_section(std::string_view(s).substr(p + 9, end - p - 9));
                    p = end + 3;
                } else if (looking_at(s, p, "<?")) {
                    parse_pi(s, p, false);
                } else if (looking_at(s, p, "<!")) {
                    fail(s, p, "a declaration is not allowed inside an element");
                } else {
                    parse_start_tag(s, p);
                    if (m_open.empty() && !fragment) return;
                }
                continue;
            }
            if (c == '&') {
                if (looking_at(s, p, "&#")) {
                    std::string text;
                    append_utf8(text, parse_char_ref(s, p));
                    m_handler.characters(text);
                    continue;
                }
                const std::size_t at = p++;
                const std::string name = parse_name(s, p, "an entity reference");
                expect(s, p, ';', "after the name of an entity");
                if (const char* text = predefined(name)) {
                    m_handler.characters(text);
                    continue;
                }
                Entity* entity = general_entity(s, at, name);
                if (!entity) continue;
                if (entity->unparsed) fail(s, at, "the unparsed entity '" + name + "' cannot be referred to in content");
                // An external entity is not fetched: a parser that does not
                // validate may pass it over (4.4.3), and a browser does.
                if (entity->external) continue;
                if (entity->open) fail(s, at, "the entity '" + name + "' refers to itself");
                account_expansion(s, at, entity->value.size());
                entity->open = true;
                // `source`, `s` and `p` are not used after this.
                m_sources.push_back({&entity->value, 0, entity, m_open.size()});
                continue;
            }
            // [14] CharData: up to the next markup, and never "]]>".
            std::size_t end = p;
            while (end < s.size() && s[end] != '<' && s[end] != '&') ++end;
            const std::string_view run = std::string_view(s).substr(p, end - p);
            const auto bad = run.find("]]>");
            if (bad != std::string_view::npos) fail(s, p + bad, "']]>' is not allowed in text");
            m_handler.characters(run);
            p = end;
        }
    }

    XmlHandler& m_handler;
    const XmlParseOptions& m_options;
    XmlError m_error;

    /// The document: UTF-8, checked, line ends normalized.
    std::string m_doc;
    /// Where in the document the reference is that the parser is inside of.
    std::size_t m_reference_pos{0};
    std::vector<Source> m_sources;
    std::vector<Open> m_open;

    std::unordered_map<std::string, std::vector<std::string>> m_bindings;
    std::vector<std::string> m_declared;

    std::unordered_map<std::string, std::unique_ptr<Entity>> m_entities;
    std::unordered_map<std::string, std::unique_ptr<Entity>> m_parameter_entities;
    std::unordered_map<std::string, std::vector<AttributeDecl>> m_attlists;
    bool m_standalone{false};
    bool m_has_external_subset{false};
    /// The DTD refers to something that was not read, so a name that is not
    /// declared here may be declared there.
    bool m_has_pe_references{false};
    /// False after such a reference: declarations that follow it may have
    /// been changed by what was not read, and are not used (5.1).
    bool m_keep_declarations{true};
    bool m_xhtml_entities{false};
    std::size_t m_expanded{0};
};

// ---------------------------------------------------------------------------
// Encodings
// ---------------------------------------------------------------------------

enum class Encoding { Utf8, Utf16LE, Utf16BE, Windows1252 };

bool encoding_from_label(std::string_view label, Encoding& out, bool& generic_utf16) {
    std::string lower;
    for (const char c : label) lower += c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
    generic_utf16 = false;
    if (lower == "utf-8" || lower == "utf8") out = Encoding::Utf8;
    else if (lower == "utf-16") { out = Encoding::Utf16LE; generic_utf16 = true; }
    else if (lower == "utf-16le") out = Encoding::Utf16LE;
    else if (lower == "utf-16be") out = Encoding::Utf16BE;
    // The Encoding Standard reads all of these as windows-1252.
    else if (lower == "iso-8859-1" || lower == "iso8859-1" || lower == "latin1" || lower == "l1" || lower == "us-ascii" ||
             lower == "ascii" || lower == "windows-1252" || lower == "cp1252" || lower == "iso_8859-1") out = Encoding::Windows1252;
    else return false;
    return true;
}

const char* encoding_name(Encoding encoding) {
    switch (encoding) {
        case Encoding::Utf8: return "UTF-8";
        case Encoding::Utf16LE: return "UTF-16LE";
        case Encoding::Utf16BE: return "UTF-16BE";
        case Encoding::Windows1252: return "windows-1252";
    }
    return "UTF-8";
}

// The name in `encoding="..."` of an XML declaration at the start of `ascii`.
std::string declared_encoding(const std::string& ascii) {
    // "<?xml " and not "<?xml-stylesheet".
    if (ascii.compare(0, 5, "<?xml") != 0 || ascii.size() < 6 || !is_space(ascii[5])) return std::string();
    const auto end = ascii.find("?>");
    const std::string head = ascii.substr(0, end == std::string::npos ? ascii.size() : end);
    auto at = head.find("encoding");
    if (at == std::string::npos) return std::string();
    at += 8;
    while (at < head.size() && is_space(head[at])) ++at;
    if (at >= head.size() || head[at] != '=') return std::string();
    ++at;
    while (at < head.size() && is_space(head[at])) ++at;
    if (at >= head.size() || (head[at] != '"' && head[at] != '\'')) return std::string();
    const char quote = head[at++];
    const auto close = head.find(quote, at);
    return close == std::string::npos ? std::string() : head.substr(at, close - at);
}

} // namespace

std::string XmlError::describe() const {
    if (line == 0) return message;
    return message + " at line " + std::to_string(line) + ", column " + std::to_string(column);
}

bool parse_xml(std::string_view text, XmlHandler& handler, XmlError& error, const XmlParseOptions& options) {
    Parser parser(handler, options);
    return parser.run(text, error);
}

namespace {

// Which encoding the bytes of an entity are in, and how many bytes of byte
// order mark stand in front. In this order:
//   1. a byte order mark;
//   2. what the transport said, when it named an encoding this build reads;
//   3. what the first bytes look like, and the encoding declaration;
//   4. UTF-8.
// Without a transport the declaration must agree with what the bytes turned
// out to be (4.3.3: a fatal error otherwise). With one it has no say: that
// sentence of 4.3.3 begins "In the absence of information provided by an
// external transport protocol".
bool find_encoding(std::string_view bytes, std::string_view transport_charset, Encoding& found, std::size_t& skip,
                   XmlError& error) {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
    found = Encoding::Utf8;
    skip = 0;
    bool wide = false;          // the bytes are 16-bit code units
    bool from_mark = false;     // a byte order mark said so
    if (bytes.size() >= 3 && byte(0) == 0xEF && byte(1) == 0xBB && byte(2) == 0xBF) {
        from_mark = true;
        skip = 3;
    } else if (bytes.size() >= 2 && byte(0) == 0xFE && byte(1) == 0xFF) {
        found = Encoding::Utf16BE;
        wide = from_mark = true;
        skip = 2;
    } else if (bytes.size() >= 2 && byte(0) == 0xFF && byte(1) == 0xFE) {
        found = Encoding::Utf16LE;
        wide = from_mark = true;
        skip = 2;
    } else if (bytes.size() >= 4 && byte(0) == 0 && byte(1) == '<' && byte(2) == 0 && byte(3) == '?') {
        found = Encoding::Utf16BE;
        wide = true;
    } else if (bytes.size() >= 4 && byte(0) == '<' && byte(1) == 0 && byte(2) == '?' && byte(3) == 0) {
        found = Encoding::Utf16LE;
        wide = true;
    }

    {
        Encoding named = Encoding::Utf8;
        bool generic = false;
        if (!transport_charset.empty() && encoding_from_label(transport_charset, named, generic)) {
            // "UTF-16" with no mark is little-endian on the web (the
            // Encoding Standard), which is what `named` holds for it.
            if (!from_mark) found = named;
            return true;
        }
    }

    // The start of the text as ASCII, enough to read an XML declaration.
    std::string head;
    const std::size_t step = wide ? 2 : 1;
    const std::size_t offset = wide && found == Encoding::Utf16BE ? 1 : 0;
    for (std::size_t i = skip; i + step <= bytes.size() && head.size() < 512; i += step) {
        head += static_cast<char>(byte(i + offset) < 0x80 ? byte(i + offset) : '?');
    }
    const std::string declared = declared_encoding(head);
    if (declared.empty()) return true;

    Encoding named = Encoding::Utf8;
    bool generic = false;
    if (!encoding_from_label(declared, named, generic)) {
        error.message = "the encoding '" + declared + "' is not supported";
        error.unsupported_encoding = true;
        return false;
    }
    const bool named_wide = named == Encoding::Utf16LE || named == Encoding::Utf16BE;
    if (from_mark) {
        if (named_wide != wide || (!wide && named != Encoding::Utf8) || (wide && !generic && named != found)) {
            error.message = "the encoding declaration '" + declared + "' contradicts the byte order mark";
            return false;
        }
        return true;
    }
    // The declaration could be read as one kind of text and names the other.
    if (named_wide != wide || (wide && !generic && named != found)) {
        error.message = "the encoding declaration '" + declared + "' contradicts the bytes of the document";
        return false;
    }
    if (!wide) found = named;
    return true;
}

} // namespace

bool xml_encoding(std::string_view bytes, std::string& encoding, XmlError& error, std::string_view transport_charset) {
    Encoding found = Encoding::Utf8;
    std::size_t skip = 0;
    if (!find_encoding(bytes, transport_charset, found, skip, error)) return false;
    encoding = encoding_name(found);
    return true;
}

bool decode_xml(std::string_view bytes, std::string& utf8, std::string& encoding, XmlError& error,
                std::string_view transport_charset) {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
    Encoding found = Encoding::Utf8;
    std::size_t skip = 0;
    if (!find_encoding(bytes, transport_charset, found, skip, error)) return false;

    encoding = encoding_name(found);
    utf8.clear();
    utf8.reserve(bytes.size());
    if (found == Encoding::Utf8) {
        utf8.assign(bytes.data() + skip, bytes.size() - skip);
        return true;
    }
    if (found == Encoding::Windows1252) {
        static const char32_t kHigh[32] = {
            0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
            0x2039, 0x0152, 0x8D,   0x017D, 0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
            0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178,
        };
        for (std::size_t i = skip; i < bytes.size(); ++i) {
            const unsigned char b = byte(i);
            if (b < 0x80) utf8 += static_cast<char>(b);
            else append_utf8(utf8, b < 0xA0 ? kHigh[b - 0x80] : b);
        }
        return true;
    }
    if ((bytes.size() - skip) % 2 != 0) {
        error.message = "the document is UTF-16 and ends in half a character";
        return false;
    }
    const auto unit = [&](std::size_t i) -> char32_t {
        return found == Encoding::Utf16LE ? static_cast<char32_t>(byte(i) | (byte(i + 1) << 8))
                                          : static_cast<char32_t>((byte(i) << 8) | byte(i + 1));
    };
    for (std::size_t i = skip; i + 1 < bytes.size(); i += 2) {
        char32_t c = unit(i);
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 3 >= bytes.size()) {
                error.message = "the document is UTF-16 and ends in half a character";
                return false;
            }
            const char32_t low = unit(i + 2);
            if (low < 0xDC00 || low > 0xDFFF) {
                error.message = "the document is UTF-16 and has half a surrogate pair";
                return false;
            }
            c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
            i += 2;
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            error.message = "the document is UTF-16 and has half a surrogate pair";
            return false;
        }
        append_utf8(utf8, c);
    }
    return true;
}

bool is_xml_name(std::string_view name) {
    return !name.empty() && name_length(name, 0) == name.size();
}

bool is_xml_qname(std::string_view name) {
    if (!is_xml_name(name)) return false;
    const auto colon = name.find(':');
    if (colon == std::string_view::npos) return true;
    if (colon == 0 || colon + 1 == name.size() || name.find(':', colon + 1) != std::string_view::npos) return false;
    return name_length(name, colon + 1) == name.size() - colon - 1;
}

} // namespace xml
} // namespace litebird
