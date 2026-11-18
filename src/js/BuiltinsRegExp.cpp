// RegExp: a backtracking regular-expression engine and the RegExp object.
//
// Supports ECMA-262 RegExp flags `d` (hasIndices), `g` (global), `i`
// (ignoreCase), `m` (multiline), `s` (dotAll), `u` (unicode), `v`
// (unicodeSets), and `y` (sticky), full Unicode 17.0 property escapes
// (General_Category, Binary Properties, Script, Script_Extensions, and
// Properties of Strings), `v`-mode character-class set operations (`--`, `&&`,
// nested `[...]`, `\q{...}`), ES2025 inline modifiers `(?ims-ims:...)`,
// ES2025 duplicate named capturing groups, ES2025 `RegExp.escape()`, and
// `RegExp.prototype[Symbol.{match,matchAll,replace,search,split}]`.
//
// The matcher works on UTF-16 code units, so indices agree with the string
// methods. Two limits keep hostile patterns from taking the process down:
// recursion stops before the native stack is exhausted, and a step budget ends
// catastrophic backtracking. Both surface to script as a RangeError.

#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/JsIdentifierTables.hpp"
#include "litebird/js/RegExpUnicodeTables.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/common/StackLimit.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace litebird {

namespace {

using Args = const std::vector<JsValue>&;

JsValue self() { return JsValue::current_this(); }

const size_t kUnbounded = std::numeric_limits<size_t>::max();
const uint64_t kMaxSteps = 30'000'000;

constexpr uint8_t kFlagIgnoreCase = 1 << 0;
constexpr uint8_t kFlagMultiline  = 1 << 1;
constexpr uint8_t kFlagDotAll     = 1 << 2;

struct RegExpSyntaxError {
    std::string message;
};

struct RegExpRangeError {
    std::string message;
};

// ---------------------------------------------------------------------------
// Character classes and v-mode ClassSets
// ---------------------------------------------------------------------------

struct CharClass {
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    bool negated{false};

    void add(uint32_t lo, uint32_t hi) {
        if (lo <= hi) ranges.emplace_back(lo, hi);
    }

    void add_ranges(regexp_unicode::PropertyRangesView v) {
        ranges.reserve(ranges.size() + v.count);
        for (size_t i = 0; i < v.count; ++i) {
            ranges.emplace_back(v.data[i].lo, v.data[i].hi);
        }
    }

    void add_class(const CharClass& other) {
        ranges.insert(ranges.end(), other.ranges.begin(), other.ranges.end());
    }

    void normalize() {
        if (ranges.size() <= 1) return;
        std::sort(ranges.begin(), ranges.end());
        size_t out = 0;
        for (size_t i = 1; i < ranges.size(); ++i) {
            if (ranges[i].first <= ranges[out].second + 1) {
                if (ranges[i].second > ranges[out].second) {
                    ranges[out].second = ranges[i].second;
                }
            } else {
                ranges[++out] = ranges[i];
            }
        }
        ranges.resize(out + 1);
    }

    bool contains(uint32_t c) const {
        if (ranges.empty()) return false;
        size_t lo = 0, hi = ranges.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (ranges[mid].second < c) lo = mid + 1;
            else hi = mid;
        }
        return lo < ranges.size() && c >= ranges[lo].first && c <= ranges[lo].second;
    }

    /// Replaces `ranges` with `[0, max_cp] \ ranges` (sets `negated = false`).
    void complement_in_place(uint32_t max_cp = 0x10FFFF) {
        normalize();
        std::vector<std::pair<uint32_t, uint32_t>> comp;
        uint32_t next = 0;
        for (const auto& [lo, hi] : ranges) {
            if (lo > max_cp) break;
            if (lo > next) comp.emplace_back(next, lo - 1);
            if (hi >= max_cp) {
                next = max_cp + 1;
                break;
            }
            next = hi + 1;
        }
        if (next <= max_cp) comp.emplace_back(next, max_cp);
        ranges = std::move(comp);
        negated = false;
    }

    /// Adds everything in `[0, max_cp]` not covered by `other`'s ranges.
    void add_complement(const CharClass& other, uint32_t max_cp = 0x10FFFF) {
        CharClass tmp = other;
        tmp.complement_in_place(max_cp);
        add_class(tmp);
    }

    /// Replaces `ranges` with `ranges ∩ other.ranges`.
    void intersect_with(const CharClass& other) {
        normalize();
        CharClass rhs = other;
        rhs.normalize();
        std::vector<std::pair<uint32_t, uint32_t>> out;
        size_t i = 0, j = 0;
        while (i < ranges.size() && j < rhs.ranges.size()) {
            uint32_t lo = std::max(ranges[i].first, rhs.ranges[j].first);
            uint32_t hi = std::min(ranges[i].second, rhs.ranges[j].second);
            if (lo <= hi) out.emplace_back(lo, hi);
            if (ranges[i].second < rhs.ranges[j].second) ++i;
            else ++j;
        }
        ranges = std::move(out);
    }

    /// Replaces `ranges` with `ranges \ other.ranges`.
    void subtract(const CharClass& other) {
        normalize();
        CharClass rhs = other;
        rhs.normalize();
        std::vector<std::pair<uint32_t, uint32_t>> out;
        size_t j = 0;
        for (const auto& [alo, ahi] : ranges) {
            uint32_t cur = alo;
            while (j < rhs.ranges.size() && rhs.ranges[j].second < cur) ++j;
            size_t k = j;
            while (k < rhs.ranges.size() && rhs.ranges[k].first <= ahi) {
                if (rhs.ranges[k].first > cur) {
                    out.emplace_back(cur, rhs.ranges[k].first - 1);
                }
                if (rhs.ranges[k].second >= ahi) {
                    cur = ahi + 1;
                    break;
                }
                cur = rhs.ranges[k].second + 1;
                ++k;
            }
            if (cur <= ahi) out.emplace_back(cur, ahi);
        }
        ranges = std::move(out);
    }

    /// Closes `ranges` under Unicode 17.0 Simple Case Folding (`unicode_mode == true`)
    /// or legacy non-Unicode Canonicalize (`unicode_mode == false`).
    void close_case(bool unicode_mode) {
        normalize();
        if (ranges.empty()) return;
        std::vector<std::pair<uint32_t, uint32_t>> extra;
        if (unicode_mode) {
            auto cb = [](const uint32_t* members, size_t count, void* ctx) {
                auto* dst = static_cast<std::vector<std::pair<uint32_t, uint32_t>>*>(ctx);
                for (size_t i = 0; i < count; ++i) dst->emplace_back(members[i], members[i]);
            };
            for (const auto& [lo, hi] : ranges) {
                regexp_unicode::for_each_unicode_case_equiv_in_range(lo, hi, cb, &extra);
            }
        } else {
            auto cb = [](const uint16_t* members, size_t count, void* ctx) {
                auto* dst = static_cast<std::vector<std::pair<uint32_t, uint32_t>>*>(ctx);
                for (size_t i = 0; i < count; ++i) dst->emplace_back(members[i], members[i]);
            };
            for (const auto& [lo, hi] : ranges) {
                regexp_unicode::for_each_legacy_case_equiv_in_range(lo, hi, cb, &extra);
            }
        }
        if (!extra.empty()) {
            ranges.insert(ranges.end(), extra.begin(), extra.end());
            normalize();
        }
    }
};

struct ClassSet {
    CharClass chars;                                // single code points (normalized, negated = false)
    std::vector<std::vector<uint32_t>> strings;     // multi-CP strings (len >= 2), sorted & unique
    bool matches_empty{false};
    bool may_contain_strings{false};                // ECMA-262 §22.2.1.1 static MayContainStrings

    void normalize() {
        chars.normalize();
        if (strings.size() > 1) {
            std::sort(strings.begin(), strings.end());
            strings.erase(std::unique(strings.begin(), strings.end()), strings.end());
        }
    }

    void add_string(std::vector<uint32_t> s) {
        if (s.empty()) {
            matches_empty = true;
            may_contain_strings = true;
        } else if (s.size() == 1) {
            chars.add(s[0], s[0]);
        } else {
            strings.push_back(std::move(s));
            may_contain_strings = true;
        }
    }

    void canonicalize_for_v_ignore_case() {
        chars.close_case(true);
        for (auto& s : strings) {
            for (uint32_t& cp : s) cp = regexp_unicode::unicode_case_canonical(cp);
        }
        normalize();
    }

    void union_with(const ClassSet& other) {
        chars.add_class(other.chars);
        chars.normalize();
        if (!other.strings.empty()) {
            strings.insert(strings.end(), other.strings.begin(), other.strings.end());
            if (strings.size() > 1) {
                std::sort(strings.begin(), strings.end());
                strings.erase(std::unique(strings.begin(), strings.end()), strings.end());
            }
        }
        matches_empty = matches_empty || other.matches_empty;
        may_contain_strings = may_contain_strings || other.may_contain_strings;
    }

    void intersect_with(const ClassSet& other) {
        chars.intersect_with(other.chars);
        if (!strings.empty() && !other.strings.empty()) {
            std::vector<std::vector<uint32_t>> out;
            std::set_intersection(strings.begin(), strings.end(),
                                  other.strings.begin(), other.strings.end(),
                                  std::back_inserter(out));
            strings = std::move(out);
        } else {
            strings.clear();
        }
        matches_empty = matches_empty && other.matches_empty;
        may_contain_strings = may_contain_strings && other.may_contain_strings;
    }

    void subtract(const ClassSet& other) {
        chars.subtract(other.chars);
        if (!strings.empty() && !other.strings.empty()) {
            std::vector<std::vector<uint32_t>> out;
            std::set_difference(strings.begin(), strings.end(),
                                other.strings.begin(), other.strings.end(),
                                std::back_inserter(out));
            strings = std::move(out);
        }
        matches_empty = matches_empty && !other.matches_empty;
        // Per ECMA-262 §22.2.1.1: MayContainStrings(A -- B) == MayContainStrings(A).
    }
};

struct StringSet {
    CharClass chars;                                // single code points (normalized, negated = false)
    std::vector<std::vector<uint32_t>> strings;     // multi-CP strings sorted by descending length
    std::unordered_map<uint32_t, std::vector<size_t>> by_first;
    std::unordered_map<uint32_t, std::vector<size_t>> by_last;
    bool matches_empty{false};

    static StringSet from_class_set(const ClassSet& cs, bool ignore_case) {
        StringSet ss;
        ss.chars = cs.chars;
        ss.chars.normalize();
        ss.matches_empty = cs.matches_empty;
        ss.strings = cs.strings;
        std::sort(ss.strings.begin(), ss.strings.end(),
                  [](const auto& a, const auto& b) {
                      if (a.size() != b.size()) return a.size() > b.size();
                      return a < b;
                  });
        for (size_t i = 0; i < ss.strings.size(); ++i) {
            const auto& s = ss.strings[i];
            if (s.empty()) continue;
            uint32_t first = ignore_case ? regexp_unicode::unicode_case_canonical(s.front()) : s.front();
            uint32_t last = ignore_case ? regexp_unicode::unicode_case_canonical(s.back()) : s.back();
            ss.by_first[first].push_back(i);
            ss.by_last[last].push_back(i);
        }
        return ss;
    }
};

// ---------------------------------------------------------------------------
// AST & Bytecode representation
// ---------------------------------------------------------------------------

struct Node {
    enum class Type {
        Empty, Char, Any, Class, StringSet, LineStart, LineEnd, WordBoundary, NotWordBoundary,
        Group, Alternation, Sequence, Repeat, BackReference, LookAhead, LookBehind
    };
    Type type{Type::Empty};
    uint8_t mode_flags{0};
    uint32_t ch{0};
    CharClass klass;
    ClassSet class_set;
    std::vector<std::unique_ptr<Node>> children;
    int group{-1};                  // Group: capture index or -1; BackReference: target group
    std::string name;               // BackReference by name, resolved after parsing
    std::vector<int> named_targets; // Resolved candidate group indices for named BackReference
    size_t min{0};
    size_t max{0};
    bool greedy{true};
    bool negate{false};
    int first_group{0};             // Repeat: capture groups inside the repeated atom,
    int last_group{-1};             //         cleared at the start of each iteration
};

enum class RegExpOp : uint8_t {
    Accept,
    MatchChar,
    MatchAny,
    MatchClass,
    MatchStringSet,
    LineStart,
    LineEnd,
    WordBoundary,
    NotWordBoundary,
    BackRef,
    BeginGroup,
    EndGroup,
    Jump,
    Split,
    RepeatEnter,
    RepeatStep,
    RepeatBody,
    RepeatNext,
    SimpleRepeat,
    LookAround
};

struct RegExpInsn {
    RegExpOp op{RegExpOp::Accept};
    uint8_t atom_kind{0}; // 0=Char, 1=Any, 2=Class (for SimpleRepeat)
    uint8_t mode_flags{0};
    bool flag1{false};    // backward (Match*/BackRef/SimpleRepeat/LookAround) or greedy (RepeatStep)
    bool flag2{false};    // greedy (SimpleRepeat) or negate (LookAround) or named (BackRef)
    size_t a{0};          // ch / class_idx / string_set_idx / group / backref_idx / slot / target
    size_t b{0};          // target2 / first_group / skip_pc / step_pc
    int32_t last_group{-1};
    size_t min{0};
    size_t max{0};
};

struct Program {
    std::unique_ptr<Node> root;
    std::vector<RegExpInsn> code;
    std::vector<CharClass> classes;
    std::vector<StringSet> string_sets;
    std::vector<std::vector<int>> named_backrefs;
    size_t entry_pc{0};
    size_t repeat_count{0};
    bool has_literal_start{false};
    uint32_t literal_start_ch{0};
    int group_count{1};             // including group 0, the whole match
    std::vector<std::pair<std::string, size_t>> names; // (name, group_idx) in source order (may repeat name across |)
    std::vector<std::string> unique_names;             // unique group names in first-appearance order
    std::string source;
    std::string flags;
    bool global{false}, ignore_case{false}, multiline{false}, dot_all{false};
    bool unicode{false}, unicode_sets{false}, sticky{false}, has_indices{false};

    bool unicode_mode() const { return unicode || unicode_sets; }

    ~Program() {
        if (!root) return;
        std::vector<std::unique_ptr<Node>> stack;
        stack.push_back(std::move(root));
        while (!stack.empty()) {
            auto node = std::move(stack.back());
            stack.pop_back();
            for (auto& child : node->children) {
                if (child) stack.push_back(std::move(child));
            }
            node->children.clear();
        }
    }
};

// ---------------------------------------------------------------------------
// Character classification & case helpers
// ---------------------------------------------------------------------------

bool is_line_terminator(uint32_t c) {
    return c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029;
}

bool is_ascii_word_char(uint32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool is_word_char(uint32_t c, bool unicode_mode, bool ignore_case) {
    if (is_ascii_word_char(c)) return true;
    if (unicode_mode && ignore_case && (c == 0x017F || c == 0x212A)) return true;
    return false;
}

bool is_syntax_char(uint32_t c) {
    switch (c) {
        case '^': case '$': case '\\': case '.': case '*': case '+': case '?':
        case '(': case ')': case '[': case ']': case '{': case '}': case '|':
            return true;
        default:
            return false;
    }
}

bool is_v_class_syntax_char(uint32_t c) {
    switch (c) {
        case '(': case ')': case '[': case ']': case '{': case '}':
        case '/': case '-': case '\\': case '|':
            return true;
        default:
            return false;
    }
}

bool is_v_class_reserved_punctuator(uint32_t c) {
    switch (c) {
        case '&': case '-': case '!': case '#': case '%': case ',': case ':':
        case ';': case '<': case '=': case '>': case '@': case '`': case '~':
            return true;
        default:
            return false;
    }
}

bool is_v_double_punctuator_char(uint32_t c) {
    switch (c) {
        case '&': case '!': case '#': case '$': case '%': case '*': case '+':
        case ',': case '.': case ':': case ';': case '<': case '=': case '>':
        case '?': case '@': case '^': case '`': case '~':
            return true;
        default:
            return false;
    }
}

void add_digit(CharClass& c) { c.add('0', '9'); }
void add_word(CharClass& c, bool unicode_mode, bool ignore_case) {
    c.add('a', 'z'); c.add('A', 'Z'); c.add('0', '9'); c.add('_', '_');
    if (unicode_mode && ignore_case) {
        c.add(0x017F, 0x017F);
        c.add(0x212A, 0x212A);
    }
}
void add_space(CharClass& c) {
    c.add(0x09, 0x0D); c.add(0x20, 0x20); c.add(0xA0, 0xA0); c.add(0x1680, 0x1680);
    c.add(0x2000, 0x200A); c.add(0x2028, 0x2029); c.add(0x202F, 0x202F); c.add(0x205F, 0x205F);
    c.add(0x3000, 0x3000); c.add(0xFEFF, 0xFEFF);
}

bool is_valid_unicode_prop_name_chars(const std::u16string& s, size_t& eq_pos) {
    eq_pos = std::u16string::npos;
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        char16_t c = s[i];
        if (c == '=') {
            if (eq_pos != std::u16string::npos) return false;
            eq_pos = i;
        } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    if (eq_pos == 0 || (eq_pos != std::u16string::npos && eq_pos + 1 == s.size())) return false;
    return true;
}

bool lookup_property_raw(const std::u16string& raw, bool allow_strings,
                         regexp_unicode::PropertyLookup& out) {
    size_t eq_pos = std::u16string::npos;
    if (!is_valid_unicode_prop_name_chars(raw, eq_pos)) return false;
    std::string ascii;
    ascii.reserve(raw.size());
    for (char16_t c : raw) ascii.push_back(static_cast<char>(c));
    std::string_view sv(ascii);
    std::string_view key;
    std::string_view val = sv;
    if (eq_pos != std::u16string::npos) {
        key = sv.substr(0, eq_pos);
        val = sv.substr(eq_pos + 1);
    }
    out = regexp_unicode::lookup_unicode_property(key, val, allow_strings);
    return out.kind != regexp_unicode::PropertyKind::Invalid;
}

void append_property_to_class_set(const regexp_unicode::PropertyLookup& prop, ClassSet& out) {
    out.chars.add_ranges(prop.ranges);
    if (prop.kind == regexp_unicode::PropertyKind::Strings) {
        out.may_contain_strings = true;
        out.strings.reserve(out.strings.size() + prop.seq_count);
        for (size_t i = 0; i < prop.seq_count; ++i) {
            const uint32_t* data = nullptr;
            size_t len = 0;
            regexp_unicode::get_string_sequence(prop.seq_offset + i, data, len);
            out.strings.emplace_back(data, data + len);
        }
    }
}

uint32_t canonicalize_cp(uint32_t c, bool unicode_mode) {
    return unicode_mode ? regexp_unicode::unicode_case_canonical(c)
                        : regexp_unicode::legacy_case_canonical(c);
}

// ---------------------------------------------------------------------------
// Pattern parser
// ---------------------------------------------------------------------------

class PatternParser {
public:
    PatternParser(const std::u16string& pattern, Program& program)
        : m_pattern(pattern), m_program(program) {
        m_mode_flags = 0;
        if (program.ignore_case) m_mode_flags |= kFlagIgnoreCase;
        if (program.multiline)   m_mode_flags |= kFlagMultiline;
        if (program.dot_all)     m_mode_flags |= kFlagDotAll;
    }

    void parse() {
        m_total_groups = count_groups();
        m_has_named_groups = scan_has_named_groups();
        std::unordered_set<std::string> top_names;
        m_program.root = parse_disjunction(top_names);
        if (m_pos < m_pattern.size()) {
            fail(m_pattern[m_pos] == ')' ? "Unmatched ')'" : "Unexpected character");
        }
        m_program.group_count = m_next_group;
        resolve_named_iterative(m_program.root.get());
    }

private:
    using NameSet = std::unordered_set<std::string>;

    [[noreturn]] void fail(const std::string& message) const {
        throw RegExpSyntaxError{
            "Invalid regular expression: /" + m_program.source + "/" + m_program.flags + ": " + message};
    }

    bool u_mode() const { return m_program.unicode_mode(); }
    bool v_mode() const { return m_program.unicode_sets; }
    bool ignore_case_now() const { return (m_mode_flags & kFlagIgnoreCase) != 0; }
    uint32_t max_cp() const { return u_mode() ? 0x10FFFFu : 0xFFFFu; }

    bool at_end() const { return m_pos >= m_pattern.size(); }
    char16_t peek(size_t offset = 0) const {
        return m_pos + offset < m_pattern.size() ? m_pattern[m_pos + offset] : u'\0';
    }
    bool eat(char16_t c) {
        if (!at_end() && m_pattern[m_pos] == c) { ++m_pos; return true; }
        return false;
    }

    /// The next pattern character, joining a surrogate pair in unicode / unicodeSets mode.
    uint32_t next_code_point() {
        uint32_t c = m_pattern[m_pos++];
        if (u_mode() && c >= 0xD800 && c <= 0xDBFF && !at_end() &&
            m_pattern[m_pos] >= 0xDC00 && m_pattern[m_pos] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (m_pattern[m_pos++] - 0xDC00);
        }
        return c;
    }

    // Capture groups are numbered by their opening parenthesis, and a
    // backreference may point forwards, so the count is needed up front.
    int count_groups() const {
        int count = 0;
        int class_depth = 0;
        for (size_t i = 0; i < m_pattern.size(); ++i) {
            char16_t c = m_pattern[i];
            if (c == '\\') { ++i; continue; }
            if (class_depth > 0) {
                if (v_mode() && c == '[') ++class_depth;
                else if (c == ']') --class_depth;
                continue;
            }
            if (c == '[') { class_depth = 1; continue; }
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

    bool scan_has_named_groups() const {
        int class_depth = 0;
        for (size_t i = 0; i < m_pattern.size(); ++i) {
            char16_t c = m_pattern[i];
            if (c == '\\') { ++i; continue; }
            if (class_depth > 0) {
                if (v_mode() && c == '[') ++class_depth;
                else if (c == ']') --class_depth;
                continue;
            }
            if (c == '[') { class_depth = 1; continue; }
            if (c == '(' && i + 3 < m_pattern.size() && m_pattern[i + 1] == '?' &&
                m_pattern[i + 2] == '<' && m_pattern[i + 3] != '=' && m_pattern[i + 3] != '!') {
                return true;
            }
        }
        return false;
    }

    std::unique_ptr<Node> make(Node::Type type) {
        auto node = std::make_unique<Node>();
        node->type = type;
        node->mode_flags = m_mode_flags;
        return node;
    }

    std::unique_ptr<Node> parse_disjunction(NameSet& out_names) {
        if (stack_is_exhausted()) fail("Pattern is nested too deeply");
        NameSet first_names;
        auto first = parse_alternative(first_names);
        if (peek() != '|' || at_end()) {
            out_names = std::move(first_names);
            return first;
        }
        auto alt = make(Node::Type::Alternation);
        alt->children.push_back(std::move(first));
        out_names = std::move(first_names);
        while (eat('|')) {
            NameSet branch_names;
            alt->children.push_back(parse_alternative(branch_names));
            for (const auto& n : branch_names) out_names.insert(n);
        }
        return alt;
    }

    std::unique_ptr<Node> parse_alternative(NameSet& out_names) {
        auto seq = make(Node::Type::Sequence);
        while (!at_end() && peek() != '|' && peek() != ')') {
            NameSet term_names;
            seq->children.push_back(parse_term(term_names));
            for (const auto& n : term_names) {
                if (!out_names.insert(n).second) {
                    fail("Duplicate capture group name");
                }
            }
        }
        if (seq->children.size() == 1) return std::move(seq->children[0]);
        if (seq->children.empty()) return make(Node::Type::Empty);
        return seq;
    }

    std::unique_ptr<Node> parse_term(NameSet& out_names) {
        const int groups_before = m_next_group;
        std::unique_ptr<Node> atom;
        bool quantifiable = true;
        char16_t c = peek();

        if (c == '^') { ++m_pos; atom = make(Node::Type::LineStart); quantifiable = false; }
        else if (c == '$') { ++m_pos; atom = make(Node::Type::LineEnd); quantifiable = false; }
        else if (c == '(') { atom = parse_group(quantifiable, out_names); }
        else if (c == '.') { ++m_pos; atom = make(Node::Type::Any); }
        else if (c == '[') { ++m_pos; atom = v_mode() ? parse_v_class() : parse_class(); }
        else if (c == '\\') { ++m_pos; atom = parse_escape(quantifiable); }
        else if (c == '*' || c == '+' || c == '?') { fail("Nothing to repeat"); }
        else if (c == '{' && u_mode()) { fail("Nothing to repeat"); }
        else if ((c == '}' || c == ']') && u_mode()) { fail("Lone quantifier brackets"); }
        else {
            if (c == '{') {
                // Outside unicode mode, `{n}` with nothing before it is a SyntaxError if it's a valid quantifier.
                size_t save = m_pos;
                ++m_pos;
                size_t dmin = 0, dmax = 0;
                if (parse_braces(dmin, dmax)) fail("Nothing to repeat");
                m_pos = save;
            }
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
                if (u_mode()) fail("Incomplete quantifier");
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

    static uint8_t modifier_bit(char16_t c) {
        switch (c) {
            case 'i': return kFlagIgnoreCase;
            case 'm': return kFlagMultiline;
            case 's': return kFlagDotAll;
            default:  return 0;
        }
    }

    std::unique_ptr<Node> parse_group(bool& quantifiable, NameSet& out_names) {
        ++m_pos;        // '('
        if (eat('?')) {
            if (eat(':')) {
                auto inner = parse_disjunction(out_names);
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
                look->children.push_back(parse_disjunction(out_names));
                if (!eat(')')) fail("Unterminated group");
                // Annex B allows quantifying a lookahead outside unicode mode.
                quantifiable = !u_mode();
                return look;
            }
            if (eat('<')) {
                if (peek() == '=' || peek() == '!') {
                    bool negate = m_pattern[m_pos++] == '!';
                    auto look = make(Node::Type::LookBehind);
                    look->negate = negate;
                    look->children.push_back(parse_disjunction(out_names));
                    if (!eat(')')) fail("Unterminated group");
                    quantifiable = false;
                    return look;
                }
                std::string name = parse_group_name();
                auto group = make(Node::Type::Group);
                group->group = m_next_group++;
                bool seen_globally = false;
                for (const auto& u : m_program.unique_names) {
                    if (u == name) { seen_globally = true; break; }
                }
                if (!seen_globally) m_program.unique_names.push_back(name);
                m_program.names.emplace_back(name, static_cast<size_t>(group->group));
                NameSet inner_names;
                group->children.push_back(parse_disjunction(inner_names));
                if (!eat(')')) fail("Unterminated group");
                if (inner_names.count(name)) fail("Duplicate capture group name");
                inner_names.insert(name);
                out_names = std::move(inner_names);
                return group;
            }
            // ES2025 RegExp modifiers: `(?ims-ims:...)`
            if (peek() == '-' || modifier_bit(peek()) != 0) {
                uint8_t add_mask = 0;
                uint8_t rem_mask = 0;
                while (!at_end() && modifier_bit(peek()) != 0) {
                    uint8_t bit = modifier_bit(m_pattern[m_pos++]);
                    if (add_mask & bit) fail("Repeated flag in modifier group");
                    add_mask |= bit;
                }
                if (eat('-')) {
                    while (!at_end() && modifier_bit(peek()) != 0) {
                        uint8_t bit = modifier_bit(m_pattern[m_pos++]);
                        if ((rem_mask & bit) || (add_mask & bit)) {
                            fail("Invalid or repeated flag in modifier group");
                        }
                        rem_mask |= bit;
                    }
                }
                if ((add_mask | rem_mask) == 0 || !eat(':')) {
                    fail("Invalid modifier group");
                }
                uint8_t saved_flags = m_mode_flags;
                m_mode_flags = static_cast<uint8_t>((m_mode_flags | add_mask) & ~rem_mask);
                auto inner = parse_disjunction(out_names);
                m_mode_flags = saved_flags;
                if (!eat(')')) fail("Unterminated group");
                auto group = make(Node::Type::Group);
                group->children.push_back(std::move(inner));
                return group;
            }
            fail("Invalid group");
        }
        auto group = make(Node::Type::Group);
        group->group = m_next_group++;
        group->children.push_back(parse_disjunction(out_names));
        if (!eat(')')) fail("Unterminated group");
        return group;
    }

    char16_t previous() const { return m_pattern[m_pos - 1]; }

    /// Parses a Unicode escape sequence inside a capture group name (`\uXXXX`
    /// with optional surrogate pair, or `\u{H...}`). Always valid regardless of `u`/`v`.
    uint32_t parse_group_name_unicode_escape() {
        if (!eat('u')) fail("Invalid capture group name");
        if (eat('{')) {
            uint32_t cp = 0;
            bool has_digit = false;
            while (!at_end() && peek() != '}') {
                uint32_t d = 0;
                if (!parse_hex(1, d)) fail("Invalid capture group name");
                has_digit = true;
                cp = cp * 16 + d;
                if (cp > 0x10FFFF) fail("Invalid capture group name");
            }
            if (!eat('}') || !has_digit) fail("Invalid capture group name");
            return cp;
        }
        uint32_t value = 0;
        if (!parse_hex(4, value)) fail("Invalid capture group name");
        if (value >= 0xD800 && value <= 0xDBFF && peek() == '\\' && peek(1) == 'u') {
            size_t save = m_pos;
            m_pos += 2;
            uint32_t low = 0;
            if (parse_hex(4, low) && low >= 0xDC00 && low <= 0xDFFF) {
                return 0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
            }
            m_pos = save;
        }
        return value;
    }

    static void append_utf16_cp(std::u16string& dst, uint32_t cp) {
        if (cp <= 0xFFFF) {
            dst.push_back(static_cast<char16_t>(cp));
        } else {
            uint32_t adj = cp - 0x10000;
            dst.push_back(static_cast<char16_t>(0xD800 + (adj >> 10)));
            dst.push_back(static_cast<char16_t>(0xDC00 + (adj & 0x3FF)));
        }
    }

    std::string parse_group_name() {
        std::u16string name;
        bool first = true;
        while (!at_end() && peek() != '>') {
            uint32_t cp = 0;
            if (eat('\\')) {
                cp = parse_group_name_unicode_escape();
            } else {
                cp = m_pattern[m_pos++];
                if (cp >= 0xD800 && cp <= 0xDBFF && !at_end() &&
                    m_pattern[m_pos] >= 0xDC00 && m_pattern[m_pos] <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (m_pattern[m_pos++] - 0xDC00);
                }
            }
            // Lone surrogates are invalid in RegExpIdentifierName.
            if (cp >= 0xD800 && cp <= 0xDFFF) fail("Invalid capture group name");
            if (first) {
                if (!(cp == '$' || cp == '_' || is_js_id_start(cp))) {
                    fail("Invalid capture group name");
                }
                first = false;
            } else {
                if (!(cp == '$' || cp == 0x200C || cp == 0x200D || is_js_id_continue(cp))) {
                    fail("Invalid capture group name");
                }
            }
            append_utf16_cp(name, cp);
        }
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

    /// Parses a character escape after `\` has been consumed.
    /// `in_class`: true inside `[...]` (allows `\-` in `u` mode and `\b` as U+0008).
    /// `v_class`: true inside `v`-mode `[...]` or `\q{...}` (allows ClassSetReservedPunctuator).
    bool parse_character_escape(uint32_t& out, bool in_class, bool v_class) {
        if (at_end()) fail("\\ at end of pattern");
        char16_t c = m_pattern[m_pos++];
        switch (c) {
            case 'n': out = '\n'; return true;
            case 'r': out = '\r'; return true;
            case 't': out = '\t'; return true;
            case 'f': out = '\f'; return true;
            case 'v': out = '\v'; return true;
            case 'b':
                if (in_class) { out = 0x08; return true; }
                break;
            case '0':
                if (peek() < '0' || peek() > '9') { out = 0; return true; }
                if (u_mode()) fail("Invalid decimal escape");
                break;
            case 'c': {
                char16_t letter = peek();
                if ((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z')) {
                    ++m_pos;
                    out = static_cast<uint32_t>(letter % 32);
                    return true;
                }
                if (u_mode()) fail("Invalid control escape");
                // Annex B: inside a class, \c0..\c9 and \c_ are ClassControlLetter.
                if (in_class && ((letter >= '0' && letter <= '9') || letter == '_')) {
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
                if (u_mode()) fail("Invalid escape");
                out = 'x';
                return true;
            }
            case 'u': {
                uint32_t value = 0;
                if (peek() == '{' && u_mode()) {
                    ++m_pos;
                    uint32_t cp = 0;
                    bool has_digit = false;
                    while (!at_end() && peek() != '}') {
                        uint32_t d = 0;
                        if (!parse_hex(1, d)) fail("Invalid Unicode escape");
                        has_digit = true;
                        cp = cp * 16 + d;
                        if (cp > 0x10FFFF) fail("Invalid Unicode escape");
                    }
                    if (!eat('}') || !has_digit) fail("Invalid Unicode escape");
                    out = cp;
                    return true;
                }
                if (parse_hex(4, value)) {
                    // \uD83D\uDE00 is one code point in unicode / unicodeSets mode.
                    if (u_mode() && value >= 0xD800 && value <= 0xDBFF && peek() == '\\' && peek(1) == 'u') {
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
                if (u_mode()) fail("Invalid Unicode escape");
                out = 'u';
                return true;
            }
            default:
                break;
        }
        if (c >= '0' && c <= '7' && !u_mode()) {
            // Legacy octal escape.
            uint32_t value = static_cast<uint32_t>(c - '0');
            for (int i = 0; i < 2 && peek() >= '0' && peek() <= '7' &&
                 value * 8 + static_cast<uint32_t>(peek() - '0') <= 0xFF; ++i) {
                value = value * 8 + static_cast<uint32_t>(m_pattern[m_pos++] - '0');
            }
            out = value;
            return true;
        }
        if (u_mode()) {
            // Strict IdentityEscape rules in `u` and `v` mode.
            if (is_syntax_char(c) || c == '/') {
                out = c;
                return true;
            }
            if (in_class && c == '-') {
                out = '-';
                return true;
            }
            if (v_class && is_v_class_reserved_punctuator(c)) {
                out = c;
                return true;
            }
            fail("Invalid escape");
        }
        // Annex B identity escape outside unicode mode: the character itself.
        --m_pos;
        out = next_code_point();
        return true;
    }

    bool parse_class_escape_into(char16_t c, CharClass& out) {
        CharClass base;
        const bool um = u_mode();
        const bool ic = ignore_case_now();
        switch (c) {
            case 'd': add_digit(out); return true;
            case 'w': add_word(out, um, ic); return true;
            case 's': add_space(out); return true;
            case 'D': add_digit(base); out.add_complement(base, max_cp()); return true;
            case 'W': add_word(base, um, ic); out.add_complement(base, max_cp()); return true;
            case 'S': add_space(base); out.add_complement(base, max_cp()); return true;
            default: return false;
        }
    }

    /// Parses `\p{...}` or `\P{...}` (after `p`/`P` has been consumed).
    void parse_property_escape_into(bool negate, bool allow_strings, ClassSet& out) {
        if (!eat('{')) fail("Invalid property name");
        std::u16string raw;
        while (!at_end() && peek() != '}') raw += m_pattern[m_pos++];
        if (!eat('}')) fail("Invalid property name");
        regexp_unicode::PropertyLookup prop;
        if (!lookup_property_raw(raw, allow_strings && !negate, prop)) {
            fail("Invalid property name");
        }
        if (negate) {
            // Properties of strings are never valid with \P{...}.
            ClassSet base;
            base.chars.add_ranges(prop.ranges);
            base.chars.normalize();
            if (v_mode() && ignore_case_now()) {
                base.chars.close_case(true);
            }
            base.chars.complement_in_place(0x10FFFF);
            out.union_with(base);
        } else {
            ClassSet base;
            append_property_to_class_set(prop, base);
            base.normalize();
            if (v_mode() && ignore_case_now()) {
                base.canonicalize_for_v_ignore_case();
            }
            out.union_with(base);
        }
    }

    std::unique_ptr<Node> make_node_from_class_set(ClassSet cs) {
        cs.normalize();
        if (!cs.matches_empty && cs.strings.empty()) {
            auto node = make(Node::Type::Class);
            node->klass = std::move(cs.chars);
            node->klass.negated = false;
            return node;
        }
        auto node = make(Node::Type::StringSet);
        node->class_set = std::move(cs);
        return node;
    }

    std::unique_ptr<Node> parse_escape(bool& quantifiable) {
        if (at_end()) fail("\\ at end of pattern");
        char16_t c = peek();

        if (c == 'b' || c == 'B') {
            ++m_pos;
            quantifiable = false;
            return make(c == 'b' ? Node::Type::WordBoundary : Node::Type::NotWordBoundary);
        }
        if (c == 'k' && (u_mode() || m_has_named_groups)) {
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
            if (u_mode()) fail("Invalid escape");
            m_pos = save;       // not a group: fall through to a legacy octal or identity escape
        }
        if (c == 'd' || c == 'D' || c == 'w' || c == 'W' || c == 's' || c == 'S') {
            ++m_pos;
            auto klass = make(Node::Type::Class);
            if (v_mode()) {
                // In v mode: close positive class under Unicode case folding before complementing.
                bool neg = (c == 'D' || c == 'W' || c == 'S');
                char16_t pos_c = (c == 'D') ? 'd' : (c == 'W') ? 'w' : (c == 'S') ? 's' : c;
                parse_class_escape_into(pos_c, klass->klass);
                klass->klass.normalize();
                if (ignore_case_now()) klass->klass.close_case(true);
                if (neg) klass->klass.complement_in_place(0x10FFFF);
            } else {
                parse_class_escape_into(c, klass->klass);
                klass->klass.normalize();
                if (ignore_case_now()) klass->klass.close_case(u_mode());
            }
            return klass;
        }
        if ((c == 'p' || c == 'P') && u_mode()) {
            ++m_pos;
            ClassSet cs;
            parse_property_escape_into(c == 'P', v_mode(), cs);
            if (!v_mode() && ignore_case_now()) {
                cs.chars.close_case(true);
            }
            return make_node_from_class_set(std::move(cs));
        }
        auto literal = make(Node::Type::Char);
        parse_character_escape(literal->ch, false, false);
        return literal;
    }

    // -----------------------------------------------------------------------
    // Non-v character class parser (`[...]` for legacy and `u` mode)
    // -----------------------------------------------------------------------

    /// One member of a legacy/`u` character class: a single code point or a class escape.
    /// Returns true when it is a single code point (stored in `out`).
    bool parse_class_atom(uint32_t& out, CharClass& set) {
        if (peek() != '\\') {
            out = next_code_point();
            return true;
        }
        ++m_pos;
        if (at_end()) fail("\\ at end of pattern");
        char16_t c = peek();
        if (parse_class_escape_into(c, set)) { ++m_pos; return false; }
        if ((c == 'p' || c == 'P') && u_mode()) {
            ++m_pos;
            ClassSet cs;
            parse_property_escape_into(c == 'P', false, cs);
            set.add_class(cs.chars);
            return false;
        }
        parse_character_escape(out, true, false);
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
            bool is_single_atom = parse_class_atom(lo, set);
            if (!is_single_atom) {
                // In `u` mode, a ClassEscape followed by `-` and another class atom is SyntaxError.
                if (peek() == '-' && peek(1) != ']' && m_pos + 1 < m_pattern.size()) {
                    if (u_mode()) fail("Invalid character class");
                    // Annex B: `-` is literal, continue normally so next loop consumes `-`.
                }
                continue;
            }
            // A range needs a `-` that is followed by something other than `]`.
            if (peek() == '-' && peek(1) != ']' && m_pos + 1 < m_pattern.size()) {
                ++m_pos;
                uint32_t hi = 0;
                if (parse_class_atom(hi, set)) {
                    if (hi < lo) fail("Range out of order in character class");
                    set.add(lo, hi);
                    continue;
                }
                // `a-\d`: invalid in `u` mode; in Annex B the dash is a literal.
                if (u_mode()) fail("Invalid character class");
                set.add(lo, lo);
                set.add('-', '-');
                continue;
            }
            set.add(lo, lo);
        }
        set.normalize();
        if (ignore_case_now()) {
            set.close_case(u_mode());
        }
        return node;
    }

    // -----------------------------------------------------------------------
    // ES2024 `v` (`unicodeSets`) ClassSetExpression parser
    // -----------------------------------------------------------------------

    /// Parses one `ClassSetCharacter` (single code point, never a class escape).
    uint32_t parse_v_class_char() {
        if (at_end()) fail("Unterminated character class");
        char16_t c = peek();
        if (c == '\\') {
            ++m_pos;
            uint32_t out = 0;
            parse_character_escape(out, true, true);
            return out;
        }
        if (is_v_class_syntax_char(c)) {
            fail("Invalid character in character class");
        }
        if (is_v_double_punctuator_char(c) && peek(1) == c) {
            fail("Invalid double punctuator in character class");
        }
        return next_code_point();
    }

    /// Parses `\q{alt1|alt2|...}` (after `\q` has been consumed).
    ClassSet parse_v_string_disjunction() {
        if (!eat('{')) fail("Invalid \\q escape in character class");
        ClassSet cs;
        while (true) {
            std::vector<uint32_t> str;
            while (!at_end() && peek() != '|' && peek() != '}') {
                str.push_back(parse_v_class_char());
            }
            cs.add_string(std::move(str));
            if (eat('}')) break;
            if (!eat('|')) fail("Unterminated \\q escape in character class");
        }
        cs.normalize();
        if (ignore_case_now()) cs.canonicalize_for_v_ignore_case();
        return cs;
    }

    /// Parses a `ClassSetOperand`: NestedClass, `\q{...}`, CharacterClassEscape,
    /// or ClassSetCharacter. Sets `is_single_char = true` and `single_cp` ONLY
    /// when the operand is a bare `ClassSetCharacter` (eligible to start a range).
    ClassSet parse_v_operand(bool& is_single_char, uint32_t& single_cp) {
        if (stack_is_exhausted()) fail("Character class nested too deeply");
        is_single_char = false;
        single_cp = 0;

        if (at_end()) fail("Unterminated character class");
        char16_t c = peek();

        if (c == '[') {
            ++m_pos;
            return parse_v_class_body();
        }
        if (c == '\\') {
            if (m_pos + 1 >= m_pattern.size()) fail("\\ at end of pattern");
            char16_t next = peek(1);
            if (next == 'q') {
                m_pos += 2;
                return parse_v_string_disjunction();
            }
            if (next == 'p' || next == 'P') {
                m_pos += 2;
                ClassSet cs;
                parse_property_escape_into(next == 'P', true, cs);
                return cs;
            }
            if (next == 'd' || next == 'D' || next == 'w' || next == 'W' || next == 's' || next == 'S') {
                m_pos += 2;
                ClassSet cs;
                bool neg = (next == 'D' || next == 'W' || next == 'S');
                char16_t pos_c = (next == 'D') ? 'd' : (next == 'W') ? 'w' : (next == 'S') ? 's' : next;
                parse_class_escape_into(pos_c, cs.chars);
                cs.chars.normalize();
                if (ignore_case_now()) cs.chars.close_case(true);
                if (neg) cs.chars.complement_in_place(0x10FFFF);
                return cs;
            }
        }
        single_cp = parse_v_class_char();
        is_single_char = true;
        ClassSet cs;
        cs.chars.add(single_cp, single_cp);
        if (ignore_case_now()) cs.chars.close_case(true);
        return cs;
    }

    /// Parses the body of `[ ... ]` or `[^ ... ]` in `v` mode (after `[` is consumed),
    /// consuming the closing `]`.
    ClassSet parse_v_class_body() {
        if (stack_is_exhausted()) fail("Character class nested too deeply");
        const bool negated = eat('^');
        ClassSet result;

        if (eat(']')) {
            // Empty `[]` or `[^]`
            if (negated) result.chars.complement_in_place(0x10FFFF);
            return result;
        }

        // Parse first operand (might start a range `a-z`, union, intersection `&&`, or subtraction `--`).
        bool first_is_char = false;
        uint32_t first_cp = 0;
        ClassSet first = parse_v_operand(first_is_char, first_cp);

        // Check for ClassIntersection (`&&` with lookahead != `&`)
        if (peek() == '&' && peek(1) == '&') {
            if (peek(2) == '&') fail("Invalid intersection operator in character class");
            result = std::move(first);
            while (peek() == '&' && peek(1) == '&') {
                if (peek(2) == '&') fail("Invalid intersection operator in character class");
                m_pos += 2;
                bool rhs_char = false;
                uint32_t rhs_cp = 0;
                ClassSet rhs = parse_v_operand(rhs_char, rhs_cp);
                result.intersect_with(rhs);
            }
            if (!eat(']')) fail("Expected ']' after class intersection");
        } else if (peek() == '-' && peek(1) == '-') {
            // ClassSubtraction (`--`)
            result = std::move(first);
            while (peek() == '-' && peek(1) == '-') {
                m_pos += 2;
                bool rhs_char = false;
                uint32_t rhs_cp = 0;
                ClassSet rhs = parse_v_operand(rhs_char, rhs_cp);
                result.subtract(rhs);
            }
            if (!eat(']')) fail("Expected ']' after class subtraction");
        } else {
            // ClassUnion: optional range on the first item, then remaining union items.
            if (first_is_char && peek() == '-') {
                // Note: `--` was already handled above, so single `-` starts a ClassSetRange.
                ++m_pos;
                uint32_t hi = parse_v_class_char();
                if (hi < first_cp) fail("Range out of order in character class");
                ClassSet range_set;
                range_set.chars.add(first_cp, hi);
                if (ignore_case_now()) range_set.chars.close_case(true);
                result.union_with(range_set);
            } else {
                result.union_with(first);
            }

            while (!eat(']')) {
                if (at_end()) fail("Unterminated character class");
                // Cannot introduce `&&` or `--` inside a ClassUnion!
                if ((peek() == '&' && peek(1) == '&') || (peek() == '-' && peek(1) == '-')) {
                    fail("Cannot mix set operators inside character class union");
                }
                bool item_is_char = false;
                uint32_t item_cp = 0;
                ClassSet item = parse_v_operand(item_is_char, item_cp);
                if (item_is_char && peek() == '-' && peek(1) != '-') {
                    ++m_pos;
                    uint32_t hi = parse_v_class_char();
                    if (hi < item_cp) fail("Range out of order in character class");
                    ClassSet range_set;
                    range_set.chars.add(item_cp, hi);
                    if (ignore_case_now()) range_set.chars.close_case(true);
                    result.union_with(range_set);
                } else {
                    result.union_with(item);
                }
            }
        }

        result.normalize();
        if (negated) {
            if (result.may_contain_strings) {
                fail("Cannot negate character class that may contain strings");
            }
            result.chars.complement_in_place(0x10FFFF);
            result.strings.clear();
            result.matches_empty = false;
            result.may_contain_strings = false;
        }
        return result;
    }

    std::unique_ptr<Node> parse_v_class() {
        return make_node_from_class_set(parse_v_class_body());
    }

    void resolve_named_iterative(Node* root) {
        if (!root) return;
        std::vector<Node*> stack;
        stack.push_back(root);
        while (!stack.empty()) {
            Node* node = stack.back();
            stack.pop_back();
            if (node->type == Node::Type::BackReference && node->group < 0) {
                for (const auto& [name, index] : m_program.names) {
                    if (name == node->name) {
                        node->named_targets.push_back(static_cast<int>(index));
                    }
                }
                if (node->named_targets.empty()) fail("Invalid named capture referenced");
                if (node->named_targets.size() == 1) {
                    node->group = node->named_targets[0];
                    node->named_targets.clear();
                }
            }
            for (auto& child : node->children) {
                if (child) stack.push_back(child.get());
            }
        }
    }

    void free_ast_iterative(std::unique_ptr<Node>& root) {
        if (!root) return;
        std::vector<std::unique_ptr<Node>> stack;
        stack.push_back(std::move(root));
        while (!stack.empty()) {
            auto node = std::move(stack.back());
            stack.pop_back();
            for (auto& child : node->children) {
                if (child) stack.push_back(std::move(child));
            }
            node->children.clear();
        }
    }

    static bool is_single(const Node* node) {
        return node->type == Node::Type::Char || node->type == Node::Type::Any || node->type == Node::Type::Class;
    }

    void compile_node(const Node* node, bool backward) {
        if (stack_is_exhausted()) throw RegExpRangeError{"Regular expression too complex: nesting limit exceeded"};
        auto& code = m_program.code;
        switch (node->type) {
            case Node::Type::Empty:
                break;

            case Node::Type::Char: {
                RegExpInsn insn;
                insn.op = RegExpOp::MatchChar;
                insn.mode_flags = node->mode_flags;
                insn.flag1 = backward;
                insn.a = node->ch;
                code.push_back(insn);
                break;
            }

            case Node::Type::Any: {
                RegExpInsn insn;
                insn.op = RegExpOp::MatchAny;
                insn.mode_flags = node->mode_flags;
                insn.flag1 = backward;
                code.push_back(insn);
                break;
            }

            case Node::Type::Class: {
                RegExpInsn insn;
                insn.op = RegExpOp::MatchClass;
                insn.mode_flags = node->mode_flags;
                insn.flag1 = backward;
                insn.a = m_program.classes.size();
                m_program.classes.push_back(node->klass);
                code.push_back(insn);
                break;
            }

            case Node::Type::StringSet: {
                RegExpInsn insn;
                insn.op = RegExpOp::MatchStringSet;
                insn.mode_flags = node->mode_flags;
                insn.flag1 = backward;
                insn.a = m_program.string_sets.size();
                m_program.string_sets.push_back(
                    StringSet::from_class_set(node->class_set, (node->mode_flags & kFlagIgnoreCase) != 0));
                code.push_back(insn);
                break;
            }

            case Node::Type::LineStart: {
                RegExpInsn insn;
                insn.op = RegExpOp::LineStart;
                insn.mode_flags = node->mode_flags;
                code.push_back(insn);
                break;
            }

            case Node::Type::LineEnd: {
                RegExpInsn insn;
                insn.op = RegExpOp::LineEnd;
                insn.mode_flags = node->mode_flags;
                code.push_back(insn);
                break;
            }

            case Node::Type::WordBoundary:
            case Node::Type::NotWordBoundary: {
                RegExpInsn insn;
                insn.op = (node->type == Node::Type::WordBoundary) ? RegExpOp::WordBoundary : RegExpOp::NotWordBoundary;
                insn.mode_flags = node->mode_flags;
                code.push_back(insn);
                break;
            }

            case Node::Type::Sequence: {
                const size_t n = node->children.size();
                for (size_t i = 0; i < n; ++i) {
                    const Node* item = backward ? node->children[n - 1 - i].get() : node->children[i].get();
                    compile_node(item, backward);
                }
                break;
            }

            case Node::Type::Alternation: {
                const size_t n = node->children.size();
                if (n == 0) break;
                if (n == 1) {
                    compile_node(node->children[0].get(), backward);
                    break;
                }
                std::vector<size_t> end_jumps;
                for (size_t i = 0; i + 1 < n; ++i) {
                    size_t split_pc = code.size();
                    RegExpInsn split;
                    split.op = RegExpOp::Split;
                    split.a = split_pc + 1;
                    code.push_back(split);

                    compile_node(node->children[i].get(), backward);

                    size_t jmp_pc = code.size();
                    RegExpInsn jmp;
                    jmp.op = RegExpOp::Jump;
                    code.push_back(jmp);
                    end_jumps.push_back(jmp_pc);

                    code[split_pc].b = code.size();
                }
                compile_node(node->children[n - 1].get(), backward);
                const size_t end_pc = code.size();
                for (size_t j : end_jumps) code[j].a = end_pc;
                break;
            }

            case Node::Type::Group: {
                if (node->group < 0) {
                    compile_node(node->children[0].get(), backward);
                    break;
                }
                RegExpInsn begin_insn;
                begin_insn.op = RegExpOp::BeginGroup;
                begin_insn.a = static_cast<size_t>(node->group);
                code.push_back(begin_insn);

                compile_node(node->children[0].get(), backward);

                RegExpInsn end_insn;
                end_insn.op = RegExpOp::EndGroup;
                end_insn.a = static_cast<size_t>(node->group);
                code.push_back(end_insn);
                break;
            }

            case Node::Type::Repeat: {
                const Node* atom = node->children[0].get();
                if (is_single(atom)) {
                    RegExpInsn insn;
                    insn.op = RegExpOp::SimpleRepeat;
                    insn.mode_flags = atom->mode_flags;
                    insn.flag1 = backward;
                    insn.flag2 = node->greedy;
                    insn.min = node->min;
                    insn.max = node->max;
                    if (atom->type == Node::Type::Char) {
                        insn.atom_kind = 0;
                        insn.a = atom->ch;
                    } else if (atom->type == Node::Type::Any) {
                        insn.atom_kind = 1;
                    } else {
                        insn.atom_kind = 2;
                        insn.a = m_program.classes.size();
                        m_program.classes.push_back(atom->klass);
                    }
                    code.push_back(insn);
                    break;
                }

                const size_t slot = m_program.repeat_count++;

                RegExpInsn enter_insn;
                enter_insn.op = RegExpOp::RepeatEnter;
                enter_insn.a = slot;
                code.push_back(enter_insn);

                const size_t step_pc = code.size();
                RegExpInsn step_insn;
                step_insn.op = RegExpOp::RepeatStep;
                step_insn.a = slot;
                step_insn.min = node->min;
                step_insn.max = node->max;
                step_insn.flag1 = node->greedy;
                code.push_back(step_insn);

                RegExpInsn body_insn;
                body_insn.op = RegExpOp::RepeatBody;
                body_insn.a = slot;
                body_insn.b = static_cast<size_t>(node->first_group);
                body_insn.last_group = node->last_group;
                code.push_back(body_insn);

                compile_node(atom, backward);

                RegExpInsn next_insn;
                next_insn.op = RegExpOp::RepeatNext;
                next_insn.a = slot;
                next_insn.b = step_pc;
                next_insn.min = node->min;
                code.push_back(next_insn);

                code[step_pc].b = code.size(); // skip_pc
                break;
            }

            case Node::Type::BackReference: {
                RegExpInsn insn;
                insn.op = RegExpOp::BackRef;
                insn.mode_flags = node->mode_flags;
                insn.flag1 = backward;
                if (!node->named_targets.empty()) {
                    insn.flag2 = true;
                    insn.a = m_program.named_backrefs.size();
                    m_program.named_backrefs.push_back(node->named_targets);
                } else {
                    insn.flag2 = false;
                    insn.a = static_cast<size_t>(node->group);
                }
                code.push_back(insn);
                break;
            }

            case Node::Type::LookAhead:
            case Node::Type::LookBehind: {
                const size_t jmp_over = code.size();
                RegExpInsn jmp;
                jmp.op = RegExpOp::Jump;
                code.push_back(jmp);

                const size_t sub_pc = code.size();
                compile_node(node->children[0].get(), node->type == Node::Type::LookBehind);
                code.push_back(RegExpInsn{RegExpOp::Accept});

                code[jmp_over].a = code.size();

                RegExpInsn insn;
                insn.op = RegExpOp::LookAround;
                insn.a = sub_pc;
                insn.flag1 = (node->type == Node::Type::LookBehind);
                insn.flag2 = node->negate;
                code.push_back(insn);
                break;
            }
        }
    }

public:
    void finalize_bytecode() {
        const Node* first = m_program.root.get();
        while (first && first->type == Node::Type::Sequence && !first->children.empty()) {
            first = first->children[0].get();
        }
        if (first && first->type == Node::Type::Char &&
            !(first->mode_flags & kFlagIgnoreCase) &&
            first->ch < 0x10000 && !(first->ch >= 0xD800 && first->ch <= 0xDFFF)) {
            m_program.has_literal_start = true;
            m_program.literal_start_ch = first->ch;
        }
        m_program.entry_pc = 0;
        compile_node(m_program.root.get(), false);
        m_program.code.push_back(RegExpInsn{RegExpOp::Accept});
        free_ast_iterative(m_program.root);
    }

private:
    const std::u16string& m_pattern;
    Program& m_program;
    size_t m_pos{0};
    int m_next_group{1};
    int m_total_groups{0};
    bool m_has_named_groups{false};
    uint8_t m_mode_flags{0};
};

// ---------------------------------------------------------------------------
// Matcher (heap-backed iterative backtracking VM)
// ---------------------------------------------------------------------------

class Matcher {
public:
    Matcher(const Program& program, const Utf16View& subject)
        : m_program(program), m_subject(subject),
          m_captures(static_cast<size_t>(program.group_count) * 2, -1),
          m_group_starts(static_cast<size_t>(program.group_count), -1),
          m_rep_counts(program.repeat_count, 0),
          m_rep_starts(program.repeat_count, 0) {}

    /// In unicode / unicodeSets mode the subject is a sequence of code points,
    /// so no match may begin between the two halves of a surrogate pair.
    bool splits_pair(size_t pos) const {
        return m_program.unicode_mode() && pos > 0 && pos < m_subject.size() &&
               m_subject[pos] >= 0xDC00 && m_subject[pos] <= 0xDFFF &&
               m_subject[pos - 1] >= 0xD800 && m_subject[pos - 1] <= 0xDBFF;
    }

    /// Tries to match starting exactly at `start`. On success the captures
    /// hold [start, end) pairs in UTF-16 units, -1 for unmatched groups.
    bool match_at(size_t start) {
        std::fill(m_captures.begin(), m_captures.end(), -1);
        std::fill(m_group_starts.begin(), m_group_starts.end(), -1);
        std::fill(m_rep_counts.begin(), m_rep_counts.end(), 0);
        std::fill(m_rep_starts.begin(), m_rep_starts.end(), 0);
        m_bt.clear();
        m_trail.clear();
        m_ends_pool.clear();

        size_t pos = start;
        if (!exec_vm(m_program.entry_pc, pos, 0)) return false;
        m_captures[0] = static_cast<long>(start);
        m_captures[1] = static_cast<long>(pos);
        return true;
    }

    const std::vector<long>& captures() const { return m_captures; }
    void reset_budget() { m_steps = 0; }

private:
    enum class BtKind : uint8_t {
        Choice,
        SimpleGreedyAscii,
        SimpleGreedyUnicode,
        SimpleLazy,
        StringSetChoice
    };

    struct BtFrame {
        BtKind kind{BtKind::Choice};
        size_t pc{0};
        size_t pos{0};
        size_t trail_mark{0};
        size_t start_or_insn{0};
        size_t min_count{0};
        size_t cur_count{0};
        bool backward{false};
    };

    enum class TrailKind : uint8_t {
        Capture,
        GroupStart,
        RepCount,
        RepStart
    };

    struct TrailEntry {
        TrailKind kind;
        size_t index;
        long old_long{0};
        size_t old_size{0};
    };

    void charge() {
        if (++m_steps > kMaxSteps) js_throw_range_error("Regular expression too complex: step limit exceeded");
    }

    /// The code point at `pos` and how many units it occupies.
    uint32_t code_point_at(size_t pos, size_t& width) const {
        uint32_t c = m_subject[pos];
        width = 1;
        if (m_program.unicode_mode() && c >= 0xD800 && c <= 0xDBFF && pos + 1 < m_subject.size()) {
            uint32_t low = m_subject[pos + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                width = 2;
                return 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
            }
        }
        return c;
    }

    /// The code point that ends at `pos`, read right to left, and its width.
    uint32_t code_point_before(size_t pos, size_t& width) const {
        uint32_t c = m_subject[pos - 1];
        width = 1;
        if (m_program.unicode_mode() && c >= 0xDC00 && c <= 0xDFFF && pos >= 2) {
            uint32_t high = m_subject[pos - 2];
            if (high >= 0xD800 && high <= 0xDBFF) {
                width = 2;
                return 0x10000 + ((high - 0xD800) << 10) + (c - 0xDC00);
            }
        }
        return c;
    }

    bool same_char(uint32_t a, uint32_t b, bool ignore_case) const {
        if (a == b) return true;
        if (!ignore_case) return false;
        const bool um = m_program.unicode_mode();
        return canonicalize_cp(a, um) == canonicalize_cp(b, um);
    }

    static bool class_matches(const CharClass& set, uint32_t c) {
        return set.contains(c) != set.negated;
    }

    bool match_simple_atom(const RegExpInsn& insn, size_t pos, size_t& width) const {
        uint32_t c;
        if (insn.flag1) {
            if (pos == 0) return false;
            c = code_point_before(pos, width);
        } else {
            if (pos >= m_subject.size()) return false;
            c = code_point_at(pos, width);
        }
        switch (insn.atom_kind) {
            case 0: return same_char(c, static_cast<uint32_t>(insn.a), (insn.mode_flags & kFlagIgnoreCase) != 0);
            case 1: return ((insn.mode_flags & kFlagDotAll) != 0) || !is_line_terminator(c);
            case 2: return class_matches(m_program.classes[insn.a], c);
            default: return false;
        }
    }

    /// Matches multi-codepoint sequence `seq` forward at `pos` or backward ending at `pos`.
    bool match_cp_seq(const std::vector<uint32_t>& seq, size_t pos, bool backward,
                      bool ignore_case, size_t& out_end) const {
        size_t p = pos;
        if (backward) {
            for (size_t i = seq.size(); i > 0; --i) {
                if (p == 0) return false;
                size_t w = 1;
                uint32_t c = code_point_before(p, w);
                if (!same_char(c, seq[i - 1], ignore_case)) return false;
                p -= w;
            }
        } else {
            for (uint32_t expected : seq) {
                if (p >= m_subject.size()) return false;
                size_t w = 1;
                uint32_t c = code_point_at(p, w);
                if (!same_char(c, expected, ignore_case)) return false;
                p += w;
            }
        }
        out_end = p;
        return true;
    }

    void unwind_trail(size_t mark) {
        while (m_trail.size() > mark) {
            const TrailEntry& e = m_trail.back();
            switch (e.kind) {
                case TrailKind::Capture: m_captures[e.index] = e.old_long; break;
                case TrailKind::GroupStart: m_group_starts[e.index] = e.old_long; break;
                case TrailKind::RepCount: m_rep_counts[e.index] = e.old_size; break;
                case TrailKind::RepStart: m_rep_starts[e.index] = e.old_size; break;
            }
            m_trail.pop_back();
        }
    }

    bool exec_vm(size_t pc, size_t& io_pos, size_t bt_base) {
        const auto& code = m_program.code;
        size_t pos = io_pos;

        auto backtrack = [&]() -> bool {
            while (m_bt.size() > bt_base) {
                BtFrame f = m_bt.back();
                m_bt.pop_back();
                unwind_trail(f.trail_mark);

                switch (f.kind) {
                    case BtKind::Choice:
                        pc = f.pc;
                        pos = f.pos;
                        return true;

                    case BtKind::SimpleGreedyAscii: {
                        charge();
                        const size_t try_count = f.cur_count;
                        if (try_count > f.min_count) {
                            m_bt.push_back({BtKind::SimpleGreedyAscii, f.pc, f.pos, f.trail_mark,
                                            f.start_or_insn, f.min_count, try_count - 1, f.backward});
                        }
                        pos = f.backward ? f.start_or_insn - try_count : f.start_or_insn + try_count;
                        pc = f.pc;
                        return true;
                    }

                    case BtKind::SimpleGreedyUnicode: {
                        charge();
                        const size_t try_i = f.cur_count;
                        const size_t next_pos = m_ends_pool[f.start_or_insn][try_i];
                        if (try_i > f.min_count) {
                            m_bt.push_back({BtKind::SimpleGreedyUnicode, f.pc, 0, f.trail_mark,
                                            f.start_or_insn, f.min_count, try_i - 1, f.backward});
                        }
                        pos = next_pos;
                        pc = f.pc;
                        return true;
                    }

                    case BtKind::SimpleLazy: {
                        const RegExpInsn& insn = code[f.start_or_insn];
                        size_t width = 1;
                        if (f.cur_count < insn.max && match_simple_atom(insn, f.pos, width)) {
                            charge();
                            const size_t next_pos = f.backward ? f.pos - width : f.pos + width;
                            const size_t next_count = f.cur_count + 1;
                            if (next_count < insn.max) {
                                m_bt.push_back({BtKind::SimpleLazy, f.pc, next_pos, f.trail_mark,
                                                f.start_or_insn, 0, next_count, f.backward});
                            }
                            pos = next_pos;
                            pc = f.pc;
                            return true;
                        }
                        break;
                    }

                    case BtKind::StringSetChoice: {
                        charge();
                        const auto& candidates = m_ends_pool[f.start_or_insn];
                        const size_t idx = f.cur_count;
                        pos = candidates[idx];
                        if (idx + 1 < candidates.size()) {
                            m_bt.push_back({BtKind::StringSetChoice, f.pc, 0, f.trail_mark,
                                            f.start_or_insn, 0, idx + 1, f.backward});
                        }
                        pc = f.pc;
                        return true;
                    }
                }
            }
            return false;
        };

        for (;;) {
            charge();
            const RegExpInsn& insn = code[pc];
            switch (insn.op) {
                case RegExpOp::Accept:
                    io_pos = pos;
                    m_bt.resize(bt_base);
                    return true;

                case RegExpOp::MatchChar: {
                    size_t width = 1;
                    uint32_t c = 0;
                    bool ok = true;
                    if (insn.flag1) {
                        if (pos == 0) ok = false;
                        else c = code_point_before(pos, width);
                    } else {
                        if (pos >= m_subject.size()) ok = false;
                        else c = code_point_at(pos, width);
                    }
                    if (!ok || !same_char(c, static_cast<uint32_t>(insn.a), (insn.mode_flags & kFlagIgnoreCase) != 0)) {
                        if (!backtrack()) return false;
                        continue;
                    }
                    pos = insn.flag1 ? pos - width : pos + width;
                    ++pc;
                    break;
                }

                case RegExpOp::MatchAny: {
                    size_t width = 1;
                    uint32_t c = 0;
                    bool ok = true;
                    if (insn.flag1) {
                        if (pos == 0) ok = false;
                        else c = code_point_before(pos, width);
                    } else {
                        if (pos >= m_subject.size()) ok = false;
                        else c = code_point_at(pos, width);
                    }
                    const bool dot_all = (insn.mode_flags & kFlagDotAll) != 0;
                    if (!ok || (!dot_all && is_line_terminator(c))) {
                        if (!backtrack()) return false;
                        continue;
                    }
                    pos = insn.flag1 ? pos - width : pos + width;
                    ++pc;
                    break;
                }

                case RegExpOp::MatchClass: {
                    size_t width = 1;
                    uint32_t c = 0;
                    bool ok = true;
                    if (insn.flag1) {
                        if (pos == 0) ok = false;
                        else c = code_point_before(pos, width);
                    } else {
                        if (pos >= m_subject.size()) ok = false;
                        else c = code_point_at(pos, width);
                    }
                    if (!ok || !class_matches(m_program.classes[insn.a], c)) {
                        if (!backtrack()) return false;
                        continue;
                    }
                    pos = insn.flag1 ? pos - width : pos + width;
                    ++pc;
                    break;
                }

                case RegExpOp::MatchStringSet: {
                    const StringSet& ss = m_program.string_sets[insn.a];
                    const bool backward = insn.flag1;
                    const bool ignore_case = (insn.mode_flags & kFlagIgnoreCase) != 0;
                    std::vector<size_t> candidates;

                    size_t edge_w = 1;
                    bool has_edge = false;
                    uint32_t edge_cp = 0;
                    if (backward) {
                        if (pos > 0) { edge_cp = code_point_before(pos, edge_w); has_edge = true; }
                    } else {
                        if (pos < m_subject.size()) { edge_cp = code_point_at(pos, edge_w); has_edge = true; }
                    }

                    if (has_edge) {
                        uint32_t key = ignore_case ? regexp_unicode::unicode_case_canonical(edge_cp) : edge_cp;
                        const auto& table = backward ? ss.by_last : ss.by_first;
                        auto it = table.find(key);
                        if (it != table.end()) {
                            for (size_t sidx : it->second) {
                                size_t end_p = 0;
                                if (match_cp_seq(ss.strings[sidx], pos, backward, ignore_case, end_p)) {
                                    // Deduplicate identical end positions preserving longest-first order
                                    if (candidates.empty() || candidates.back() != end_p) {
                                        candidates.push_back(end_p);
                                    }
                                }
                            }
                        }
                        if (ss.chars.contains(edge_cp)) {
                            candidates.push_back(backward ? pos - edge_w : pos + edge_w);
                        }
                    }
                    if (ss.matches_empty) {
                        candidates.push_back(pos);
                    }

                    if (candidates.empty()) {
                        if (!backtrack()) return false;
                        continue;
                    }
                    pos = candidates[0];
                    if (candidates.size() > 1) {
                        const size_t pool_idx = m_ends_pool.size();
                        m_ends_pool.push_back(std::move(candidates));
                        m_bt.push_back({BtKind::StringSetChoice, pc + 1, 0, m_trail.size(),
                                        pool_idx, 0, 1, backward});
                    }
                    ++pc;
                    break;
                }

                case RegExpOp::LineStart: {
                    const bool multiline = (insn.mode_flags & kFlagMultiline) != 0;
                    if (pos == 0 || (multiline && is_line_terminator(m_subject[pos - 1]))) {
                        ++pc;
                    } else if (!backtrack()) {
                        return false;
                    }
                    break;
                }

                case RegExpOp::LineEnd: {
                    const bool multiline = (insn.mode_flags & kFlagMultiline) != 0;
                    if (pos == m_subject.size() || (multiline && is_line_terminator(m_subject[pos]))) {
                        ++pc;
                    } else if (!backtrack()) {
                        return false;
                    }
                    break;
                }

                case RegExpOp::WordBoundary:
                case RegExpOp::NotWordBoundary: {
                    const bool um = m_program.unicode_mode();
                    const bool ic = (insn.mode_flags & kFlagIgnoreCase) != 0;
                    size_t dummy = 1;
                    bool before = pos > 0 && is_word_char(code_point_before(pos, dummy), um, ic);
                    bool after = pos < m_subject.size() && is_word_char(code_point_at(pos, dummy), um, ic);
                    bool boundary = before != after;
                    if (boundary == (insn.op == RegExpOp::WordBoundary)) {
                        ++pc;
                    } else if (!backtrack()) {
                        return false;
                    }
                    break;
                }

                case RegExpOp::BackRef: {
                    int target_group = -1;
                    if (insn.flag2) {
                        for (int g : m_program.named_backrefs[insn.a]) {
                            size_t s = static_cast<size_t>(g) * 2;
                            if (s + 1 < m_captures.size() && m_captures[s] >= 0) {
                                target_group = g;
                                break;
                            }
                        }
                    } else {
                        target_group = static_cast<int>(insn.a);
                    }
                    if (target_group < 0) {
                        ++pc;
                        break;
                    }
                    size_t slot = static_cast<size_t>(target_group) * 2;
                    if (slot + 1 >= m_captures.size() || m_captures[slot] < 0) {
                        ++pc;
                        break;
                    }
                    size_t start = static_cast<size_t>(m_captures[slot]);
                    size_t end = static_cast<size_t>(m_captures[slot + 1]);
                    size_t length = end - start;
                    if (insn.flag1 ? length > pos : pos + length > m_subject.size()) {
                        if (!backtrack()) return false;
                        continue;
                    }
                    const size_t at = insn.flag1 ? pos - length : pos;
                    const bool ic = (insn.mode_flags & kFlagIgnoreCase) != 0;
                    bool ok = true;
                    if (m_program.unicode_mode()) {
                        // In unicode mode, a backreference cannot split a surrogate pair in subject
                        // and must compare code point by code point (with Simple Case Folding).
                        if (splits_pair(at) || splits_pair(at + length)) {
                            ok = false;
                        } else {
                            size_t i = 0;
                            while (i < length) {
                                size_t w1 = 1, w2 = 1;
                                uint32_t c1 = code_point_at(start + i, w1);
                                uint32_t c2 = code_point_at(at + i, w2);
                                if (w1 != w2 || !same_char(c1, c2, ic)) {
                                    ok = false;
                                    break;
                                }
                                i += w1;
                            }
                        }
                    } else {
                        for (size_t i = 0; i < length; ++i) {
                            if (!same_char(m_subject[start + i], m_subject[at + i], ic)) {
                                ok = false;
                                break;
                            }
                        }
                    }
                    if (!ok) {
                        if (!backtrack()) return false;
                        continue;
                    }
                    pos = insn.flag1 ? pos - length : pos + length;
                    ++pc;
                    break;
                }

                case RegExpOp::BeginGroup:
                    m_trail.push_back({TrailKind::GroupStart, insn.a, m_group_starts[insn.a], 0});
                    m_group_starts[insn.a] = static_cast<long>(pos);
                    ++pc;
                    break;

                case RegExpOp::EndGroup: {
                    size_t slot = insn.a * 2;
                    long g_start = m_group_starts[insn.a];
                    long g_end = static_cast<long>(pos);
                    m_trail.push_back({TrailKind::Capture, slot, m_captures[slot], 0});
                    m_trail.push_back({TrailKind::Capture, slot + 1, m_captures[slot + 1], 0});
                    m_captures[slot] = std::min(g_start, g_end);
                    m_captures[slot + 1] = std::max(g_start, g_end);
                    ++pc;
                    break;
                }

                case RegExpOp::Jump:
                    pc = insn.a;
                    break;

                case RegExpOp::Split:
                    m_bt.push_back({BtKind::Choice, insn.b, pos, m_trail.size(), 0, 0, 0, false});
                    pc = insn.a;
                    break;

                case RegExpOp::RepeatEnter:
                    m_trail.push_back({TrailKind::RepCount, insn.a, 0, m_rep_counts[insn.a]});
                    m_rep_counts[insn.a] = 0;
                    ++pc;
                    break;

                case RegExpOp::RepeatStep: {
                    size_t count = m_rep_counts[insn.a];
                    if (count >= insn.max) {
                        pc = insn.b;
                    } else if (count < insn.min) {
                        ++pc;
                    } else if (insn.flag1) {
                        m_bt.push_back({BtKind::Choice, insn.b, pos, m_trail.size(), 0, 0, 0, false});
                        ++pc;
                    } else {
                        m_bt.push_back({BtKind::Choice, pc + 1, pos, m_trail.size(), 0, 0, 0, false});
                        pc = insn.b;
                    }
                    break;
                }

                case RegExpOp::RepeatBody:
                    m_trail.push_back({TrailKind::RepStart, insn.a, 0, m_rep_starts[insn.a]});
                    m_rep_starts[insn.a] = pos;
                    if (insn.last_group >= static_cast<int32_t>(insn.b)) {
                        for (int g = static_cast<int>(insn.b); g <= insn.last_group; ++g) {
                            size_t slot = static_cast<size_t>(g) * 2;
                            if (m_captures[slot] != -1) {
                                m_trail.push_back({TrailKind::Capture, slot, m_captures[slot], 0});
                                m_captures[slot] = -1;
                            }
                            if (m_captures[slot + 1] != -1) {
                                m_trail.push_back({TrailKind::Capture, slot + 1, m_captures[slot + 1], 0});
                                m_captures[slot + 1] = -1;
                            }
                        }
                    }
                    ++pc;
                    break;

                case RegExpOp::RepeatNext:
                    if (pos == m_rep_starts[insn.a] && m_rep_counts[insn.a] >= insn.min) {
                        if (!backtrack()) return false;
                        continue;
                    }
                    m_trail.push_back({TrailKind::RepCount, insn.a, 0, m_rep_counts[insn.a]});
                    ++m_rep_counts[insn.a];
                    pc = insn.b;
                    break;

                case RegExpOp::SimpleRepeat: {
                    if (!insn.flag2) {
                        size_t p = pos;
                        size_t count = 0;
                        size_t width = 1;
                        while (count < insn.min) {
                            if (!match_simple_atom(insn, p, width)) {
                                count = insn.min + 1; // signal failure
                                break;
                            }
                            p = insn.flag1 ? p - width : p + width;
                            ++count;
                            charge();
                        }
                        if (count > insn.min) {
                            if (!backtrack()) return false;
                            continue;
                        }
                        if (count < insn.max) {
                            m_bt.push_back({BtKind::SimpleLazy, pc + 1, p, m_trail.size(), pc, 0, count, insn.flag1});
                        }
                        pos = p;
                        ++pc;
                        break;
                    }

                    if (!m_program.unicode_mode()) {
                        size_t count = 0;
                        size_t width = 1;
                        size_t p = pos;
                        while (count < insn.max && match_simple_atom(insn, p, width)) {
                            p = insn.flag1 ? p - 1 : p + 1;
                            ++count;
                            charge();
                        }
                        if (count < insn.min) {
                            if (!backtrack()) return false;
                            continue;
                        }
                        if (count > insn.min) {
                            m_bt.push_back({BtKind::SimpleGreedyAscii, pc + 1, pos, m_trail.size(),
                                            pos, insn.min, count - 1, insn.flag1});
                        }
                        pos = p;
                        ++pc;
                        break;
                    }

                    std::vector<size_t> ends;
                    ends.push_back(pos);
                    size_t p = pos;
                    size_t width = 1;
                    while (ends.size() - 1 < insn.max && match_simple_atom(insn, p, width)) {
                        p = insn.flag1 ? p - width : p + width;
                        ends.push_back(p);
                        charge();
                    }
                    if (ends.size() - 1 < insn.min) {
                        if (!backtrack()) return false;
                        continue;
                    }
                    if (ends.size() - 1 > insn.min) {
                        const size_t pool_idx = m_ends_pool.size();
                        m_ends_pool.push_back(std::move(ends));
                        const auto& ref = m_ends_pool[pool_idx];
                        m_bt.push_back({BtKind::SimpleGreedyUnicode, pc + 1, 0, m_trail.size(),
                                        pool_idx, insn.min, ref.size() - 2, insn.flag1});
                        pos = ref.back();
                    } else {
                        pos = ends.back();
                    }
                    ++pc;
                    break;
                }

                case RegExpOp::LookAround: {
                    if (stack_is_exhausted()) {
                        js_throw_range_error("Regular expression too complex: recursion limit exceeded");
                    }
                    const size_t mark = m_trail.size();
                    const size_t sub_bt_base = m_bt.size();
                    size_t sub_pos = pos;
                    const bool found = exec_vm(insn.a, sub_pos, sub_bt_base);
                    if (insn.flag2) { // negate
                        unwind_trail(mark);
                        if (found) {
                            if (!backtrack()) return false;
                            continue;
                        }
                        ++pc;
                    } else {
                        if (!found) {
                            unwind_trail(mark);
                            if (!backtrack()) return false;
                            continue;
                        }
                        ++pc;
                    }
                    break;
                }
            }
        }
    }

    const Program& m_program;
    const Utf16View& m_subject;
    std::vector<long> m_captures;
    std::vector<long> m_group_starts;
    std::vector<size_t> m_rep_counts;
    std::vector<size_t> m_rep_starts;
    std::vector<BtFrame> m_bt;
    std::vector<TrailEntry> m_trail;
    std::vector<std::vector<size_t>> m_ends_pool;
    uint64_t m_steps{0};
};

// ---------------------------------------------------------------------------
// Compilation & RegExp objects
// ---------------------------------------------------------------------------

std::shared_ptr<Program> compile_raw(const std::string& pattern, const std::string& flags, bool emit_bytecode) {
    auto program = std::make_shared<Program>();
    program->source = pattern;
    for (char flag : flags) {
        bool* target = nullptr;
        switch (flag) {
            case 'g': target = &program->global; break;
            case 'i': target = &program->ignore_case; break;
            case 'm': target = &program->multiline; break;
            case 's': target = &program->dot_all; break;
            case 'u': target = &program->unicode; break;
            case 'v': target = &program->unicode_sets; break;
            case 'y': target = &program->sticky; break;
            case 'd': target = &program->has_indices; break;
            default: break;
        }
        if (!target || *target) {
            throw RegExpSyntaxError{"Invalid flags supplied to RegExp constructor '" + flags + "'"};
        }
        *target = true;
    }
    if (program->unicode && program->unicode_sets) {
        throw RegExpSyntaxError{"Invalid flags supplied to RegExp constructor '" + flags + "'"};
    }
    // Canonical flag order, as the `flags` accessor reports it.
    for (char flag : std::string("dgimsuvy")) {
        if (flags.find(flag) != std::string::npos) program->flags += flag;
    }
    std::u16string wide = js_utf8_to_utf16(pattern);
    PatternParser parser(wide, *program);
    parser.parse();
    if (emit_bytecode) {
        parser.finalize_bytecode();
    }
    return program;
}

std::shared_ptr<Program> compile(const std::string& pattern, const std::string& flags) {
    try {
        return compile_raw(pattern, flags, true);
    } catch (const RegExpSyntaxError& err) {
        js_throw_error(JsErrorKind::SyntaxError, err.message);
    } catch (const RegExpRangeError& err) {
        js_throw_range_error(err.message);
    }
}

bool has_regexp_slot(const JsValue& value) {
    return value.type == JsType::Object && value.object_val &&
           value.object_val->klass == ObjectClass::RegExp && value.object_val->internal;
}

std::shared_ptr<Program> program_of(const JsValue& value, const char* method) {
    if (!has_regexp_slot(value)) {
        js_throw_type_error(std::string("Method ") + method + " called on incompatible receiver " +
                            js_describe_thrown(value));
    }
    return std::static_pointer_cast<Program>(value.object_val->internal);
}

JsValue find_global(const JsIntrinsics& realm, const char* name) {
    for (const auto& [k, v] : realm.globals) {
        if (k == name) return v;
    }
    return JsValue::make_undefined();
}

JsValue make_regexp(std::shared_ptr<Program> program, JsObject* proto = nullptr,
                    const JsValue* derived_this = nullptr) {
    JsObject* cell = (derived_this && derived_this->object_val)
                   ? derived_this->object_val.get()
                   : Heap::default_heap().allocate_object();
    if (!derived_this || !derived_this->object_val) {
        cell->prototype = proto ? proto : JsIntrinsics::get().regexp_proto;
    }
    cell->klass = ObjectClass::RegExp;
    cell->internal = std::move(program);
    JsValue regex = JsValue::make_object(cell);
    regex.define_property("lastIndex", JsValue::make_number(0), PropertyMap::Writable);
    return regex;
}

struct MatchResult {
    bool matched{false};
    std::vector<long> captures;
};

/// Finds the first match at or after `from` (exactly at `from` when sticky).
MatchResult run_match(const Program& program, const Utf16View& subject, size_t from, bool sticky) {
    MatchResult result;
    if (from > subject.size()) return result;
    Matcher matcher(program, subject);

    for (size_t start = from; start <= subject.size(); ++start) {
        if (program.has_literal_start && !sticky) {
            size_t found = subject.find(static_cast<char16_t>(program.literal_start_ch), start);
            if (found == Utf16View::npos) return result;
            start = found;
        }
        if (start > from && matcher.splits_pair(start)) continue;
        if (matcher.match_at(start)) {
            result.matched = true;
            result.captures = matcher.captures();
            return result;
        }
        if (sticky) break;
    }
    return result;
}

JsString capture_string(const Utf16View& subject, const std::vector<long>& captures, size_t group) {
    long start = captures[group * 2];
    long end = captures[group * 2 + 1];
    if (start < 0) return JsString();
    return subject.slice(static_cast<size_t>(start), static_cast<size_t>(end));
}

JsValue build_match_array(const Program& program, const Utf16View& subject, const std::vector<long>& captures) {
    constexpr uint8_t kDataProp = PropertyMap::Writable | PropertyMap::Enumerable | PropertyMap::Configurable;
    std::vector<JsValue> items;
    items.reserve(static_cast<size_t>(program.group_count));
    for (int g = 0; g < program.group_count; ++g) {
        if (captures[static_cast<size_t>(g) * 2] < 0) items.push_back(JsValue::make_undefined());
        else items.push_back(JsValue::make_string(capture_string(subject, captures, static_cast<size_t>(g))));
    }
    JsValue array = JsValue::make_array(items);
    array.define_property("index", JsValue::make_number(static_cast<double>(captures[0])), kDataProp);
    array.define_property("input", JsValue::make_string(subject.string()), kDataProp);

    if (program.unique_names.empty()) {
        array.define_property("groups", JsValue::make_undefined(), kDataProp);
    } else {
        JsValue groups = JsValue::make_object();
        groups.object_val->prototype = nullptr;
        for (const auto& uname : program.unique_names) {
            JsValue val = JsValue::make_undefined();
            for (const auto& [name, index] : program.names) {
                if (name == uname && captures[index * 2] >= 0) {
                    val = items[index];
                    break;
                }
            }
            groups.define_property(uname, val, kDataProp);
        }
        array.define_property("groups", groups, kDataProp);
    }

    if (program.has_indices) {
        std::vector<JsValue> idx_items;
        idx_items.reserve(static_cast<size_t>(program.group_count));
        for (int g = 0; g < program.group_count; ++g) {
            long s = captures[static_cast<size_t>(g) * 2];
            long e = captures[static_cast<size_t>(g) * 2 + 1];
            if (s < 0) {
                idx_items.push_back(JsValue::make_undefined());
            } else {
                idx_items.push_back(JsValue::make_array({
                    JsValue::make_number(static_cast<double>(s)),
                    JsValue::make_number(static_cast<double>(e)),
                }));
            }
        }
        JsValue indices = JsValue::make_array(idx_items);
        if (program.unique_names.empty()) {
            indices.define_property("groups", JsValue::make_undefined(), kDataProp);
        } else {
            JsValue idx_groups = JsValue::make_object();
            idx_groups.object_val->prototype = nullptr;
            for (const auto& uname : program.unique_names) {
                JsValue pair = JsValue::make_undefined();
                for (const auto& [name, index] : program.names) {
                    if (name == uname && captures[index * 2] >= 0) {
                        pair = idx_items[index];
                        break;
                    }
                }
                idx_groups.define_property(uname, pair, kDataProp);
            }
            indices.define_property("groups", idx_groups, kDataProp);
        }
        array.define_property("indices", indices, kDataProp);
    }

    return array;
}

double to_length_double(const JsValue& v) {
    double n = js_to_integer(js_to_number(v));
    if (n <= 0.0 || std::isnan(n)) return 0.0;
    constexpr double kMaxSafe = 9007199254740991.0; // 2^53 - 1
    return n > kMaxSafe ? kMaxSafe : n;
}

void set_last_index_strict(JsValue regex, const JsValue& value) {
    // ECMA-262 Set(R, "lastIndex", value, true): throws TypeError if non-writable
    // or a getter-only accessor (own or inherited), or non-extensible.
    JsObject* cell = regex.property_cell();
    if (cell) {
        bool found_own = false;
        for (JsObject* cur = cell; cur; cur = cur->prototype) {
            auto it = cur->properties.find("lastIndex");
            if (it != cur->properties.end()) {
                if (it->flags & PropertyMap::Accessor) {
                    if (!it->setter) {
                        js_throw_type_error("Cannot set property 'lastIndex' which has only a getter");
                    }
                } else if (!(it->flags & PropertyMap::Writable)) {
                    js_throw_type_error("Cannot assign to read only property 'lastIndex'");
                }
                found_own = (cur == cell);
                break;
            }
        }
        if (!found_own && !cell->extensible) {
            js_throw_type_error("Cannot add property 'lastIndex', object is not extensible");
        }
    }
    regex.set_property("lastIndex", value);
}

void set_last_index_strict(JsValue regex, double value) {
    set_last_index_strict(std::move(regex), JsValue::make_number(value));
}

/// RegExp.prototype.exec: honours and updates lastIndex for global/sticky.
bool exec_raw(const JsValue& regex, const Program& program, const Utf16View& subject, MatchResult& out) {
    const bool uses_last_index = program.global || program.sticky;
    double raw_last = to_length_double(regex.get_property("lastIndex"));
    if (!uses_last_index) raw_last = 0.0;
    if (raw_last > static_cast<double>(subject.size())) {
        if (uses_last_index) set_last_index_strict(regex, 0.0);
        return false;
    }
    size_t from = static_cast<size_t>(raw_last);
    // In unicode / unicodeSets mode, a lastIndex pointing at the second half
    // of a surrogate pair starts at the whole code point.
    if (program.unicode_mode() && from > 0 && from < subject.size() &&
        subject[from] >= 0xDC00 && subject[from] <= 0xDFFF &&
        subject[from - 1] >= 0xD800 && subject[from - 1] <= 0xDBFF) {
        --from;
    }
    out = run_match(program, subject, from, program.sticky);
    if (!out.matched) {
        if (uses_last_index) set_last_index_strict(regex, 0.0);
        return false;
    }
    if (uses_last_index) set_last_index_strict(regex, static_cast<double>(out.captures[1]));
    return true;
}

size_t advance_string_index(const Utf16View& subject, size_t index, bool full_unicode) {
    if (!full_unicode || index + 1 >= subject.size()) return index + 1;
    if (subject[index] >= 0xD800 && subject[index] <= 0xDBFF &&
        subject[index + 1] >= 0xDC00 && subject[index + 1] <= 0xDFFF) {
        return index + 2;
    }
    return index + 1;
}

/// ECMA-262 §22.2.5.2.1 RegExpExec(R, S): calls custom R.exec if callable, else builtin exec.
JsValue regexp_exec_abstract(const JsValue& rx, const JsString& subject_string) {
    JsValue exec = rx.get_property("exec");
    if (exec.is_function()) {
        JsValue res = exec.call_with_this(rx, {JsValue::make_string(subject_string)});
        if (res.type != JsType::Null && !res.is_object_like()) {
            js_throw_type_error("RegExp exec method returned something other than an Object or null");
        }
        return res;
    }
    auto program = program_of(rx, "RegExp.prototype.exec");
    const Utf16View subject(subject_string);
    MatchResult result;
    if (!exec_raw(rx, *program, subject, result)) return JsValue::make_null();
    return build_match_array(*program, subject, result.captures);
}

JsValue species_constructor(const JsValue& obj, const JsValue& default_ctor) {
    JsValue c = obj.get_property("constructor");
    if (c.is_undefined()) return default_ctor;
    if (!c.is_object_like()) {
        js_throw_type_error("constructor property is not an object");
    }
    JsValue s = c.get_property(kSymbolSpeciesKey);
    if (s.is_nullish()) return default_ctor;
    if (!s.is_function()) js_throw_type_error("Symbol.species is not a constructor");
    return s;
}

std::string escape_source(const std::string& source) {
    if (source.empty()) return "(?:)";
    std::string out;
    bool in_class = false;
    for (size_t i = 0; i < source.size(); ++i) {
        unsigned char uc = static_cast<unsigned char>(source[i]);
        char c = source[i];
        if (c == '\\') {
            out += c;
            if (i + 1 < source.size()) {
                char next = source[++i];
                if (next == '\n') { out += 'n'; continue; }
                if (next == '\r') { out += 'r'; continue; }
                // UTF-8 U+2028 (E2 80 A8) and U+2029 (E2 80 A9)
                if (static_cast<unsigned char>(next) == 0xE2 && i + 2 < source.size() &&
                    static_cast<unsigned char>(source[i + 1]) == 0x80 &&
                    (static_cast<unsigned char>(source[i + 2]) == 0xA8 ||
                     static_cast<unsigned char>(source[i + 2]) == 0xA9)) {
                    out += (static_cast<unsigned char>(source[i + 2]) == 0xA8) ? "u2028" : "u2029";
                    i += 2;
                    continue;
                }
                out += next;
            }
            continue;
        }
        if (c == '[') in_class = true;
        else if (c == ']') in_class = false;
        if (c == '/' && !in_class) { out += "\\/"; continue; }
        if (c == '\n') { out += "\\n"; continue; }
        if (c == '\r') { out += "\\r"; continue; }
        if (uc == 0xE2 && i + 2 < source.size() &&
            static_cast<unsigned char>(source[i + 1]) == 0x80 &&
            (static_cast<unsigned char>(source[i + 2]) == 0xA8 ||
             static_cast<unsigned char>(source[i + 2]) == 0xA9)) {
            out += (static_cast<unsigned char>(source[i + 2]) == 0xA8) ? "\\u2028" : "\\u2029";
            i += 2;
            continue;
        }
        out += c;
    }
    return out;
}

/// ECMA-262 ES2025 `RegExp.escape(S)` / `EncodeForRegExpEscape`.
std::string regexp_escape_string(const JsString& input) {
    const Utf16View view(input);
    std::u16string out;
    out.reserve(view.size() * 2);

    auto append_hex2 = [&](uint32_t cp) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\x%02x", cp & 0xFF);
        for (const char* p = buf; *p; ++p) out.push_back(static_cast<char16_t>(*p));
    };
    auto append_hex4 = [&](uint32_t cp) {
        char buf[10];
        std::snprintf(buf, sizeof(buf), "\\u%04x", cp & 0xFFFF);
        for (const char* p = buf; *p; ++p) out.push_back(static_cast<char16_t>(*p));
    };
    auto is_ascii_alnum = [](uint32_t c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    };
    auto is_other_punctuator = [](uint32_t c) {
        switch (c) {
            case ',': case '-': case '=': case '<': case '>': case '#': case '&': case '!':
            case '%': case ':': case ';': case '@': case '~': case '\'': case '`': case '"':
                return true;
            default:
                return false;
        }
    };
    auto is_unicode_ws_or_lt = [](uint32_t c) {
        return c == 0x20 || c == 0xA0 || c == 0x1680 ||
               (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 ||
               c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
    };

    for (size_t i = 0; i < view.size(); ) {
        uint32_t cp = view[i];
        size_t w = 1;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < view.size() &&
            view[i + 1] >= 0xDC00 && view[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (view[i + 1] - 0xDC00);
            w = 2;
        }
        if (i == 0 && is_ascii_alnum(cp)) {
            append_hex2(cp);
        } else if (is_syntax_char(cp) || cp == '/') {
            out.push_back(u'\\');
            out.push_back(static_cast<char16_t>(cp));
        } else if (cp == '\t') {
            out.push_back(u'\\'); out.push_back(u't');
        } else if (cp == '\n') {
            out.push_back(u'\\'); out.push_back(u'n');
        } else if (cp == '\v') {
            out.push_back(u'\\'); out.push_back(u'v');
        } else if (cp == '\f') {
            out.push_back(u'\\'); out.push_back(u'f');
        } else if (cp == '\r') {
            out.push_back(u'\\'); out.push_back(u'r');
        } else if (is_other_punctuator(cp)) {
            append_hex2(cp);
        } else if (is_unicode_ws_or_lt(cp) || (cp >= 0xD800 && cp <= 0xDFFF)) {
            if (cp <= 0xFF) append_hex2(cp);
            else append_hex4(cp);
        } else {
            out.push_back(view[i]);
            if (w == 2) out.push_back(view[i + 1]);
        }
        i += w;
    }
    return js_utf16_to_utf8(out);
}

JsValue make_non_constructor_native(const char* name, int length, JsValue::NativeFn fn) {
    JsValue v = js_make_native(name, length, std::move(fn));
    if (JsObject* cell = v.function_cell()) cell->not_constructor = true;
    return v;
}

void define_named_getter(JsObject* obj, const char* key, const char* fn_name, JsValue::NativeFn getter) {
    auto& entry = obj->properties.entry(key);
    entry.second = make_non_constructor_native(fn_name, 0, std::move(getter));
    entry.setter.reset();
    entry.flags = PropertyMap::Configurable | PropertyMap::Accessor;
    obj->has_accessors = true;
}

void define_symbol_method(JsObject* obj, const char* key, const char* fn_name, int length, JsValue::NativeFn fn) {
    auto& entry = obj->properties.entry(key);
    entry.second = make_non_constructor_native(fn_name, length, std::move(fn));
    entry.setter.reset();
    entry.flags = PropertyMap::Writable | PropertyMap::Configurable;
}

void define_regexp_method(JsObject* obj, const char* name, int length, JsValue::NativeFn fn) {
    define_symbol_method(obj, name, name, length, std::move(fn));
}

/// Generic `RegExp.prototype[Symbol.match](string)` per ECMA-262 §22.2.6.8.
JsValue regexp_symbol_match(const JsValue& rx, const JsString& subject_string) {
    if (rx.type != JsType::Object && !rx.is_function() && !rx.is_array()) {
        js_throw_type_error("RegExp.prototype[Symbol.match] called on non-object");
    }
    std::string flags = js_to_string(rx.get_property("flags"));
    const bool global = flags.find('g') != std::string::npos;
    if (!global) {
        return regexp_exec_abstract(rx, subject_string);
    }
    const bool full_unicode = flags.find('u') != std::string::npos || flags.find('v') != std::string::npos;
    set_last_index_strict(rx, 0.0);
    const Utf16View subject(subject_string);
    std::vector<JsValue> matches;
    while (true) {
        JsValue result = regexp_exec_abstract(rx, subject_string);
        if (result.type == JsType::Null) {
            return matches.empty() ? JsValue::make_null() : JsValue::make_array(matches);
        }
        JsString match_str = js_to_js_string(result.get_property("0"));
        matches.push_back(JsValue::make_string(match_str));
        if (match_str.empty()) {
            double this_idx = to_length_double(rx.get_property("lastIndex"));
            size_t next_idx = advance_string_index(subject, static_cast<size_t>(std::min(this_idx, static_cast<double>(subject.size()))), full_unicode);
            if (this_idx > static_cast<double>(subject.size())) {
                set_last_index_strict(rx, this_idx + 1.0);
            } else {
                set_last_index_strict(rx, static_cast<double>(next_idx));
            }
        }
    }
}

/// Generic `RegExp.prototype[Symbol.matchAll](string)` per ECMA-262 §22.2.6.9.
JsValue regexp_symbol_match_all(const JsValue& rx, const JsString& subject_string) {
    if (rx.type != JsType::Object && !rx.is_function() && !rx.is_array()) {
        js_throw_type_error("RegExp.prototype[Symbol.matchAll] called on non-object");
    }
    JsValue default_ctor = find_global(JsIntrinsics::get(), "RegExp");
    JsValue ctor = species_constructor(rx, default_ctor);
    std::string flags = js_to_string(rx.get_property("flags"));
    JsValue matcher = js_construct(ctor, {rx, JsValue::make_string(flags)});
    double last_idx = to_length_double(rx.get_property("lastIndex"));
    set_last_index_strict(matcher, last_idx);
    const bool global = flags.find('g') != std::string::npos;
    const bool full_unicode = flags.find('u') != std::string::npos || flags.find('v') != std::string::npos;

    auto subject = std::make_shared<const Utf16View>(subject_string);
    auto done = std::make_shared<bool>(false);
    return js_make_iterator([matcher, subject, done, global, full_unicode](JsValue& value) {
        if (*done) return false;
        JsValue match = regexp_exec_abstract(matcher, subject->string());
        if (match.type == JsType::Null) {
            *done = true;
            return false;
        }
        if (!global) {
            *done = true;
            value = match;
            return true;
        }
        JsString match_str = js_to_js_string(match.get_property("0"));
        if (match_str.empty()) {
            double this_idx = to_length_double(matcher.get_property("lastIndex"));
            if (this_idx > static_cast<double>(subject->size())) {
                set_last_index_strict(matcher, this_idx + 1.0);
            } else {
                size_t next_idx = advance_string_index(*subject, static_cast<size_t>(this_idx), full_unicode);
                set_last_index_strict(matcher, static_cast<double>(next_idx));
            }
        }
        value = match;
        return true;
    }, {matcher}, JsIntrinsics::get().regexp_string_iterator_proto);
}

/// Generic `RegExp.prototype[Symbol.search](string)` per ECMA-262 §22.2.6.11.
JsValue regexp_symbol_search(const JsValue& rx, const JsString& subject_string) {
    if (rx.type != JsType::Object && !rx.is_function() && !rx.is_array()) {
        js_throw_type_error("RegExp.prototype[Symbol.search] called on non-object");
    }
    JsValue prev_last = rx.get_property("lastIndex");
    if (!js_same_value(prev_last, JsValue::make_number(0))) {
        set_last_index_strict(rx, 0.0);
    }
    JsValue result = regexp_exec_abstract(rx, subject_string);
    JsValue current_last = rx.get_property("lastIndex");
    if (!js_same_value(current_last, prev_last)) {
        // Restore previous lastIndex per spec.
        set_last_index_strict(rx, prev_last);
    }
    if (result.type == JsType::Null) return JsValue::make_number(-1);
    return result.get_property("index");
}

/// Generic `RegExp.prototype[Symbol.replace](string, replaceValue)` per ECMA-262 §22.2.6.10.
JsValue regexp_symbol_replace(const JsValue& rx, const JsString& subject_string, const JsValue& replace_value) {
    if (rx.type != JsType::Object && !rx.is_function() && !rx.is_array()) {
        js_throw_type_error("RegExp.prototype[Symbol.replace] called on non-object");
    }
    const bool functional = replace_value.is_function();
    const std::u16string repl_template = functional ? std::u16string() : Utf16View(js_to_js_string(replace_value)).copy();

    std::string flags = js_to_string(rx.get_property("flags"));
    const bool global = flags.find('g') != std::string::npos;
    const bool full_unicode = flags.find('u') != std::string::npos || flags.find('v') != std::string::npos;
    if (global) set_last_index_strict(rx, 0.0);

    const Utf16View subject(subject_string);
    std::vector<JsValue> results;
    while (true) {
        JsValue res = regexp_exec_abstract(rx, subject_string);
        if (res.type == JsType::Null) break;
        results.push_back(res);
        if (!global) break;
        JsString match_str = js_to_js_string(res.get_property("0"));
        if (match_str.empty()) {
            double this_idx = to_length_double(rx.get_property("lastIndex"));
            if (this_idx > static_cast<double>(subject.size())) {
                set_last_index_strict(rx, this_idx + 1.0);
            } else {
                size_t next_idx = advance_string_index(subject, static_cast<size_t>(this_idx), full_unicode);
                set_last_index_strict(rx, static_cast<double>(next_idx));
            }
        }
    }

    if (results.empty()) return JsValue::make_string(subject_string);

    std::u16string out;
    size_t next_source_pos = 0;
    for (const auto& res : results) {
        double raw_len = to_length_double(res.get_property("length"));
        size_t n_captures = raw_len > 1.0 ? static_cast<size_t>(std::min(raw_len - 1.0, 100000.0)) : 0;
        JsString matched_js = js_to_js_string(res.get_property("0"));
        const Utf16View matched_view(matched_js);
        double raw_pos = js_to_integer(js_to_number(res.get_property("index")));
        size_t position = raw_pos <= 0.0 ? 0 : static_cast<size_t>(std::min(raw_pos, static_cast<double>(subject.size())));

        std::vector<JsReplaceCapture> captures(n_captures + 1);
        captures[0].matched = true;
        captures[0].text = matched_view.copy();
        std::vector<JsValue> fn_args;
        if (functional) fn_args.push_back(JsValue::make_string(matched_js));
        for (size_t k = 1; k <= n_captures; ++k) {
            JsValue cap_val = res.get_property(std::to_string(k));
            if (cap_val.is_undefined()) {
                captures[k].matched = false;
                if (functional) fn_args.push_back(JsValue::make_undefined());
            } else {
                JsString cap_str = js_to_js_string(cap_val);
                captures[k].matched = true;
                captures[k].text = Utf16View(cap_str).copy();
                if (functional) fn_args.push_back(JsValue::make_string(cap_str));
            }
        }

        JsValue named_groups = res.get_property("groups");
        std::u16string replacement;
        if (functional) {
            fn_args.push_back(JsValue::make_number(static_cast<double>(position)));
            fn_args.push_back(JsValue::make_string(subject_string));
            if (!named_groups.is_undefined()) fn_args.push_back(named_groups);
            replacement = Utf16View(js_to_js_string(replace_value.call(fn_args))).copy();
        } else {
            // Resolve named capture groups dynamically per ECMA-262 GetSubstitution
            // (supports inherited properties on subclass groups objects and boxed primitives).
            std::vector<std::pair<std::string, size_t>> named_pairs;
            if (!named_groups.is_undefined()) {
                if (named_groups.type == JsType::Null) {
                    js_throw_type_error("Cannot convert null to object");
                }
                // Walk template to collect referenced $<name> keys in order.
                for (size_t ti = 0; ti + 2 < repl_template.size(); ++ti) {
                    if (repl_template[ti] != u'$') continue;
                    char16_t next = repl_template[ti + 1];
                    if (next == u'$') { ++ti; continue; }
                    if (next == u'<') {
                        size_t close = repl_template.find(u'>', ti + 2);
                        if (close != std::u16string::npos) {
                            std::string gname = js_utf16_to_utf8(repl_template.substr(ti + 2, close - (ti + 2)));
                            bool already = false;
                            for (const auto& p : named_pairs) {
                                if (p.first == gname) { already = true; break; }
                            }
                            if (!already) {
                                JsValue gv = named_groups.get_property(gname);
                                size_t synthetic_idx = captures.size();
                                if (!gv.is_undefined()) {
                                    captures.push_back({true, Utf16View(js_to_js_string(gv)).copy()});
                                } else {
                                    captures.push_back({false, {}});
                                }
                                named_pairs.emplace_back(std::move(gname), synthetic_idx);
                            }
                            ti = close;
                        }
                    }
                }
            }
            replacement = js_expand_replacement(repl_template, subject, position, captures, named_pairs);
        }

        if (position >= next_source_pos) {
            subject.append_to(out, next_source_pos, position - next_source_pos);
            out += replacement;
            next_source_pos = position + matched_view.size();
        }
    }
    if (next_source_pos < subject.size()) {
        subject.append_to(out, next_source_pos);
    }
    return JsValue::make_string(JsString::from_utf16(out));
}

/// Generic `RegExp.prototype[Symbol.split](string, limit)` per ECMA-262 §22.2.6.13.
JsValue regexp_symbol_split(const JsValue& rx, const JsString& subject_string, const JsValue& limit_val) {
    if (rx.type != JsType::Object && !rx.is_function() && !rx.is_array()) {
        js_throw_type_error("RegExp.prototype[Symbol.split] called on non-object");
    }
    JsValue default_ctor = find_global(JsIntrinsics::get(), "RegExp");
    JsValue ctor = species_constructor(rx, default_ctor);
    std::string flags = js_to_string(rx.get_property("flags"));
    const bool full_unicode = flags.find('u') != std::string::npos || flags.find('v') != std::string::npos;
    std::string new_flags = flags;
    if (new_flags.find('y') == std::string::npos) new_flags += 'y';

    JsValue splitter = js_construct(ctor, {rx, JsValue::make_string(new_flags)});
    uint32_t lim = limit_val.is_undefined() ? 0xFFFFFFFFu : js_to_uint32(js_to_number(limit_val));

    std::vector<JsValue> parts;
    if (lim == 0) return JsValue::make_array(parts);

    const Utf16View subject(subject_string);
    if (subject.empty()) {
        JsValue z = regexp_exec_abstract(splitter, subject_string);
        if (z.type != JsType::Null) return JsValue::make_array(parts);
        parts.push_back(JsValue::make_string(subject_string));
        return JsValue::make_array(parts);
    }

    size_t p = 0;
    size_t q = 0;
    while (q < subject.size()) {
        set_last_index_strict(splitter, static_cast<double>(q));
        JsValue z = regexp_exec_abstract(splitter, subject_string);
        if (z.type == JsType::Null) {
            q = advance_string_index(subject, q, full_unicode);
            continue;
        }
        double e_raw = to_length_double(splitter.get_property("lastIndex"));
        size_t e = static_cast<size_t>(std::min(e_raw, static_cast<double>(subject.size())));
        if (e == p) {
            q = advance_string_index(subject, q, full_unicode);
            continue;
        }
        parts.push_back(JsValue::make_string(subject.slice(p, q)));
        if (parts.size() >= lim) return JsValue::make_array(parts);
        p = e;
        double z_len = to_length_double(z.get_property("length"));
        size_t n_caps = z_len > 1.0 ? static_cast<size_t>(std::min(z_len - 1.0, 100000.0)) : 0;
        for (size_t i = 1; i <= n_caps; ++i) {
            parts.push_back(z.get_property(std::to_string(i)));
            if (parts.size() >= lim) return JsValue::make_array(parts);
        }
        q = p;
    }
    parts.push_back(JsValue::make_string(subject.slice(p, subject.size())));
    return JsValue::make_array(parts);
}

} // namespace

// ---------------------------------------------------------------------------
// Entry points used by JsParser and String.prototype
// ---------------------------------------------------------------------------

bool js_is_regexp(const JsValue& value) {
    if (value.type != JsType::Object && !value.is_function() && !value.is_array()) return false;
    JsValue matcher = value.get_property(kSymbolMatchKey);
    if (!matcher.is_undefined()) return matcher.is_truthy();
    return has_regexp_slot(value);
}

bool js_regexp_validate(const std::string& pattern, const std::string& flags, std::string& error_out) {
    try {
        (void)compile_raw(pattern, flags, false);
        return true;
    } catch (const RegExpSyntaxError& err) {
        error_out = err.message;
        return false;
    } catch (const RegExpRangeError& err) {
        error_out = err.message;
        return false;
    }
}

JsValue js_regexp_create(const std::string& pattern, const std::string& flags) {
    return make_regexp(compile(pattern, flags));
}

JsValue js_regexp_match(const JsValue& regex, const JsString& subject_string) {
    return regexp_symbol_match(regex, subject_string);
}

JsValue js_regexp_match_all(const JsValue& regex, const JsString& subject_string) {
    return regexp_symbol_match_all(regex, subject_string);
}

double js_regexp_search(const JsValue& regex, const JsString& subject_string) {
    return js_to_number(regexp_symbol_search(regex, subject_string));
}

JsValue js_regexp_replace(const JsValue& regex, const JsString& subject_string, const JsValue& replacement) {
    return regexp_symbol_replace(regex, subject_string, replacement);
}

JsValue js_regexp_split(const JsValue& regex, const JsString& subject_string, double limit) {
    JsValue lim_val = (limit == 4294967295.0) ? JsValue::make_undefined() : JsValue::make_number(limit);
    return regexp_symbol_split(regex, subject_string, lim_val);
}

// ---------------------------------------------------------------------------
// The RegExp object & well-known RegExp symbols
// ---------------------------------------------------------------------------

void install_regexp_builtins(JsIntrinsics& realm) {
    JsObject* rp = realm.regexp_proto;

    // Install well-known RegExp & species symbols on global Symbol object.
    for (auto& [gname, gval] : realm.globals) {
        if (gname == "Symbol") {
            for (const auto& [name, key] : {
                    std::pair<const char*, const char*>{"match", kSymbolMatchKey},
                    {"matchAll", kSymbolMatchAllKey},
                    {"replace", kSymbolReplaceKey},
                    {"search", kSymbolSearchKey},
                    {"split", kSymbolSplitKey},
                    {"species", kSymbolSpeciesKey}}) {
                gval.define_property(name, js_symbol_for_key(key, std::string("Symbol.") + name), 0);
            }
            break;
        }
    }

    JsValue regexp_ctor = js_make_native("RegExp", 2, [](Args args) {
        const JsValue& pattern = js_arg(args, 0);
        const JsValue& flags = js_arg(args, 1);
        const bool is_construct = JsValue::current_is_construct();
        const JsValue* new_target_ptr = JsValue::current_new_target();
        const JsValue* derived_ptr = is_construct ? JsValue::current_derived_this() : nullptr;
        JsValue derived_holder;
        if (derived_ptr) derived_holder = *derived_ptr;
        JsValue nt_holder;
        if (new_target_ptr) nt_holder = *new_target_ptr;

        const bool pattern_is_regexp = js_is_regexp(pattern);
        if (!is_construct) {
            JsValue self_ctor = find_global(JsIntrinsics::get(), "RegExp");
            if (pattern_is_regexp && flags.is_undefined()) {
                JsValue pattern_ctor = pattern.get_property("constructor");
                if (js_same_value(self_ctor, pattern_ctor)) {
                    return pattern;
                }
            }
        }

        JsObject* proto = JsIntrinsics::get().regexp_proto;
        if (!derived_ptr && new_target_ptr && nt_holder.is_object_like()) {
            JsValue nt_proto = nt_holder.get_property("prototype");
            if (nt_proto.is_object() && nt_proto.object_val) {
                proto = nt_proto.object_val;
            }
        }

        std::string p_str;
        std::string f_str;
        if (has_regexp_slot(pattern)) {
            auto program = program_of(pattern, "RegExp");
            p_str = program->source;
            f_str = flags.is_undefined() ? program->flags : js_to_string(flags);
        } else if (pattern_is_regexp) {
            JsValue p_val = pattern.get_property("source");
            JsValue f_val = flags.is_undefined() ? pattern.get_property("flags") : flags;
            p_str = p_val.is_undefined() ? std::string() : js_to_string(p_val);
            f_str = f_val.is_undefined() ? std::string() : js_to_string(f_val);
        } else {
            p_str = pattern.is_undefined() ? std::string() : js_to_string(pattern);
            f_str = flags.is_undefined() ? std::string() : js_to_string(flags);
        }
        return make_regexp(compile(p_str, f_str), proto, derived_ptr ? &derived_holder : nullptr);
    });
    regexp_ctor.define_property("prototype", js_object_value(rp), 0);
    js_define_hidden(rp, "constructor", regexp_ctor);

    // RegExp[Symbol.species]
    regexp_ctor.define_accessor(
        kSymbolSpeciesKey,
        make_non_constructor_native("get [Symbol.species]", 0, [](Args) { return self(); }),
        JsValue::make_undefined(),
        PropertyMap::Configurable);

    // ES2025 RegExp.escape(S)
    regexp_ctor.define_property(
        "escape",
        make_non_constructor_native("escape", 1, [](Args args) {
            const JsValue& arg = js_arg(args, 0);
            if (arg.type != JsType::String) {
                js_throw_type_error("RegExp.escape requires a string argument");
            }
            return JsValue::make_string(regexp_escape_string(arg.string_val));
        }),
        PropertyMap::Writable | PropertyMap::Configurable);

    realm.add_global("RegExp", regexp_ctor);

    define_regexp_method(rp, "exec", 1, [](Args args) {
        JsValue regex = self();
        auto program = program_of(regex, "RegExp.prototype.exec");
        const Utf16View subject(js_to_js_string(js_arg(args, 0)));
        MatchResult result;
        if (!exec_raw(regex, *program, subject, result)) return JsValue::make_null();
        return build_match_array(*program, subject, result.captures);
    });
    define_regexp_method(rp, "test", 1, [](Args args) {
        JsValue regex = self();
        if (!regex.is_object_like()) {
            js_throw_type_error("RegExp.prototype.test called on non-object");
        }
        JsString subject = js_to_js_string(js_arg(args, 0));
        JsValue match = regexp_exec_abstract(regex, subject);
        return JsValue::make_boolean(match.type != JsType::Null);
    });
    define_regexp_method(rp, "toString", 0, [](Args) {
        JsValue me = self();
        if (me.type != JsType::Object && !me.is_function() && !me.is_array()) {
            js_throw_type_error("RegExp.prototype.toString requires that 'this' be an Object");
        }
        std::string pattern = js_to_string(me.get_property("source"));
        std::string flags = js_to_string(me.get_property("flags"));
        return JsValue::make_string("/" + pattern + "/" + flags);
    });
    define_regexp_method(rp, "compile", 2, [](Args args) {
        JsValue me = self();
        auto prev = program_of(me, "RegExp.prototype.compile");
        const JsValue& p = js_arg(args, 0);
        const JsValue& f = js_arg(args, 1);
        std::shared_ptr<Program> next;
        if (has_regexp_slot(p)) {
            if (!f.is_undefined()) js_throw_type_error("Cannot supply flags when constructing one RegExp from another");
            next = program_of(p, "RegExp.prototype.compile");
        } else {
            next = compile(p.is_undefined() ? std::string() : js_to_string(p),
                           f.is_undefined() ? std::string() : js_to_string(f));
        }
        me.object_val->internal = std::move(next);
        set_last_index_strict(me, 0.0);
        return me;
    });

    // Well-known symbol methods on RegExp.prototype
    define_symbol_method(rp, kSymbolMatchKey, "[Symbol.match]", 1, [](Args args) {
        return regexp_symbol_match(self(), js_to_js_string(js_arg(args, 0)));
    });
    define_symbol_method(rp, kSymbolMatchAllKey, "[Symbol.matchAll]", 1, [](Args args) {
        return regexp_symbol_match_all(self(), js_to_js_string(js_arg(args, 0)));
    });
    define_symbol_method(rp, kSymbolReplaceKey, "[Symbol.replace]", 2, [](Args args) {
        return regexp_symbol_replace(self(), js_to_js_string(js_arg(args, 0)), js_arg(args, 1));
    });
    define_symbol_method(rp, kSymbolSearchKey, "[Symbol.search]", 1, [](Args args) {
        return regexp_symbol_search(self(), js_to_js_string(js_arg(args, 0)));
    });
    define_symbol_method(rp, kSymbolSplitKey, "[Symbol.split]", 2, [](Args args) {
        return regexp_symbol_split(self(), js_to_js_string(js_arg(args, 0)), js_arg(args, 1));
    });

    // RegExp.prototype.source
    define_named_getter(rp, "source", "get source", [](Args) {
        JsValue me = self();
        if (me.type != JsType::Object && !me.is_function() && !me.is_array()) {
            js_throw_type_error("get RegExp.prototype.source called on non-object");
        }
        if (!has_regexp_slot(me)) {
            if (me.object_val == JsIntrinsics::get().regexp_proto) {
                return JsValue::make_string("(?:)");
            }
            js_throw_type_error("get RegExp.prototype.source called on incompatible receiver");
        }
        return JsValue::make_string(escape_source(program_of(me, "source")->source));
    });

    // RegExp.prototype.flags (generic per ECMA-262 §22.2.6.4, canonical order `dgimsuvy`)
    define_named_getter(rp, "flags", "get flags", [](Args) {
        JsValue me = self();
        if (me.type != JsType::Object && !me.is_function() && !me.is_array()) {
            js_throw_type_error("get RegExp.prototype.flags called on non-object");
        }
        std::string out;
        if (me.get_property("hasIndices").is_truthy())  out += 'd';
        if (me.get_property("global").is_truthy())      out += 'g';
        if (me.get_property("ignoreCase").is_truthy())  out += 'i';
        if (me.get_property("multiline").is_truthy())   out += 'm';
        if (me.get_property("dotAll").is_truthy())      out += 's';
        if (me.get_property("unicode").is_truthy())     out += 'u';
        if (me.get_property("unicodeSets").is_truthy()) out += 'v';
        if (me.get_property("sticky").is_truthy())      out += 'y';
        return JsValue::make_string(out);
    });

    struct FlagGetter { const char* name; const char* fn_name; bool Program::*member; };
    static const FlagGetter flag_getters[] = {
        {"global",      "get global",      &Program::global},
        {"ignoreCase",  "get ignoreCase",  &Program::ignore_case},
        {"multiline",   "get multiline",   &Program::multiline},
        {"dotAll",      "get dotAll",      &Program::dot_all},
        {"unicode",     "get unicode",     &Program::unicode},
        {"unicodeSets", "get unicodeSets", &Program::unicode_sets},
        {"sticky",      "get sticky",      &Program::sticky},
        {"hasIndices",  "get hasIndices",  &Program::has_indices},
    };
    for (const auto& getter : flag_getters) {
        bool Program::*member = getter.member;
        define_named_getter(rp, getter.name, getter.fn_name, [member](Args) {
            JsValue me = self();
            if (me.type != JsType::Object && !me.is_function() && !me.is_array()) {
                js_throw_type_error("RegExp flag getter called on non-object");
            }
            if (!has_regexp_slot(me)) {
                if (me.object_val == JsIntrinsics::get().regexp_proto) {
                    return JsValue::make_undefined();
                }
                js_throw_type_error("RegExp flag getter called on incompatible receiver");
            }
            return JsValue::make_boolean((*program_of(me, "flag")).*member);
        });
    }
}

} // namespace litebird
