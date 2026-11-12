// RegExp: a backtracking regular-expression engine and the RegExp object.
//
// The matcher works on UTF-16 code units, so indices agree with the string
// methods. Two limits keep hostile patterns from taking the process down:
// recursion stops before the native stack is exhausted, and a step budget ends
// catastrophic backtracking. Both surface to script as a RangeError.

#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/common/StackLimit.hpp"
#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

JsValue self() { return JsValue::current_this(); }

const size_t kUnbounded = std::numeric_limits<size_t>::max();
const uint64_t kMaxSteps = 30'000'000;

// ---------------------------------------------------------------------------
// Program representation
// ---------------------------------------------------------------------------

struct CharClass {
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    bool negated{false};

    void add(uint32_t lo, uint32_t hi) { ranges.emplace_back(lo, hi); }

    bool contains(uint32_t c) const {
        for (const auto& [lo, hi] : ranges) {
            if (c >= lo && c <= hi) return true;
        }
        return false;
    }

    /// Adds everything not covered by `other`'s ranges.
    void add_complement(const CharClass& other) {
        std::vector<std::pair<uint32_t, uint32_t>> sorted = other.ranges;
        std::sort(sorted.begin(), sorted.end());
        uint32_t next = 0;
        for (const auto& [lo, hi] : sorted) {
            if (lo > next) add(next, lo - 1);
            if (hi + 1 > next) next = hi + 1;
        }
        if (next <= 0x10FFFF) add(next, 0x10FFFF);
    }
};

struct Node {
    enum class Type {
        Empty, Char, Any, Class, LineStart, LineEnd, WordBoundary, NotWordBoundary,
        Group, Alternation, Sequence, Repeat, BackReference, LookAhead, LookBehind
    };
    Type type{Type::Empty};
    uint32_t ch{0};
    CharClass klass;
    std::vector<std::unique_ptr<Node>> children;
    int group{-1};                  // Group: capture index or -1; BackReference: target group
    std::string name;               // BackReference by name, resolved after parsing
    size_t min{0};
    size_t max{0};
    bool greedy{true};
    bool negate{false};
    int first_group{0};             // Repeat: capture groups inside the repeated atom,
    int last_group{-1};             //         cleared at the start of each iteration
};

struct Program {
    std::unique_ptr<Node> root;
    int group_count{1};             // including group 0, the whole match
    std::vector<std::pair<std::string, size_t>> names;
    std::string source;
    std::string flags;
    bool global{false}, ignore_case{false}, multiline{false}, dot_all{false};
    bool unicode{false}, sticky{false}, has_indices{false};
};

bool is_line_terminator(uint32_t c) {
    return c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029;
}

bool is_word_char(uint32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Simple case folding for the scripts that have case in everyday text.
uint32_t fold_case(uint32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c < 0x80) return c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x100 && c <= 0x17F) {
        bool odd_is_lower = (c < 0x139) || (c >= 0x14A && c <= 0x177);
        if (c == 0x130 || c == 0x131 || c == 0x138 || c == 0x149 || c == 0x17F) return c;
        if (odd_is_lower) return (c & 1) ? c : c + 1;
        return (c & 1) ? c + 1 : c;
    }
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

uint32_t unfold_case(uint32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c < 0x80) return c;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c >= 0x100 && c <= 0x17F) {
        bool odd_is_lower = (c < 0x139) || (c >= 0x14A && c <= 0x177);
        if (c == 0x130 || c == 0x131 || c == 0x138 || c == 0x149 || c == 0x17F) return c;
        if (odd_is_lower) return (c & 1) ? c - 1 : c;
        return (c & 1) ? c : c - 1;
    }
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}

void add_digit(CharClass& c) { c.add('0', '9'); }
void add_word(CharClass& c) { c.add('a', 'z'); c.add('A', 'Z'); c.add('0', '9'); c.add('_', '_'); }
void add_space(CharClass& c) {
    c.add(0x09, 0x0D); c.add(0x20, 0x20); c.add(0xA0, 0xA0); c.add(0x1680, 0x1680);
    c.add(0x2000, 0x200A); c.add(0x2028, 0x2029); c.add(0x202F, 0x202F); c.add(0x205F, 0x205F);
    c.add(0x3000, 0x3000); c.add(0xFEFF, 0xFEFF);
}

/// Approximate Unicode property tables: enough for the scripts in common use.
bool add_unicode_property(CharClass& c, const std::string& raw_name) {
    std::string name = raw_name;
    size_t eq = name.find('=');
    std::string key = eq == std::string::npos ? "" : name.substr(0, eq);
    if (eq != std::string::npos) name = name.substr(eq + 1);

    auto letters_upper = [&]() {
        c.add('A', 'Z'); c.add(0xC0, 0xD6); c.add(0xD8, 0xDE); c.add(0x391, 0x3A9); c.add(0x400, 0x42F);
    };
    auto letters_lower = [&]() {
        c.add('a', 'z'); c.add(0xDF, 0xF6); c.add(0xF8, 0xFF); c.add(0x3B1, 0x3C9); c.add(0x430, 0x45F);
    };
    auto letters_other = [&]() {
        c.add(0xAA, 0xAA); c.add(0xB5, 0xB5); c.add(0xBA, 0xBA); c.add(0x100, 0x2C1); c.add(0x370, 0x390);
        c.add(0x3AA, 0x3B0); c.add(0x3CA, 0x3FF); c.add(0x460, 0x52F); c.add(0x531, 0x587);
        c.add(0x5D0, 0x5EA); c.add(0x620, 0x64A); c.add(0x671, 0x6D3); c.add(0x904, 0x939);
        c.add(0xE01, 0xE30); c.add(0x1E00, 0x1FFF); c.add(0x3041, 0x3096); c.add(0x30A1, 0x30FA);
        c.add(0x3400, 0x4DBF); c.add(0x4E00, 0x9FFF); c.add(0xAC00, 0xD7A3); c.add(0x20000, 0x2A6DF);
    };

    if (key.empty() || key == "General_Category" || key == "gc") {
        if (name == "L" || name == "Letter" || name == "Alphabetic" || name == "Alpha") {
            letters_upper(); letters_lower(); letters_other(); return true;
        }
        if (name == "Lu" || name == "Uppercase_Letter" || name == "Uppercase") { letters_upper(); return true; }
        if (name == "Ll" || name == "Lowercase_Letter" || name == "Lowercase") { letters_lower(); return true; }
        if (name == "N" || name == "Number" || name == "Nd" || name == "Decimal_Number") {
            c.add('0', '9'); c.add(0x660, 0x669); c.add(0x6F0, 0x6F9); c.add(0x966, 0x96F); c.add(0xFF10, 0xFF19);
            return true;
        }
        if (name == "P" || name == "Punctuation") {
            c.add(0x21, 0x23); c.add(0x25, 0x2A); c.add(0x2C, 0x2F); c.add(0x3A, 0x3B); c.add(0x3F, 0x40);
            c.add(0x5B, 0x5D); c.add(0x5F, 0x5F); c.add(0x7B, 0x7B); c.add(0x7D, 0x7D); c.add(0xA1, 0xA1);
            c.add(0xA7, 0xA7); c.add(0xAB, 0xAB); c.add(0xB6, 0xB7); c.add(0xBB, 0xBB); c.add(0xBF, 0xBF);
            c.add(0x2010, 0x2027); c.add(0x2030, 0x205E); c.add(0x3001, 0x3003);
            return true;
        }
        if (name == "White_Space" || name == "space" || name == "Zs" || name == "Space_Separator") {
            add_space(c); return true;
        }
        if (name == "Any") { c.add(0, 0x10FFFF); return true; }
        if (name == "ASCII") { c.add(0, 0x7F); return true; }
    }
    if (key == "Script" || key == "sc" || key == "Script_Extensions" || key == "scx") {
        if (name == "Latin" || name == "Latn") {
            c.add('A', 'Z'); c.add('a', 'z'); c.add(0xAA, 0xAA); c.add(0xBA, 0xBA); c.add(0xC0, 0xD6);
            c.add(0xD8, 0xF6); c.add(0xF8, 0x2B8); c.add(0x1E00, 0x1EFF);
            return true;
        }
        if (name == "Greek" || name == "Grek") { c.add(0x370, 0x3FF); c.add(0x1F00, 0x1FFF); return true; }
        if (name == "Cyrillic" || name == "Cyrl") { c.add(0x400, 0x52F); return true; }
        if (name == "Han" || name == "Hani") { c.add(0x3400, 0x4DBF); c.add(0x4E00, 0x9FFF); c.add(0x20000, 0x2A6DF); return true; }
        if (name == "Hiragana" || name == "Hira") { c.add(0x3041, 0x309F); return true; }
        if (name == "Katakana" || name == "Kana") { c.add(0x30A0, 0x30FF); return true; }
        if (name == "Arabic" || name == "Arab") { c.add(0x600, 0x6FF); return true; }
        if (name == "Hebrew" || name == "Hebr") { c.add(0x591, 0x5F4); return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Pattern parser
// ---------------------------------------------------------------------------

class PatternParser {
public:
    PatternParser(const std::u16string& pattern, Program& program)
        : m_pattern(pattern), m_program(program) {}

    void parse() {
        m_total_groups = count_groups();
        m_program.root = parse_disjunction();
        if (m_pos < m_pattern.size()) {
            fail(m_pattern[m_pos] == ')' ? "Unmatched ')'" : "Unexpected character");
        }
        m_program.group_count = m_next_group;
        resolve_named(m_program.root.get());
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        js_throw_error(JsErrorKind::SyntaxError,
                       "Invalid regular expression: /" + m_program.source + "/" + m_program.flags + ": " + message);
    }

    bool at_end() const { return m_pos >= m_pattern.size(); }
    char16_t peek(size_t offset = 0) const {
        return m_pos + offset < m_pattern.size() ? m_pattern[m_pos + offset] : u'\0';
    }
    bool eat(char16_t c) {
        if (!at_end() && m_pattern[m_pos] == c) { ++m_pos; return true; }
        return false;
    }

    /// The next pattern character, joining a surrogate pair in unicode mode.
    uint32_t next_code_point() {
        uint32_t c = m_pattern[m_pos++];
        if (m_program.unicode && c >= 0xD800 && c <= 0xDBFF && !at_end() &&
            m_pattern[m_pos] >= 0xDC00 && m_pattern[m_pos] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (m_pattern[m_pos++] - 0xDC00);
        }
        return c;
    }

    // Capture groups are numbered by their opening parenthesis, and a
    // backreference may point forwards, so the count is needed up front.
    int count_groups() const {
        int count = 0;
        bool in_class = false;
        for (size_t i = 0; i < m_pattern.size(); ++i) {
            char16_t c = m_pattern[i];
            if (c == '\\') { ++i; continue; }
            if (in_class) { if (c == ']') in_class = false; continue; }
            if (c == '[') { in_class = true; continue; }
            if (c != '(') continue;
            if (i + 1 < m_pattern.size() && m_pattern[i + 1] == '?') {
                // (?<name> is a capture; (?<= and (?<! are lookbehinds.
                if (i + 2 < m_pattern.size() && m_pattern[i + 2] == '<' && i + 3 < m_pattern.size() &&
                    m_pattern[i + 3] != '=' && m_pattern[i + 3] != '!') {
                    ++count;
                }
                continue;
            }
            ++count;
        }
        return count;
    }

    std::unique_ptr<Node> make(Node::Type type) {
        auto node = std::make_unique<Node>();
        node->type = type;
        return node;
    }

    std::unique_ptr<Node> parse_disjunction() {
        if (stack_is_exhausted()) fail("Pattern is nested too deeply");
        auto first = parse_alternative();
        if (peek() != '|' || at_end()) return first;
        auto alt = make(Node::Type::Alternation);
        alt->children.push_back(std::move(first));
        while (eat('|')) alt->children.push_back(parse_alternative());
        return alt;
    }

    std::unique_ptr<Node> parse_alternative() {
        auto seq = make(Node::Type::Sequence);
        while (!at_end() && peek() != '|' && peek() != ')') {
            seq->children.push_back(parse_term());
        }
        if (seq->children.size() == 1) return std::move(seq->children[0]);
        if (seq->children.empty()) return make(Node::Type::Empty);
        return seq;
    }

    std::unique_ptr<Node> parse_term() {
        const int groups_before = m_next_group;
        std::unique_ptr<Node> atom;
        bool quantifiable = true;
        char16_t c = peek();

        if (c == '^') { ++m_pos; atom = make(Node::Type::LineStart); quantifiable = false; }
        else if (c == '$') { ++m_pos; atom = make(Node::Type::LineEnd); quantifiable = false; }
        else if (c == '(') { atom = parse_group(quantifiable); }
        else if (c == '.') { ++m_pos; atom = make(Node::Type::Any); }
        else if (c == '[') { ++m_pos; atom = parse_class(); }
        else if (c == '\\') { ++m_pos; atom = parse_escape(quantifiable); }
        else if (c == '*' || c == '+' || c == '?') { fail("Nothing to repeat"); }
        else if (c == '{' && m_program.unicode) { fail("Nothing to repeat"); }
        else if ((c == '}' || c == ']') && m_program.unicode) { fail("Lone quantifier brackets"); }
        else {
            atom = make(Node::Type::Char);
            atom->ch = next_code_point();
        }

        // Quantifier
        size_t min = 0, max = 0;
        bool has_quantifier = false;
        if (eat('*')) { min = 0; max = kUnbounded; has_quantifier = true; }
        else if (eat('+')) { min = 1; max = kUnbounded; has_quantifier = true; }
        else if (eat('?')) { min = 0; max = 1; has_quantifier = true; }
        else if (peek() == '{') {
            size_t save = m_pos;
            ++m_pos;
            if (parse_braces(min, max)) has_quantifier = true;
            else {
                // Not a quantifier: outside unicode mode `{` is a literal.
                m_pos = save;
                if (m_program.unicode) fail("Incomplete quantifier");
            }
        }
        if (!has_quantifier) return atom;
        if (!quantifiable) fail("Nothing to repeat");
        if (min > max) fail("numbers out of order in {} quantifier");

        auto repeat = make(Node::Type::Repeat);
        repeat->min = min;
        repeat->max = max;
        repeat->greedy = !eat('?');
        repeat->first_group = groups_before;
        repeat->last_group = m_next_group - 1;
        repeat->children.push_back(std::move(atom));
        return repeat;
    }

    bool parse_number(size_t& out) {
        if (at_end() || peek() < '0' || peek() > '9') return false;
        size_t value = 0;
        while (!at_end() && peek() >= '0' && peek() <= '9') {
            if (value > 100000000) value = 100000000;       // clamp absurd counts
            else value = value * 10 + static_cast<size_t>(m_pattern[m_pos] - '0');
            ++m_pos;
        }
        out = value;
        return true;
    }

    bool parse_braces(size_t& min, size_t& max) {
        if (!parse_number(min)) return false;
        if (eat('}')) { max = min; return true; }
        if (!eat(',')) return false;
        if (eat('}')) { max = kUnbounded; return true; }
        if (!parse_number(max)) return false;
        return eat('}');
    }

    std::unique_ptr<Node> parse_group(bool& quantifiable) {
        ++m_pos;        // '('
        if (eat('?')) {
            if (eat(':')) {
                auto inner = parse_disjunction();
                if (!eat(')')) fail("Unterminated group");
                auto group = make(Node::Type::Group);
                group->children.push_back(std::move(inner));
                return group;
            }
            if (eat('=') || peek() == '!') {
                bool negate = previous() != '=';
                if (negate) ++m_pos;
                auto look = make(Node::Type::LookAhead);
                look->negate = negate;
                look->children.push_back(parse_disjunction());
                if (!eat(')')) fail("Unterminated group");
                // Annex B allows quantifying a lookahead outside unicode mode.
                quantifiable = !m_program.unicode;
                return look;
            }
            if (eat('<')) {
                if (peek() == '=' || peek() == '!') {
                    bool negate = m_pattern[m_pos++] == '!';
                    auto look = make(Node::Type::LookBehind);
                    look->negate = negate;
                    look->children.push_back(parse_disjunction());
                    if (!eat(')')) fail("Unterminated group");
                    quantifiable = false;
                    return look;
                }
                std::string name = parse_group_name();
                for (const auto& existing : m_program.names) {
                    if (existing.first == name) fail("Duplicate capture group name");
                }
                auto group = make(Node::Type::Group);
                group->group = m_next_group++;
                m_program.names.emplace_back(name, static_cast<size_t>(group->group));
                group->children.push_back(parse_disjunction());
                if (!eat(')')) fail("Unterminated group");
                return group;
            }
            fail("Invalid group");
        }
        auto group = make(Node::Type::Group);
        group->group = m_next_group++;
        group->children.push_back(parse_disjunction());
        if (!eat(')')) fail("Unterminated group");
        return group;
    }

    char16_t previous() const { return m_pattern[m_pos - 1]; }

    std::string parse_group_name() {
        std::u16string name;
        while (!at_end() && peek() != '>') name += m_pattern[m_pos++];
        if (!eat('>') || name.empty()) fail("Invalid capture group name");
        return js_utf16_to_utf8(name);
    }

    bool parse_hex(int digits, uint32_t& out) {
        uint32_t value = 0;
        for (int i = 0; i < digits; ++i) {
            char16_t c = peek(static_cast<size_t>(i));
            int d = (c >= '0' && c <= '9') ? c - '0'
                  : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                  : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            value = value * 16 + static_cast<uint32_t>(d);
        }
        m_pos += static_cast<size_t>(digits);
        out = value;
        return true;
    }

    /// Parses the part of an escape that names a single character. Returns
    /// false when the escape is a class escape handled by the caller.
    bool parse_character_escape(uint32_t& out) {
        char16_t c = m_pattern[m_pos++];
        switch (c) {
            case 'n': out = '\n'; return true;
            case 'r': out = '\r'; return true;
            case 't': out = '\t'; return true;
            case 'f': out = '\f'; return true;
            case 'v': out = '\v'; return true;
            case '0':
                if (peek() < '0' || peek() > '9') { out = 0; return true; }
                break;
            case 'c': {
                char16_t letter = peek();
                if ((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z')) {
                    ++m_pos;
                    out = static_cast<uint32_t>(letter % 32);
                    return true;
                }
                out = '\\';
                --m_pos;        // `\c` followed by something else is a literal backslash
                return true;
            }
            case 'x': {
                uint32_t value = 0;
                if (parse_hex(2, value)) { out = value; return true; }
                if (m_program.unicode) fail("Invalid escape");
                out = 'x';
                return true;
            }
            case 'u': {
                uint32_t value = 0;
                if (peek() == '{' && m_program.unicode) {
                    ++m_pos;
                    uint32_t cp = 0;
                    int digits = 0;
                    while (!at_end() && peek() != '}') {
                        uint32_t d = 0;
                        if (!parse_hex(1, d)) fail("Invalid Unicode escape");
                        cp = cp * 16 + d;
                        if (++digits > 6 || cp > 0x10FFFF) fail("Invalid Unicode escape");
                    }
                    if (!eat('}') || digits == 0) fail("Invalid Unicode escape");
                    out = cp;
                    return true;
                }
                if (parse_hex(4, value)) {
                    // \uD83D\uDE00 is one code point in unicode mode.
                    if (m_program.unicode && value >= 0xD800 && value <= 0xDBFF && peek() == '\\' && peek(1) == 'u') {
                        size_t save = m_pos;
                        m_pos += 2;
                        uint32_t low = 0;
                        if (parse_hex(4, low) && low >= 0xDC00 && low <= 0xDFFF) {
                            value = 0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            m_pos = save;
                        }
                    }
                    out = value;
                    return true;
                }
                if (m_program.unicode) fail("Invalid Unicode escape");
                out = 'u';
                return true;
            }
            default:
                break;
        }
        if (c >= '0' && c <= '7' && !m_program.unicode) {
            // Legacy octal escape.
            uint32_t value = static_cast<uint32_t>(c - '0');
            for (int i = 0; i < 2 && peek() >= '0' && peek() <= '7' && value * 8 + static_cast<uint32_t>(peek() - '0') <= 0xFF; ++i) {
                value = value * 8 + static_cast<uint32_t>(m_pattern[m_pos++] - '0');
            }
            out = value;
            return true;
        }
        // Identity escape: the character itself.
        --m_pos;
        out = next_code_point();
        return true;
    }

    bool parse_class_escape(char16_t c, CharClass& out) {
        CharClass base;
        switch (c) {
            case 'd': add_digit(out); return true;
            case 'w': add_word(out); return true;
            case 's': add_space(out); return true;
            case 'D': add_digit(base); out.add_complement(base); return true;
            case 'W': add_word(base); out.add_complement(base); return true;
            case 'S': add_space(base); out.add_complement(base); return true;
            default: return false;
        }
    }

    bool parse_property_escape(bool negate, CharClass& out) {
        if (!eat('{')) fail("Invalid property name");
        std::u16string name;
        while (!at_end() && peek() != '}') name += m_pattern[m_pos++];
        if (!eat('}')) fail("Invalid property name");
        CharClass base;
        if (!add_unicode_property(base, js_utf16_to_utf8(name))) fail("Invalid property name");
        if (negate) out.add_complement(base);
        else out.ranges.insert(out.ranges.end(), base.ranges.begin(), base.ranges.end());
        return true;
    }

    std::unique_ptr<Node> parse_escape(bool& quantifiable) {
        if (at_end()) fail("\\ at end of pattern");
        char16_t c = peek();

        if (c == 'b' || c == 'B') {
            ++m_pos;
            quantifiable = false;
            return make(c == 'b' ? Node::Type::WordBoundary : Node::Type::NotWordBoundary);
        }
        if (c == 'k' && (!m_program.names.empty() || m_program.unicode || has_named_groups())) {
            ++m_pos;
            if (!eat('<')) fail("Invalid named reference");
            auto ref = make(Node::Type::BackReference);
            ref->name = parse_group_name();
            return ref;
        }
        if (c >= '1' && c <= '9') {
            size_t save = m_pos;
            size_t number = 0;
            parse_number(number);
            if (number <= static_cast<size_t>(m_total_groups)) {
                auto ref = make(Node::Type::BackReference);
                ref->group = static_cast<int>(number);
                return ref;
            }
            if (m_program.unicode) fail("Invalid escape");
            m_pos = save;       // not a group: fall through to a legacy octal or identity escape
        }
        auto klass = make(Node::Type::Class);
        if (parse_class_escape(c, klass->klass)) {
            ++m_pos;
            return klass;
        }
        if ((c == 'p' || c == 'P') && m_program.unicode) {
            ++m_pos;
            parse_property_escape(c == 'P', klass->klass);
            return klass;
        }
        auto literal = make(Node::Type::Char);
        parse_character_escape(literal->ch);
        return literal;
    }

    bool has_named_groups() const {
        for (size_t i = 0; i + 2 < m_pattern.size(); ++i) {
            if (m_pattern[i] == '(' && m_pattern[i + 1] == '?' && m_pattern[i + 2] == '<' &&
                i + 3 < m_pattern.size() && m_pattern[i + 3] != '=' && m_pattern[i + 3] != '!') {
                return true;
            }
        }
        return false;
    }

    /// One member of a character class: a single code point or a class escape.
    /// Returns true when it is a single code point (stored in `out`).
    bool parse_class_atom(uint32_t& out, CharClass& set) {
        if (peek() != '\\') {
            out = next_code_point();
            return true;
        }
        ++m_pos;
        if (at_end()) fail("\\ at end of pattern");
        char16_t c = peek();
        if (parse_class_escape(c, set)) { ++m_pos; return false; }
        if ((c == 'p' || c == 'P') && m_program.unicode) {
            ++m_pos;
            parse_property_escape(c == 'P', set);
            return false;
        }
        if (c == 'b') { ++m_pos; out = 0x08; return true; }       // backspace inside a class
        if (c == '-' ) { ++m_pos; out = '-'; return true; }
        parse_character_escape(out);
        return true;
    }

    std::unique_ptr<Node> parse_class() {
        auto node = make(Node::Type::Class);
        CharClass& set = node->klass;
        set.negated = eat('^');
        while (true) {
            if (at_end()) fail("Unterminated character class");
            if (eat(']')) break;
            uint32_t lo = 0;
            if (!parse_class_atom(lo, set)) continue;
            // A range needs a `-` that is followed by something other than `]`.
            if (peek() == '-' && peek(1) != ']' && m_pos + 1 < m_pattern.size()) {
                size_t save = m_pos;
                ++m_pos;
                uint32_t hi = 0;
                if (parse_class_atom(hi, set)) {
                    if (hi < lo) fail("Range out of order in character class");
                    set.add(lo, hi);
                    continue;
                }
                // `a-\d`: the dash is a literal.
                if (m_program.unicode) fail("Invalid character class");
                set.add(lo, lo);
                set.add('-', '-');
                (void)save;
                continue;
            }
            set.add(lo, lo);
        }
        return node;
    }

    void resolve_named(Node* node) {
        if (!node) return;
        if (node->type == Node::Type::BackReference && node->group < 0) {
            for (const auto& [name, index] : m_program.names) {
                if (name == node->name) node->group = static_cast<int>(index);
            }
            if (node->group < 0) fail("Invalid named capture referenced");
        }
        for (auto& child : node->children) resolve_named(child.get());
    }

    const std::u16string& m_pattern;
    Program& m_program;
    size_t m_pos{0};
    int m_next_group{1};
    int m_total_groups{0};
};

// ---------------------------------------------------------------------------
// Matcher
// ---------------------------------------------------------------------------

class Matcher {
public:
    Matcher(const Program& program, const std::u16string& subject)
        : m_program(program), m_subject(subject),
          m_captures(static_cast<size_t>(program.group_count) * 2, -1) {}

    /// Tries to match starting exactly at `start`. On success the captures
    /// hold [start, end) pairs in UTF-16 units, -1 for unmatched groups.
    bool match_at(size_t start) {
        std::fill(m_captures.begin(), m_captures.end(), -1);
        struct Final : Continuation {
            Matcher* m;
            size_t start;
            bool run(size_t end) override {
                m->m_captures[0] = static_cast<long>(start);
                m->m_captures[1] = static_cast<long>(end);
                return true;
            }
        } final_step;
        final_step.m = this;
        final_step.start = start;
        return match(m_program.root.get(), start, final_step);
    }

    const std::vector<long>& captures() const { return m_captures; }
    void reset_budget() { m_steps = 0; }

private:
    struct Continuation {
        virtual ~Continuation() = default;
        virtual bool run(size_t pos) = 0;
    };

    void charge() {
        if (++m_steps > kMaxSteps) js_throw_range_error("Regular expression too complex: step limit exceeded");
        if ((m_steps & 63) == 0 && stack_is_exhausted()) {
            js_throw_range_error("Regular expression too complex: recursion limit exceeded");
        }
    }

    /// The code point at `pos` and how many units it occupies.
    uint32_t code_point_at(size_t pos, size_t& width) const {
        uint32_t c = m_subject[pos];
        width = 1;
        if (m_program.unicode && c >= 0xD800 && c <= 0xDBFF && pos + 1 < m_subject.size()) {
            uint32_t low = m_subject[pos + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                width = 2;
                return 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
            }
        }
        return c;
    }

    bool same_char(uint32_t a, uint32_t b) const {
        if (a == b) return true;
        return m_program.ignore_case && fold_case(a) == fold_case(b);
    }

    bool class_matches(const CharClass& set, uint32_t c) const {
        bool hit = set.contains(c);
        if (!hit && m_program.ignore_case) hit = set.contains(fold_case(c)) || set.contains(unfold_case(c));
        return hit != set.negated;
    }

    /// Matches one character-consuming atom at `pos`.
    bool single(const Node* node, size_t pos, size_t& width) const {
        if (pos >= m_subject.size()) return false;
        uint32_t c = code_point_at(pos, width);
        switch (node->type) {
            case Node::Type::Char: return same_char(c, node->ch);
            case Node::Type::Any: return m_program.dot_all || !is_line_terminator(c);
            case Node::Type::Class: return class_matches(node->klass, c);
            default: return false;
        }
    }

    static bool is_single(const Node* node) {
        return node->type == Node::Type::Char || node->type == Node::Type::Any || node->type == Node::Type::Class;
    }

    bool match_sequence(const std::vector<std::unique_ptr<Node>>& items, size_t index, size_t pos, Continuation& k) {
        if (index == items.size()) return k.run(pos);
        struct Rest : Continuation {
            Matcher* m;
            const std::vector<std::unique_ptr<Node>>* items;
            size_t next;
            Continuation* k;
            bool run(size_t p) override { return m->match_sequence(*items, next, p, *k); }
        } rest;
        rest.m = this;
        rest.items = &items;
        rest.next = index + 1;
        rest.k = &k;
        return match(items[index].get(), pos, rest);
    }

    void clear_groups(int first, int last) {
        for (int g = first; g <= last; ++g) {
            m_captures[static_cast<size_t>(g) * 2] = -1;
            m_captures[static_cast<size_t>(g) * 2 + 1] = -1;
        }
    }

    bool match_repeat(const Node* node, size_t pos, size_t count, Continuation& k) {
        charge();
        const Node* atom = node->children[0].get();

        if (count >= node->max) return k.run(pos);

        struct Iteration : Continuation {
            Matcher* m;
            const Node* node;
            size_t count;
            size_t start;
            Continuation* k;
            bool run(size_t end) override {
                // An iteration that consumed nothing cannot make progress; stop
                // here instead of looping forever on patterns like (a*)*.
                if (end == start && count >= node->min) return false;
                return m->match_repeat(node, end, count + 1, *k);
            }
        } iteration;
        iteration.m = this;
        iteration.node = node;
        iteration.count = count;
        iteration.start = pos;
        iteration.k = &k;

        const bool has_groups = node->last_group >= node->first_group;
        std::vector<long> saved;
        auto save_groups = [&]() {
            if (!has_groups) return;
            saved.assign(m_captures.begin() + node->first_group * 2, m_captures.begin() + (node->last_group + 1) * 2);
        };
        auto restore_groups = [&]() {
            if (!has_groups) return;
            std::copy(saved.begin(), saved.end(), m_captures.begin() + node->first_group * 2);
        };

        if (count < node->min) {
            save_groups();
            clear_groups(node->first_group, node->last_group);
            if (match(atom, pos, iteration)) return true;
            restore_groups();
            return false;
        }
        if (node->greedy) {
            save_groups();
            clear_groups(node->first_group, node->last_group);
            if (match(atom, pos, iteration)) return true;
            restore_groups();
            return k.run(pos);
        }
        if (k.run(pos)) return true;
        save_groups();
        clear_groups(node->first_group, node->last_group);
        if (match(atom, pos, iteration)) return true;
        restore_groups();
        return false;
    }

    /// Repetition of a single-character atom, done with a loop so that `.*`
    /// over a long string does not recurse once per character.
    bool match_simple_repeat(const Node* node, size_t pos, Continuation& k) {
        const Node* atom = node->children[0].get();
        size_t width = 1;

        if (!node->greedy) {
            size_t p = pos;
            for (size_t count = 0;; ++count) {
                if (count >= node->min && k.run(p)) return true;
                if (count >= node->max || !single(atom, p, width)) return false;
                p += width;
                charge();
            }
        }

        if (!m_program.unicode) {
            // Every match is exactly one unit wide.
            size_t count = 0;
            while (count < node->max && single(atom, pos + count, width)) ++count;
            if (count < node->min) return false;
            for (size_t i = count + 1; i-- > node->min;) {
                charge();
                if (k.run(pos + i)) return true;
            }
            return false;
        }

        std::vector<size_t> ends;
        ends.push_back(pos);
        size_t p = pos;
        while (ends.size() - 1 < node->max && single(atom, p, width)) {
            p += width;
            ends.push_back(p);
        }
        if (ends.size() - 1 < node->min) return false;
        for (size_t i = ends.size(); i-- > node->min;) {
            charge();
            if (k.run(ends[i])) return true;
        }
        return false;
    }

    bool match(const Node* node, size_t pos, Continuation& k) {
        charge();
        switch (node->type) {
            case Node::Type::Empty:
                return k.run(pos);

            case Node::Type::Char:
            case Node::Type::Any:
            case Node::Type::Class: {
                size_t width = 1;
                return single(node, pos, width) && k.run(pos + width);
            }

            case Node::Type::LineStart:
                if (pos == 0 || (m_program.multiline && is_line_terminator(m_subject[pos - 1]))) return k.run(pos);
                return false;

            case Node::Type::LineEnd:
                if (pos == m_subject.size() || (m_program.multiline && is_line_terminator(m_subject[pos]))) return k.run(pos);
                return false;

            case Node::Type::WordBoundary:
            case Node::Type::NotWordBoundary: {
                bool before = pos > 0 && is_word_char(m_subject[pos - 1]);
                bool after = pos < m_subject.size() && is_word_char(m_subject[pos]);
                bool boundary = before != after;
                if (boundary == (node->type == Node::Type::WordBoundary)) return k.run(pos);
                return false;
            }

            case Node::Type::Sequence:
                return match_sequence(node->children, 0, pos, k);

            case Node::Type::Alternation:
                for (const auto& alternative : node->children) {
                    if (match(alternative.get(), pos, k)) return true;
                }
                return false;

            case Node::Type::Group: {
                if (node->group < 0) return match(node->children[0].get(), pos, k);
                struct Capture : Continuation {
                    Matcher* m;
                    size_t slot;
                    size_t start;
                    Continuation* k;
                    bool run(size_t end) override {
                        long old_start = m->m_captures[slot];
                        long old_end = m->m_captures[slot + 1];
                        m->m_captures[slot] = static_cast<long>(start);
                        m->m_captures[slot + 1] = static_cast<long>(end);
                        if (k->run(end)) return true;
                        m->m_captures[slot] = old_start;
                        m->m_captures[slot + 1] = old_end;
                        return false;
                    }
                } capture;
                capture.m = this;
                capture.slot = static_cast<size_t>(node->group) * 2;
                capture.start = pos;
                capture.k = &k;
                return match(node->children[0].get(), pos, capture);
            }

            case Node::Type::Repeat:
                if (is_single(node->children[0].get())) return match_simple_repeat(node, pos, k);
                return match_repeat(node, pos, 0, k);

            case Node::Type::BackReference: {
                size_t slot = static_cast<size_t>(node->group) * 2;
                if (slot + 1 >= m_captures.size() || m_captures[slot] < 0) return k.run(pos);     // unset group matches empty
                size_t start = static_cast<size_t>(m_captures[slot]);
                size_t length = static_cast<size_t>(m_captures[slot + 1]) - start;
                if (pos + length > m_subject.size()) return false;
                for (size_t i = 0; i < length; ++i) {
                    if (!same_char(m_subject[start + i], m_subject[pos + i])) return false;
                }
                return k.run(pos + length);
            }

            case Node::Type::LookAhead: {
                struct Accept : Continuation {
                    bool run(size_t) override { return true; }
                } accept;
                std::vector<long> saved = m_captures;
                bool found = match(node->children[0].get(), pos, accept);
                if (node->negate) {
                    m_captures = saved;
                    return !found && k.run(pos);
                }
                if (!found) {
                    m_captures = saved;
                    return false;
                }
                if (k.run(pos)) return true;
                m_captures = saved;
                return false;
            }

            case Node::Type::LookBehind: {
                struct EndsAt : Continuation {
                    size_t target;
                    bool run(size_t p) override { return p == target; }
                } ends_at;
                ends_at.target = pos;
                std::vector<long> saved = m_captures;
                bool found = false;
                for (size_t start = pos + 1; start-- > 0;) {
                    if (match(node->children[0].get(), start, ends_at)) { found = true; break; }
                    m_captures = saved;
                }
                if (node->negate) {
                    m_captures = saved;
                    return !found && k.run(pos);
                }
                if (!found) return false;
                if (k.run(pos)) return true;
                m_captures = saved;
                return false;
            }
        }
        return false;
    }

    const Program& m_program;
    const std::u16string& m_subject;
    std::vector<long> m_captures;
    uint64_t m_steps{0};
};

// ---------------------------------------------------------------------------
// RegExp objects
// ---------------------------------------------------------------------------

std::shared_ptr<Program> compile(const std::string& pattern, const std::string& flags) {
    auto program = std::make_shared<Program>();
    program->source = pattern;
    for (char flag : flags) {
        bool* target = nullptr;
        switch (flag) {
            case 'g': target = &program->global; break;
            case 'i': target = &program->ignore_case; break;
            case 'm': target = &program->multiline; break;
            case 's': target = &program->dot_all; break;
            case 'u':
            case 'v': target = &program->unicode; break;
            case 'y': target = &program->sticky; break;
            case 'd': target = &program->has_indices; break;
            default: break;
        }
        if (!target || *target) {
            js_throw_error(JsErrorKind::SyntaxError, "Invalid flags supplied to RegExp constructor '" + flags + "'");
        }
        *target = true;
    }
    // Canonical flag order, as the `flags` accessor reports it.
    for (char flag : std::string("dgimsuvy")) {
        if (flags.find(flag) != std::string::npos) program->flags += flag;
    }
    std::u16string wide = js_utf8_to_utf16(pattern);
    PatternParser parser(wide, *program);
    parser.parse();
    return program;
}

std::shared_ptr<Program> program_of(const JsValue& value, const char* method) {
    if (!value.object_val || value.object_val->klass != ObjectClass::RegExp || !value.object_val->internal) {
        js_throw_type_error(std::string("Method ") + method + " called on incompatible receiver " +
                            js_describe_thrown(value));
    }
    return std::static_pointer_cast<Program>(value.object_val->internal);
}

JsValue make_regexp(std::shared_ptr<Program> program) {
    JsObject* cell = Heap::default_heap().allocate_object();
    cell->prototype = JsIntrinsics::get().regexp_proto;
    cell->klass = ObjectClass::RegExp;
    cell->internal = std::move(program);
    JsValue regex = JsValue::make_object(cell);
    regex.define_property("lastIndex", JsValue::make_number(0), PropertyMap::Writable);
    return regex;
}

/// The subject converted to UTF-16, cached because exec() in a loop is called
/// with the same string every time.
const std::u16string& wide_subject(const std::string& subject) {
    static thread_local std::string cached_source;
    static thread_local std::u16string cached_wide;
    static thread_local bool valid = false;
    if (!valid || cached_source != subject) {
        cached_source = subject;
        cached_wide = js_utf8_to_utf16(subject);
        valid = true;
    }
    return cached_wide;
}

struct MatchResult {
    bool matched{false};
    std::vector<long> captures;
};

/// Finds the first match at or after `from` (exactly at `from` when sticky).
MatchResult run_match(const Program& program, const std::u16string& subject, size_t from, bool sticky) {
    MatchResult result;
    if (from > subject.size()) return result;
    Matcher matcher(program, subject);

    // Skip straight to positions that can start a match when the pattern
    // begins with a literal character.
    const Node* first = program.root.get();
    while (first && first->type == Node::Type::Sequence && !first->children.empty()) first = first->children[0].get();
    const bool literal_start = first && first->type == Node::Type::Char && !program.ignore_case && first->ch < 0x10000;

    for (size_t start = from; start <= subject.size(); ++start) {
        if (literal_start && !sticky) {
            size_t found = subject.find(static_cast<char16_t>(first->ch), start);
            if (found == std::u16string::npos) return result;
            start = found;
        }
        if (matcher.match_at(start)) {
            result.matched = true;
            result.captures = matcher.captures();
            return result;
        }
        if (sticky) break;
    }
    return result;
}

std::u16string capture_text(const std::u16string& subject, const std::vector<long>& captures, size_t group) {
    long start = captures[group * 2];
    long end = captures[group * 2 + 1];
    if (start < 0) return std::u16string();
    return subject.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
}

JsValue build_match_array(const Program& program, const std::string& input, const std::u16string& subject,
                          const std::vector<long>& captures) {
    std::vector<JsValue> items;
    for (int g = 0; g < program.group_count; ++g) {
        if (captures[static_cast<size_t>(g) * 2] < 0) items.push_back(JsValue::make_undefined());
        else items.push_back(JsValue::make_string(js_utf16_to_utf8(capture_text(subject, captures, static_cast<size_t>(g)))));
    }
    JsValue array = JsValue::make_array(items);
    array.set_property("index", JsValue::make_number(static_cast<double>(captures[0])));
    array.set_property("input", JsValue::make_string(input));
    if (program.names.empty()) {
        array.set_property("groups", JsValue::make_undefined());
    } else {
        JsValue groups = JsValue::make_object();
        groups.object_val->prototype = nullptr;
        for (const auto& [name, index] : program.names) groups.set_property(name, items[index]);
        array.set_property("groups", groups);
    }
    return array;
}

size_t last_index_of(const JsValue& regex) {
    double value = js_to_integer(js_to_number(regex.get_property("lastIndex")));
    return value <= 0 ? 0 : static_cast<size_t>(std::min(value, 4294967295.0));
}

void set_last_index(JsValue regex, size_t value) {
    regex.set_property("lastIndex", JsValue::make_number(static_cast<double>(value)));
}

/// RegExp.prototype.exec: honours and updates lastIndex for global/sticky.
bool exec_raw(const JsValue& regex, const Program& program, const std::u16string& subject, MatchResult& out) {
    const bool uses_last_index = program.global || program.sticky;
    size_t from = uses_last_index ? last_index_of(regex) : 0;
    if (from > subject.size()) {
        set_last_index(regex, 0);
        return false;
    }
    out = run_match(program, subject, from, program.sticky);
    if (!out.matched) {
        if (uses_last_index) set_last_index(regex, 0);
        return false;
    }
    if (uses_last_index) set_last_index(regex, static_cast<size_t>(out.captures[1]));
    return true;
}

/// Where to resume after an empty match: one unit on, or one code point in
/// unicode mode.
size_t advance_index(const Program& program, const std::u16string& subject, size_t index) {
    if (program.unicode && index + 1 < subject.size() && subject[index] >= 0xD800 && subject[index] <= 0xDBFF &&
        subject[index + 1] >= 0xDC00 && subject[index + 1] <= 0xDFFF) {
        return index + 2;
    }
    return index + 1;
}

std::string escape_source(const std::string& source) {
    if (source.empty()) return "(?:)";
    std::string out;
    bool in_class = false;
    for (size_t i = 0; i < source.size(); ++i) {
        char c = source[i];
        if (c == '\\') {
            out += c;
            if (i + 1 < source.size()) out += source[++i];
            continue;
        }
        if (c == '[') in_class = true;
        else if (c == ']') in_class = false;
        if (c == '/' && !in_class) out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        out += c;
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Entry points used by String.prototype
// ---------------------------------------------------------------------------

bool js_is_regexp(const JsValue& value) {
    return value.object_val && value.type == JsType::Object && value.object_val->klass == ObjectClass::RegExp &&
           value.object_val->internal;
}

JsValue js_regexp_create(const std::string& pattern, const std::string& flags) {
    return make_regexp(compile(pattern, flags));
}

JsValue js_regexp_match(const JsValue& regex, const std::string& subject_utf8) {
    auto program = program_of(regex, "String.prototype.match");
    const std::u16string subject = wide_subject(subject_utf8);
    if (!program->global) {
        MatchResult result;
        if (!exec_raw(regex, *program, subject, result)) return JsValue::make_null();
        return build_match_array(*program, subject_utf8, subject, result.captures);
    }
    set_last_index(regex, 0);
    std::vector<JsValue> matches;
    while (true) {
        MatchResult result;
        if (!exec_raw(regex, *program, subject, result)) break;
        matches.push_back(JsValue::make_string(js_utf16_to_utf8(capture_text(subject, result.captures, 0))));
        if (result.captures[0] == result.captures[1]) {
            set_last_index(regex, advance_index(*program, subject, static_cast<size_t>(result.captures[1])));
        }
    }
    if (matches.empty()) return JsValue::make_null();
    return JsValue::make_array(matches);
}

JsValue js_regexp_match_all(const JsValue& regex, const std::string& subject_utf8) {
    auto program = program_of(regex, "String.prototype.matchAll");
    // Iterate with a private copy so the caller's lastIndex is left alone.
    JsValue clone = make_regexp(program);
    set_last_index(clone, last_index_of(regex));
    auto subject = std::make_shared<std::u16string>(js_utf8_to_utf16(subject_utf8));
    auto input = std::make_shared<std::string>(subject_utf8);
    auto done = std::make_shared<bool>(false);
    return js_make_iterator([clone, program, subject, input, done](JsValue& value) {
        if (*done) return false;
        MatchResult result;
        if (!exec_raw(clone, *program, *subject, result)) {
            *done = true;
            return false;
        }
        if (result.captures[0] == result.captures[1]) {
            set_last_index(clone, advance_index(*program, *subject, static_cast<size_t>(result.captures[1])));
        }
        if (!program->global) *done = true;
        value = build_match_array(*program, *input, *subject, result.captures);
        return true;
    }, {clone});
}

double js_regexp_search(const JsValue& regex, const std::string& subject_utf8) {
    auto program = program_of(regex, "String.prototype.search");
    const std::u16string subject = wide_subject(subject_utf8);
    MatchResult result = run_match(*program, subject, 0, program->sticky);
    return result.matched ? static_cast<double>(result.captures[0]) : -1.0;
}

JsValue js_regexp_replace(const JsValue& regex, const std::string& subject_utf8, const JsValue& replacement) {
    auto program = program_of(regex, "String.prototype.replace");
    const std::u16string subject = wide_subject(subject_utf8);
    const bool functional = replacement.is_function();
    const std::u16string replacement_template = functional ? std::u16string() : js_utf8_to_utf16(js_to_string(replacement));

    if (program->global) set_last_index(regex, 0);
    std::vector<MatchResult> matches;
    while (true) {
        MatchResult result;
        if (!exec_raw(regex, *program, subject, result)) break;
        matches.push_back(result);
        if (!program->global) break;
        if (result.captures[0] == result.captures[1]) {
            set_last_index(regex, advance_index(*program, subject, static_cast<size_t>(result.captures[1])));
        }
    }

    std::u16string out;
    size_t cursor = 0;
    for (const auto& match : matches) {
        size_t start = static_cast<size_t>(match.captures[0]);
        size_t end = static_cast<size_t>(match.captures[1]);
        if (start < cursor) continue;
        out.append(subject, cursor, start - cursor);

        std::vector<JsReplaceCapture> captures(static_cast<size_t>(program->group_count));
        for (int g = 0; g < program->group_count; ++g) {
            captures[static_cast<size_t>(g)].matched = match.captures[static_cast<size_t>(g) * 2] >= 0;
            captures[static_cast<size_t>(g)].text = capture_text(subject, match.captures, static_cast<size_t>(g));
        }

        if (functional) {
            std::vector<JsValue> args;
            for (const auto& capture : captures) {
                args.push_back(capture.matched ? JsValue::make_string(js_utf16_to_utf8(capture.text)) : JsValue::make_undefined());
            }
            args.push_back(JsValue::make_number(static_cast<double>(start)));
            args.push_back(JsValue::make_string(subject_utf8));
            if (!program->names.empty()) {
                JsValue groups = JsValue::make_object();
                for (const auto& [name, index] : program->names) groups.set_property(name, args[index]);
                args.push_back(groups);
            }
            out += js_utf8_to_utf16(js_to_string(replacement.call(args)));
        } else {
            out += js_expand_replacement(replacement_template, subject, start, captures, program->names);
        }
        cursor = end;
    }
    out.append(subject, cursor, std::u16string::npos);
    return JsValue::make_string(js_utf16_to_utf8(out));
}

JsValue js_regexp_split(const JsValue& regex, const std::string& subject_utf8, double limit) {
    auto program = program_of(regex, "String.prototype.split");
    const std::u16string subject = wide_subject(subject_utf8);
    std::vector<JsValue> parts;
    if (limit == 0) return JsValue::make_array(parts);

    auto push = [&](const std::u16string& text) {
        parts.push_back(JsValue::make_string(js_utf16_to_utf8(text)));
        return parts.size() >= limit;
    };

    if (subject.empty()) {
        // Splitting "" yields [""] unless the pattern matches the empty string.
        if (!run_match(*program, subject, 0, true).matched) push(subject);
        return JsValue::make_array(parts);
    }

    size_t piece_start = 0;
    size_t search = 0;
    while (search < subject.size()) {
        MatchResult result = run_match(*program, subject, search, true);
        if (!result.matched) {
            search = advance_index(*program, subject, search);
            continue;
        }
        size_t match_end = static_cast<size_t>(result.captures[1]);
        // A match that ends where the current piece starts would produce an
        // empty piece forever; skip it.
        if (match_end == piece_start || match_end > subject.size()) {
            search = advance_index(*program, subject, search);
            continue;
        }
        if (push(subject.substr(piece_start, search - piece_start))) return JsValue::make_array(parts);
        // Captured groups are spliced into the result.
        for (int g = 1; g < program->group_count; ++g) {
            if (result.captures[static_cast<size_t>(g) * 2] < 0) {
                parts.push_back(JsValue::make_undefined());
            } else {
                parts.push_back(JsValue::make_string(js_utf16_to_utf8(capture_text(subject, result.captures, static_cast<size_t>(g)))));
            }
            if (parts.size() >= limit) return JsValue::make_array(parts);
        }
        piece_start = match_end;
        search = match_end;
    }
    push(subject.substr(piece_start));
    return JsValue::make_array(parts);
}

// ---------------------------------------------------------------------------
// The RegExp object
// ---------------------------------------------------------------------------

void install_regexp_builtins(JsIntrinsics& realm) {
    JsObject* rp = realm.regexp_proto;

    JsValue regexp_ctor = js_make_native("RegExp", 2, [](Args args) {
        const JsValue& pattern = js_arg(args, 0);
        const JsValue& flags = js_arg(args, 1);
        if (js_is_regexp(pattern)) {
            auto program = program_of(pattern, "RegExp");
            if (flags.is_undefined()) return make_regexp(compile(program->source, program->flags));
            return make_regexp(compile(program->source, js_to_string(flags)));
        }
        return make_regexp(compile(pattern.is_undefined() ? std::string() : js_to_string(pattern),
                                   flags.is_undefined() ? std::string() : js_to_string(flags)));
    });
    regexp_ctor.define_property("prototype", js_object_value(rp), 0);
    js_define_hidden(rp, "constructor", regexp_ctor);
    js_define_method(regexp_ctor, "escape", 1, [](Args args) {
        std::string out;
        for (char c : js_to_string(js_arg(args, 0))) {
            if (std::string("\\^$.*+?()[]{}|/-").find(c) != std::string::npos) out += '\\';
            out += c;
        }
        return JsValue::make_string(out);
    });
    realm.add_global("RegExp", regexp_ctor);

    js_define_method(rp, "exec", 1, [](Args args) {
        JsValue regex = self();
        auto program = program_of(regex, "RegExp.prototype.exec");
        std::string input = js_to_string(js_arg(args, 0));
        const std::u16string subject = wide_subject(input);
        MatchResult result;
        if (!exec_raw(regex, *program, subject, result)) return JsValue::make_null();
        return build_match_array(*program, input, subject, result.captures);
    });
    js_define_method(rp, "test", 1, [](Args args) {
        JsValue regex = self();
        auto program = program_of(regex, "RegExp.prototype.test");
        const std::u16string subject = wide_subject(js_to_string(js_arg(args, 0)));
        MatchResult result;
        return JsValue::make_boolean(exec_raw(regex, *program, subject, result));
    });
    js_define_method(rp, "toString", 0, [](Args) {
        auto program = program_of(self(), "RegExp.prototype.toString");
        return JsValue::make_string("/" + escape_source(program->source) + "/" + program->flags);
    });
    js_define_method(rp, "compile", 2, [](Args) { return self(); });

    js_define_getter(rp, "source", [](Args) {
        JsValue me = self();
        if (!js_is_regexp(me)) return JsValue::make_string("(?:)");
        return JsValue::make_string(escape_source(program_of(me, "source")->source));
    });
    js_define_getter(rp, "flags", [](Args) {
        JsValue me = self();
        if (!js_is_regexp(me)) return JsValue::make_string("");
        return JsValue::make_string(program_of(me, "flags")->flags);
    });
    struct FlagGetter { const char* name; bool Program::*member; };
    static const FlagGetter flag_getters[] = {
        {"global", &Program::global},       {"ignoreCase", &Program::ignore_case},
        {"multiline", &Program::multiline}, {"dotAll", &Program::dot_all},
        {"unicode", &Program::unicode},     {"sticky", &Program::sticky},
        {"hasIndices", &Program::has_indices},
    };
    for (const auto& getter : flag_getters) {
        bool Program::*member = getter.member;
        js_define_getter(rp, getter.name, [member](Args) {
            JsValue me = self();
            if (!js_is_regexp(me)) return JsValue::make_undefined();
            return JsValue::make_boolean((*program_of(me, "flag")).*member);
        });
    }
}

} // namespace litebird
