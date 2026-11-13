#include "litebird/dom/Selector.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/dom/FormState.hpp"
#include "litebird/dom/ShadowRoot.hpp"
#include "litebird/dom/TreeWalk.hpp"

#include <algorithm>
#include <cstdint>

namespace litebird {

using namespace selector_detail;

namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_hex(char c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool is_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}
bool is_name_char(char c) { return is_name_start(c) || is_digit(c) || c == '-'; }

std::string ascii_lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
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

} // namespace

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

struct SelectorParser {
    std::string_view text;
    std::size_t pos{0};
    int depth{0};

    bool at_end() const { return pos >= text.size(); }
    char peek(std::size_t ahead = 0) const { return pos + ahead < text.size() ? text[pos + ahead] : '\0'; }
    bool skip_space() {
        const std::size_t start = pos;
        while (!at_end() && is_space(text[pos])) ++pos;
        return pos > start;
    }

    // An escape, the backslash already seen to be there.
    bool read_escape(std::string& out) {
        ++pos;                              // the backslash
        if (at_end() || text[pos] == '\n') return false;
        if (is_hex(text[pos])) {
            uint32_t cp = 0;
            int digits = 0;
            while (!at_end() && is_hex(text[pos]) && digits < 6) {
                const char c = text[pos++];
                cp = cp * 16 + static_cast<uint32_t>(is_digit(c) ? c - '0' : (c | 0x20) - 'a' + 10);
                ++digits;
            }
            if (!at_end() && is_space(text[pos])) ++pos;    // one space ends the escape
            append_utf8(out, cp);
            return true;
        }
        out += text[pos++];
        return true;
    }

    bool starts_identifier() const {
        char c = peek();
        if (c == '-') {
            char d = peek(1);
            return is_name_start(d) || d == '-' || (d == '\\' && peek(2) != '\n' && pos + 2 < text.size());
        }
        if (c == '\\') return pos + 1 < text.size() && peek(1) != '\n';
        return is_name_start(c);
    }

    bool read_identifier(std::string& out) {
        if (!starts_identifier()) return false;
        while (!at_end()) {
            char c = text[pos];
            if (is_name_char(c)) { out += c; ++pos; }
            else if (c == '\\') { if (!read_escape(out)) return false; }
            else break;
        }
        return !out.empty();
    }

    bool read_string(std::string& out) {
        const char quote = text[pos++];
        while (!at_end()) {
            char c = text[pos];
            if (c == quote) { ++pos; return true; }
            if (c == '\n') return false;
            if (c == '\\') {
                if (pos + 1 < text.size() && text[pos + 1] == '\n') { pos += 2; continue; }
                if (!read_escape(out)) return false;
                continue;
            }
            out += c;
            ++pos;
        }
        return false;                       // never closed
    }

    bool read_integer(int& out) {
        if (!is_digit(peek())) return false;
        long value = 0;
        while (is_digit(peek())) {
            value = std::min<long>(value * 10 + (text[pos] - '0'), 100000000L);
            ++pos;
        }
        out = static_cast<int>(value);
        return true;
    }

    // an+b, `odd`, `even`.
    bool read_nth(int& a, int& b) {
        skip_space();
        auto word = [&](std::string_view w) {
            if (text.size() - pos < w.size()) return false;
            for (std::size_t i = 0; i < w.size(); ++i) {
                if ((text[pos + i] | 0x20) != w[i]) return false;
            }
            if (is_name_char(peek(w.size()))) return false;
            pos += w.size();
            return true;
        };
        if (word("odd")) { a = 2; b = 1; return true; }
        if (word("even")) { a = 2; b = 0; return true; }
        int sign = 1;
        bool signed_ = false;
        if (peek() == '+' || peek() == '-') { sign = peek() == '-' ? -1 : 1; ++pos; signed_ = true; }
        int number = 0;
        const bool has_number = read_integer(number);
        if ((peek() | 0x20) == 'n') {
            ++pos;
            a = sign * (has_number ? number : 1);
            b = 0;
            skip_space();
            if (peek() == '+' || peek() == '-') {
                const int bsign = peek() == '-' ? -1 : 1;
                ++pos;
                skip_space();
                int offset = 0;
                if (!read_integer(offset)) return false;
                b = bsign * offset;
            }
            return true;
        }
        if (!has_number) return false;
        (void)signed_;
        a = 0;
        b = sign * number;
        return true;
    }

    bool parse_list(SelectorList& out, bool relative, char closing) {
        if (++depth > 64) return false;
        out.m_relative = relative;
        for (;;) {
            Complex complex;
            if (!parse_complex(complex, relative)) return false;
            out.m_selectors.push_back(std::move(complex));
            skip_space();
            if (peek() == ',') { ++pos; continue; }
            break;
        }
        --depth;
        if (closing) {
            if (peek() != closing) return false;
            ++pos;
            return true;
        }
        return at_end();
    }

    bool parse_complex(Complex& out, bool relative) {
        skip_space();
        if (relative && (peek() == '>' || peek() == '+' || peek() == '~')) {
            out.leading = text[pos++];
            skip_space();
        }
        for (;;) {
            Compound compound;
            if (!parse_compound(compound)) return false;
            out.compounds.push_back(std::move(compound));
            const bool space = skip_space();
            char c = peek();
            if (c == '>' || c == '+' || c == '~') {
                out.combinators.push_back(c);
                ++pos;
                skip_space();
                continue;
            }
            if (space && !at_end() && c != ',' && c != ')') {
                out.combinators.push_back(' ');
                continue;
            }
            return true;
        }
    }

    bool parse_compound(Compound& out) {
        bool any = false;
        if (peek() == '*') {
            ++pos;
            any = true;
        } else if (starts_identifier()) {
            if (!read_identifier(out.type)) return false;
            out.type_lower = ascii_lower(out.type);
            out.any_type = false;
            any = true;
        }
        for (;;) {
            char c = peek();
            if (c == '#') {
                ++pos;
                std::string id;
                if (!read_identifier(id)) return false;
                out.ids.push_back(std::move(id));
            } else if (c == '.') {
                ++pos;
                std::string name;
                if (!read_identifier(name)) return false;
                out.classes.push_back(std::move(name));
            } else if (c == '[') {
                ++pos;
                AttributePart part;
                if (!parse_attribute(part)) return false;
                out.attributes.push_back(std::move(part));
            } else if (c == ':') {
                ++pos;
                PseudoPart part;
                if (!parse_pseudo(part)) return false;
                out.pseudos.push_back(std::move(part));
            } else {
                break;
            }
            any = true;
        }
        return any;
    }

    bool parse_attribute(AttributePart& out) {
        skip_space();
        if (!read_identifier(out.name)) return false;
        out.name_lower = ascii_lower(out.name);
        skip_space();
        if (peek() == ']') { ++pos; return true; }
        char c = peek();
        if (c == '=') { out.op = '='; ++pos; }
        else if ((c == '~' || c == '|' || c == '^' || c == '$' || c == '*') && peek(1) == '=') { out.op = c; pos += 2; }
        else return false;
        skip_space();
        if (peek() == '"' || peek() == '\'') {
            if (!read_string(out.value)) return false;
        } else if (!read_identifier(out.value)) {
            return false;
        }
        skip_space();
        if ((peek() | 0x20) == 'i' || (peek() | 0x20) == 's') {
            out.ignore_case = (peek() | 0x20) == 'i';
            ++pos;
            skip_space();
        }
        if (peek() != ']') return false;
        ++pos;
        return true;
    }

    bool parse_pseudo(PseudoPart& out) {
        bool element = false;
        if (peek() == ':') { ++pos; element = true; }
        std::string name;
        if (!read_identifier(name)) return false;
        name = ascii_lower(name);
        const bool functional = peek() == '(';

        if (element) {
            // A pseudo-element: valid to write, and nothing an element is.
            static const char* const known[] = {"before", "after", "first-line", "first-letter", "selection",
                                                 "placeholder", "marker", "backdrop", "file-selector-button",
                                                 "slotted", "part", "cue"};
            if (std::find(std::begin(known), std::end(known), name) == std::end(known)) return false;
            out.kind = Pseudo::Never;
            if (functional) return skip_balanced();
            return true;
        }

        struct Simple { const char* name; Pseudo kind; };
        static const Simple simple[] = {
            {"first-child", Pseudo::FirstChild}, {"last-child", Pseudo::LastChild}, {"only-child", Pseudo::OnlyChild},
            {"first-of-type", Pseudo::FirstOfType}, {"last-of-type", Pseudo::LastOfType},
            {"only-of-type", Pseudo::OnlyOfType}, {"empty", Pseudo::Empty}, {"root", Pseudo::Root},
            {"scope", Pseudo::Scope}, {"checked", Pseudo::Checked}, {"disabled", Pseudo::Disabled},
            {"enabled", Pseudo::Enabled}, {"required", Pseudo::Required}, {"optional", Pseudo::Optional},
            {"read-only", Pseudo::ReadOnly}, {"read-write", Pseudo::ReadWrite},
            {"placeholder-shown", Pseudo::PlaceholderShown}, {"valid", Pseudo::Valid}, {"invalid", Pseudo::Invalid},
            {"link", Pseudo::Link}, {"any-link", Pseudo::Link}, {"hover", Pseudo::Hover}, {"active", Pseudo::Active},
            {"focus", Pseudo::Focus}, {"focus-visible", Pseudo::Focus}, {"focus-within", Pseudo::FocusWithin},
            {"target", Pseudo::Target}, {"defined", Pseudo::Defined},
            // States nothing is in here, and the old one-colon pseudo-elements.
            {"visited", Pseudo::Never}, {"indeterminate", Pseudo::Never}, {"default", Pseudo::Never},
            {"in-range", Pseudo::Never}, {"out-of-range", Pseudo::Never}, {"autofill", Pseudo::Never},
            {"fullscreen", Pseudo::Never}, {"modal", Pseudo::Never}, {"popover-open", Pseudo::Never},
            {"user-valid", Pseudo::Never}, {"user-invalid", Pseudo::Never}, {"target-within", Pseudo::Never},
            {"before", Pseudo::Never}, {"after", Pseudo::Never}, {"first-line", Pseudo::Never},
            {"first-letter", Pseudo::Never},
        };
        if (!functional) {
            for (const auto& entry : simple) {
                if (name == entry.name) { out.kind = entry.kind; return true; }
            }
            return false;
        }

        ++pos;                              // the '('
        if (name == "nth-child" || name == "nth-last-child" || name == "nth-of-type" || name == "nth-last-of-type") {
            out.kind = name == "nth-child" ? Pseudo::NthChild : name == "nth-last-child" ? Pseudo::NthLastChild
                     : name == "nth-of-type" ? Pseudo::NthOfType : Pseudo::NthLastOfType;
            if (!read_nth(out.a, out.b)) return false;
            const bool space = skip_space();
            if (space && (out.kind == Pseudo::NthChild || out.kind == Pseudo::NthLastChild) &&
                (peek() | 0x20) == 'o' && (peek(1) | 0x20) == 'f' && is_space(peek(2))) {
                pos += 2;
                out.list = std::make_shared<SelectorList>();
                return parse_list(*out.list, false, ')');
            }
            if (peek() != ')') return false;
            ++pos;
            return true;
        }
        if (name == "not" || name == "is" || name == "where" || name == "matches" || name == "has") {
            out.kind = name == "not" ? Pseudo::Not : name == "has" ? Pseudo::Has : Pseudo::Is;
            out.list = std::make_shared<SelectorList>();
            return parse_list(*out.list, out.kind == Pseudo::Has, ')');
        }
        if (name == "lang" || name == "dir" || name == "host" || name == "host-context" || name == "state") {
            out.kind = Pseudo::Never;
            --pos;
            return skip_balanced();
        }
        return false;
    }

    // Skips "( ... )" starting at the '('. The contents are not looked at.
    bool skip_balanced() {
        int open = 0;
        while (!at_end()) {
            char c = text[pos++];
            if (c == '(') ++open;
            else if (c == ')' && --open == 0) return true;
            else if (c == '"' || c == '\'') {
                --pos;
                std::string ignored;
                if (!read_string(ignored)) return false;
            }
        }
        return false;
    }
};

bool SelectorList::parse(std::string_view text, SelectorList& out) {
    out.m_selectors.clear();
    SelectorParser parser{text};
    return parser.parse_list(out, false, '\0') && !out.m_selectors.empty();
}

// ---------------------------------------------------------------------------
// Matching
// ---------------------------------------------------------------------------

namespace {

const Element* parent_element(const Node& node) {
    auto parent = node.parent();
    return parent && parent->is_element() ? static_cast<const Element*>(parent.get()) : nullptr;
}

// The element children of `node`'s parent, or an empty list for a node
// without one.
const std::vector<std::shared_ptr<Node>>& siblings_of(const Node& node, std::shared_ptr<Node>& keep) {
    static const std::vector<std::shared_ptr<Node>> none;
    keep = node.parent();
    return keep ? keep->children() : none;
}

const Element* previous_element(const Element& element) {
    std::shared_ptr<Node> keep;
    const auto& siblings = siblings_of(element, keep);
    const Element* previous = nullptr;
    for (const auto& sibling : siblings) {
        if (sibling.get() == &element) return previous;
        if (sibling->is_element()) previous = static_cast<const Element*>(sibling.get());
    }
    return nullptr;
}

bool match_attribute(const Element& element, const AttributePart& part) {
    const std::string* found = element.attribute(element.is_foreign() ? part.name : part.name_lower);
    if (!found && element.is_foreign()) found = element.attribute(part.name_lower);
    if (!found) return false;
    if (part.op == 0) return true;
    std::string value = *found;
    std::string wanted = part.value;
    if (part.ignore_case) {
        value = ascii_lower(value);
        wanted = ascii_lower(wanted);
    }
    switch (part.op) {
        case '=': return value == wanted;
        case '~': {
            if (wanted.empty() || wanted.find_first_of(" \t\n\r\f") != std::string::npos) return false;
            std::size_t i = 0;
            while (i < value.size()) {
                while (i < value.size() && is_space(value[i])) ++i;
                std::size_t start = i;
                while (i < value.size() && !is_space(value[i])) ++i;
                if (i > start && value.compare(start, i - start, wanted) == 0) return true;
            }
            return false;
        }
        case '|': return value == wanted || (value.size() > wanted.size() && value.compare(0, wanted.size(), wanted) == 0 &&
                                              value[wanted.size()] == '-');
        case '^': return !wanted.empty() && value.compare(0, wanted.size(), wanted) == 0;
        case '$': return !wanted.empty() && value.size() >= wanted.size() &&
                         value.compare(value.size() - wanted.size(), wanted.size(), wanted) == 0;
        case '*': return !wanted.empty() && value.find(wanted) != std::string::npos;
        default: return false;
    }
}

bool nth_matches(int a, int b, int index) {
    // index = a*n + b for some n >= 0
    const int diff = index - b;
    if (a == 0) return diff == 0;
    return diff % a == 0 && diff / a >= 0;
}

} // namespace

struct SelectorMatcher {
    const Node* scope;

    bool match_list(const SelectorList& list, const Element& element) const {
        for (const auto& complex : list.m_selectors) {
            if (match_complex(complex, complex.compounds.size() - 1, element, nullptr)) return true;
        }
        return false;
    }

    // Does `element` match compounds[0..index], `element` being the subject
    // of compounds[index]? `anchor` is the element a :has() is asked about.
    bool match_complex(const Complex& complex, std::size_t index, const Element& element, const Element* anchor) const {
        // Walks left through the combinators. Descendant and later-sibling
        // combinators need a choice of ancestor or sibling; each choice is
        // tried by a recursive call whose depth is the number of compounds in
        // the selector, never the depth of the tree.
        if (!match_compound(complex.compounds[index], element)) return false;
        if (index == 0) {
            if (!anchor) return true;
            switch (complex.leading) {
                case '>': return parent_element(element) == anchor;
                case '+': return previous_element(element) == anchor;
                case '~':
                    for (const Element* e = previous_element(element); e; e = previous_element(*e)) {
                        if (e == anchor) return true;
                    }
                    return false;
                default:
                    for (const Element* e = parent_element(element); e; e = parent_element(*e)) {
                        if (e == anchor) return true;
                    }
                    return false;
            }
        }
        switch (complex.combinators[index - 1]) {
            case '>': {
                const Element* parent = parent_element(element);
                return parent && match_complex(complex, index - 1, *parent, anchor);
            }
            case '+': {
                const Element* previous = previous_element(element);
                return previous && match_complex(complex, index - 1, *previous, anchor);
            }
            case '~': {
                std::shared_ptr<Node> keep;
                for (const auto& sibling : siblings_of(element, keep)) {
                    if (sibling.get() == &element) break;
                    if (sibling->is_element() &&
                        match_complex(complex, index - 1, static_cast<const Element&>(*sibling), anchor)) return true;
                }
                return false;
            }
            default: {
                std::shared_ptr<Node> keep = element.parent();
                while (keep && keep->is_element()) {
                    if (match_complex(complex, index - 1, static_cast<const Element&>(*keep), anchor)) return true;
                    keep = keep->parent();
                }
                return false;
            }
        }
    }

    bool match_compound(const Compound& compound, const Element& element) const {
        if (!compound.any_type) {
            if (element.is_foreign()) {
                if (element.tag_name() != compound.type) return false;
            } else if (element.tag_name() != compound.type_lower) {
                return false;
            }
        }
        for (const auto& id : compound.ids) {
            const std::string* value = element.attribute("id");
            if (!value || *value != id) return false;
        }
        for (const auto& name : compound.classes) {
            if (!element.has_class(name)) return false;
        }
        for (const auto& part : compound.attributes) {
            if (!match_attribute(element, part)) return false;
        }
        for (const auto& part : compound.pseudos) {
            if (!match_pseudo(part, element)) return false;
        }
        return true;
    }

    // The 1-based position of `element` among its siblings, counted from the
    // front or the back, among all element siblings, those of its type, or
    // those matching `of`. 0 when it has no parent.
    int position(const Element& element, bool from_end, bool same_type, const SelectorList* of) const {
        std::shared_ptr<Node> keep;
        const auto& siblings = siblings_of(element, keep);
        if (siblings.empty()) return 1;         // a root: the only one of its kind
        int before = 0, total = 0, mine = 0;
        for (const auto& sibling : siblings) {
            if (!sibling->is_element()) continue;
            const auto& other = static_cast<const Element&>(*sibling);
            if (same_type && (other.tag_name() != element.tag_name() ||
                              other.element_namespace() != element.element_namespace())) continue;
            if (of && !match_list(*of, other)) continue;
            ++total;
            if (&other == &element) mine = total;
        }
        if (mine == 0) return 0;
        before = mine;
        return from_end ? total - before + 1 : before;
    }

    bool match_pseudo(const PseudoPart& part, const Element& element) const {
        switch (part.kind) {
            case Pseudo::FirstChild: return position(element, false, false, nullptr) == 1;
            case Pseudo::LastChild: return position(element, true, false, nullptr) == 1;
            case Pseudo::OnlyChild:
                return position(element, false, false, nullptr) == 1 && position(element, true, false, nullptr) == 1;
            case Pseudo::FirstOfType: return position(element, false, true, nullptr) == 1;
            case Pseudo::LastOfType: return position(element, true, true, nullptr) == 1;
            case Pseudo::OnlyOfType:
                return position(element, false, true, nullptr) == 1 && position(element, true, true, nullptr) == 1;
            case Pseudo::NthChild:
            case Pseudo::NthLastChild: {
                const int index = position(element, part.kind == Pseudo::NthLastChild, false, part.list.get());
                return index > 0 && nth_matches(part.a, part.b, index);
            }
            case Pseudo::NthOfType:
            case Pseudo::NthLastOfType: {
                const int index = position(element, part.kind == Pseudo::NthLastOfType, true, nullptr);
                return index > 0 && nth_matches(part.a, part.b, index);
            }
            case Pseudo::Not: return !match_list(*part.list, element);
            case Pseudo::Is: return match_list(*part.list, element);
            case Pseudo::Has: return match_has(*part.list, element);
            case Pseudo::Empty:
                for (const auto& child : element.children()) {
                    if (child->is_element()) return false;
                    if (child->is_text() && !child->text_content().empty()) return false;
                }
                return true;
            case Pseudo::Root: {
                auto parent = element.parent();
                return parent && parent->is_document();
            }
            case Pseudo::Scope:
                if (scope && scope->is_element()) return scope == &element;
                {
                    // A document or a fragment as scope, or none: the root element.
                    auto parent = element.parent();
                    return parent && parent->is_document();
                }
            case Pseudo::Checked: return forms::matches_checked(element);
            case Pseudo::Disabled: return forms::is_disabled(element);
            case Pseudo::Enabled: return forms::can_be_disabled(element) && !forms::is_disabled(element);
            case Pseudo::Required: return forms::is_required(element);
            case Pseudo::Optional: return forms::can_be_required(element) && !forms::is_required(element);
            case Pseudo::ReadWrite: return forms::is_read_write(element);
            case Pseudo::ReadOnly: return !forms::is_read_write(element);
            case Pseudo::PlaceholderShown: return forms::placeholder_shown(element);
            case Pseudo::Valid: return forms::validity_for_selector(element) == 1;
            case Pseudo::Invalid: return forms::validity_for_selector(element) == 2;
            case Pseudo::Link:
                return !element.is_foreign() && (element.tag_name() == "a" || element.tag_name() == "area") &&
                       element.has_attribute("href");
            case Pseudo::Hover: return element.is_hovered();
            case Pseudo::Active: return element.is_active();
            case Pseudo::Focus: {
                auto document = element.owner_document();
                return document ? document->focused_element().get() == &element : element.is_focused();
            }
            case Pseudo::FocusWithin: {
                auto document = element.owner_document();
                if (!document) return false;
                std::shared_ptr<Node> at = document->focused_element();
                while (at) {
                    if (at.get() == &element) return true;
                    at = at->parent();
                }
                return false;
            }
            case Pseudo::Target: {
                auto document = element.owner_document();
                if (!document) return false;
                const std::string& url = document->url();
                auto hash = url.find('#');
                if (hash == std::string::npos || hash + 1 >= url.size()) return false;
                const std::string* id = element.attribute("id");
                return id && *id == url.substr(hash + 1);
            }
            case Pseudo::Defined: return element.is_defined();
            case Pseudo::Never: return false;
        }
        return false;
    }

    bool match_has(const SelectorList& list, const Element& element) const {
        for (const auto& complex : list.m_selectors) {
            const std::size_t last = complex.compounds.size() - 1;
            bool found = false;
            auto try_subtree = [&](const Node& root, bool include_root) {
                if (include_root && root.is_element() &&
                    match_complex(complex, last, static_cast<const Element&>(root), &element)) {
                    found = true;
                    return;
                }
                for_each_descendant(root, [&](Node& node) {
                    if (node.is_element() && match_complex(complex, last, static_cast<const Element&>(node), &element)) {
                        found = true;
                        return false;
                    }
                    return true;
                });
            };
            if (complex.leading == '+' || complex.leading == '~') {
                std::shared_ptr<Node> keep;
                bool after = false;
                for (const auto& sibling : siblings_of(element, keep)) {
                    if (sibling.get() == &element) { after = true; continue; }
                    if (after) try_subtree(*sibling, true);
                    if (found) break;
                }
            } else {
                try_subtree(element, false);
            }
            if (found) return true;
        }
        return false;
    }
};

bool SelectorList::matches(const Element& element, const Node* scope) const {
    return SelectorMatcher{scope}.match_list(*this, element);
}

std::vector<std::shared_ptr<Element>> SelectorList::select(const Node& root, bool first_only) const {
    std::vector<std::shared_ptr<Element>> found;
    SelectorMatcher matcher{&root};
    for_each_descendant(root, [&](Node& node) {
        if (!node.is_element()) return true;
        auto& element = static_cast<Element&>(node);
        if (!matcher.match_list(*this, element)) return true;
        found.push_back(std::static_pointer_cast<Element>(element.shared_from_this()));
        return !first_only;
    });
    return found;
}

std::string css_escape_identifier(std::string_view text) {
    std::string out;
    static const char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0) { out += "\xEF\xBF\xBD"; continue; }
        const bool leading_digit = is_digit(static_cast<char>(c)) &&
                                   (i == 0 || (i == 1 && text[0] == '-'));
        if (c < 0x20 || c == 0x7F || leading_digit) {
            out += '\\';
            if (c >= 16) out += hex[c >> 4];
            out += hex[c & 15];
            out += ' ';
        } else if (i == 0 && c == '-' && text.size() == 1) {
            out += "\\-";
        } else if (c >= 0x80 || c == '-' || c == '_' || is_digit(static_cast<char>(c)) ||
                   (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            out += static_cast<char>(c);
        } else {
            out += '\\';
            out += static_cast<char>(c);
        }
    }
    return out;
}

} // namespace litebird
