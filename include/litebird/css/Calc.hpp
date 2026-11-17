// A typed evaluator and canonical serializer for CSS math functions
// (calc(), min(), max(), clamp(), abs(), sign(), sibling-index() and
// sibling-count()), CSS Values and Units Level 4 and 5.
//
// The rest of the engine decides what to *do* with a calc(); this header
// only parses one, evaluates it (given the few facts a calculation may need
// that no property grammar carries — the font size, the query container's
// inline size, and where an element sits among its siblings) and writes one
// back the way the specification says it serializes.
//
// Every entry point is total: a calc that cannot be parsed or resolved is
// reported as a failure, and callers keep the text as it was written.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace litebird {

// What a calculation may not know by itself. `query_inline_px`, `sibling_index`
// and `sibling_count` of 0/-1 mean "not available"; container units and the
// sibling functions cannot be resolved without them.
struct CalcContext {
    double em = 16.0;
    double rem = 16.0;
    double query_inline_px = -1.0;   // the inline size of the query container, or -1
    double sibling_index = 0.0;      // 1-based position among element siblings, or 0
    double sibling_count = 0.0;      // number of element siblings, or 0
};

struct CalcNode {
    enum Kind {
        Number,     // a plain number
        Dimension,  // a magnitude and a unit; unit "%" is a percentage
        Sum,        // a + b - c (variadic, terms carry their signs)
        Product,    // a * b / c (variadic, divisors wrapped in Invert)
        Negate,     // negation of a non-constant operand
        Invert,     // 1 / operand (a non-constant divisor)
        Function,   // calc, sign, abs, sibling-index, sibling-count, min, max, clamp, ...
    } kind = Number;
    double number = 0.0;
    std::string unit;                    // Dimension only
    std::string function;                // Function only, lowercased
    std::vector<CalcNode> children;
};

struct CalcTriple {
    double px = 0.0;        // the length part, resolved against fonts and containers
    double percent = 0.0;   // the percentage part, still symbolic
    double number = 0.0;    // valid when !dimension
    bool dimension = false; // true when the result is a length-percentage
};

inline std::string calc_num_text(double value) {
    if (value == std::floor(value) && std::fabs(value) < 1e15) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.0f", value);
        return buffer;
    }
    char buffer[40];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
        if (std::strtod(buffer, nullptr) == value) break;
    }
    return buffer;
}

namespace calc_detail {

inline bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

inline bool css_number(const std::string& text, std::size_t& at, double& out) {
    const std::size_t start = at;
    std::size_t i = at;
    std::size_t digits = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') { ++i; ++digits; }
    if (i < text.size() && text[i] == '.') {
        std::size_t fraction = 0;
        std::size_t j = i + 1;
        while (j < text.size() && text[j] >= '0' && text[j] <= '9') { ++j; ++fraction; }
        if (fraction) { i = j; digits += fraction; }
    }
    if (!digits) return false;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        std::size_t j = i + 1;
        if (j < text.size() && (text[j] == '+' || text[j] == '-')) ++j;
        std::size_t exponent = 0;
        while (j < text.size() && text[j] >= '0' && text[j] <= '9') { ++j; ++exponent; }
        if (exponent) i = j;
    }
    out = std::strtod(text.substr(start, i - start).c_str(), nullptr);
    at = i;
    return true;
}

class Parser {
public:
    explicit Parser(const std::string& text) : m_text(text) {}

    bool parse(CalcNode& out) {
        if (!sum(out, 0)) return false;
        skip();
        return m_at == m_text.size();
    }

private:
    static constexpr int kMaxDepth = 32;

    void skip() {
        while (m_at < m_text.size() && is_space(m_text[m_at])) ++m_at;
    }
    bool eat(char c) {
        skip();
        if (m_at < m_text.size() && m_text[m_at] == c) { ++m_at; return true; }
        return false;
    }

    static CalcNode negate(CalcNode&& node) {
        if (node.kind == CalcNode::Number || node.kind == CalcNode::Dimension) {
            node.number = -node.number;
            return std::move(node);
        }
        CalcNode out;
        out.kind = CalcNode::Negate;
        out.children.push_back(std::move(node));
        return out;
    }

    static CalcNode invert(CalcNode&& node) {
        if (node.kind == CalcNode::Number && node.number != 0.0) {
            node.number = 1.0 / node.number;
            return std::move(node);
        }
        CalcNode out;
        out.kind = CalcNode::Invert;
        out.children.push_back(std::move(node));
        return out;
    }

    bool sum(CalcNode& out, int depth) {
        CalcNode left;
        if (!product(left, depth)) return false;
        std::vector<CalcNode> terms;
        terms.push_back(std::move(left));
        for (;;) {
            skip();
            if (m_at >= m_text.size() || (m_text[m_at] != '+' && m_text[m_at] != '-')) break;
            const char sign = m_text[m_at++];
            CalcNode right;
            if (!product(right, depth)) return false;
            if (sign == '-') right = negate(std::move(right));
            terms.push_back(std::move(right));
        }
        if (terms.size() == 1) {
            out = std::move(terms.front());
            return true;
        }
        out = CalcNode{};
        out.kind = CalcNode::Sum;
        out.children = std::move(terms);
        return true;
    }

    bool product(CalcNode& out, int depth) {
        CalcNode left;
        if (!factor(left, depth)) return false;
        std::vector<CalcNode> factors;
        factors.push_back(std::move(left));
        for (;;) {
            skip();
            if (m_at >= m_text.size() || (m_text[m_at] != '*' && m_text[m_at] != '/')) break;
            const bool times = m_text[m_at++] == '*';
            CalcNode right;
            if (!factor(right, depth)) return false;
            if (!times) right = invert(std::move(right));
            factors.push_back(std::move(right));
        }
        if (factors.size() == 1) {
            out = std::move(factors.front());
            return true;
        }
        out = CalcNode{};
        out.kind = CalcNode::Product;
        out.children = std::move(factors);
        return true;
    }

    bool factor(CalcNode& out, int depth) {
        if (depth > kMaxDepth) return false;
        skip();
        if (m_at >= m_text.size()) return false;
        if (eat('+')) return factor(out, depth);
        if (eat('-')) {
            CalcNode inner;
            if (!factor(inner, depth)) return false;
            out = negate(std::move(inner));
            return true;
        }
        const char c = m_text[m_at];
        if (c == '(') {
            ++m_at;
            CalcNode inner;
            if (!sum(inner, depth + 1)) return false;
            if (!eat(')')) return false;
            out = std::move(inner);
            return true;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') return number(out);
        std::string ident;
        if (!identifier(ident)) return false;
        if (!eat('(')) return false; // a bare identifier is not a value here
        CalcNode function;
        function.kind = CalcNode::Function;
        function.function = ident;
        if (!eat(')')) {
            for (;;) {
                CalcNode arg;
                if (!sum(arg, depth + 1)) return false;
                function.children.push_back(std::move(arg));
                if (eat(',')) continue;
                if (!eat(')')) return false;
                break;
            }
        }
        out = std::move(function);
        return true;
    }

    bool number(CalcNode& out) {
        skip();
        double value = 0.0;
        if (!css_number(m_text, m_at, value)) return false;
        const std::size_t start = m_at;
        if (m_at < m_text.size() && m_text[m_at] == '%') {
            ++m_at;
            out = CalcNode{};
            out.kind = CalcNode::Dimension;
            out.number = value;
            out.unit = "%";
            return true;
        }
        while (m_at < m_text.size()) {
            const unsigned char unitc = static_cast<unsigned char>(m_text[m_at]);
            if (std::isalpha(unitc)) ++m_at; else break;
        }
        const std::string unit = m_text.substr(start, m_at - start);
        out = CalcNode{};
        if (unit.empty()) {
            out.kind = CalcNode::Number;
            out.number = value;
            return true;
        }
        out.kind = CalcNode::Dimension;
        out.number = value;
        out.unit = unit;
        // Lowercase the unit; CSS units are case-insensitive.
        for (char& uc : out.unit) uc = static_cast<char>(std::tolower(static_cast<unsigned char>(uc)));
        return true;
    }

    bool identifier(std::string& out) {
        skip();
        const std::size_t start = m_at;
        if (m_at < m_text.size() && m_text[m_at] == '-') ++m_at;
        while (m_at < m_text.size()) {
            const unsigned char c = static_cast<unsigned char>(m_text[m_at]);
            const bool ok = std::isalnum(c) || c == '-' || c == '_' || c >= 0x80;
            if (!ok) break;
            ++m_at;
        }
        if (m_at == start) return false;
        out = m_text.substr(start, m_at - start);
        for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return true;
    }

    const std::string& m_text;
    std::size_t m_at = 0;
};

} // namespace calc_detail

// Every function inside a calc() must be one of the math functions the
// engine understands; otherwise the whole value is left alone.
inline bool calc_functions_known(const CalcNode& node) {
    if (node.kind == CalcNode::Function) {
        static const char* const kKnown[] = {"calc", "sign", "abs", "sibling-index", "sibling-count",
                                             "min",  "max",  "clamp"};
        bool found = false;
        for (const char* known : kKnown) {
            if (node.function == known) { found = true; break; }
        }
        if (!found) return false;
    }
    for (const auto& child : node.children) {
        if (!calc_functions_known(child)) return false;
    }
    return true;
}

inline bool calc_parse(const std::string& text, CalcNode& out) {
    if (text.empty()) return false;
    return calc_detail::Parser(text).parse(out);
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

namespace calc_detail {

inline bool absolute_factor(const std::string& unit, double& factor) {
    struct Entry { const char* unit; double factor; };
    static const Entry kUnits[] = {
        {"px", 1.0},        {"pt", 96.0 / 72.0}, {"pc", 16.0},     {"in", 96.0},  {"cm", 96.0 / 2.54},
        {"mm", 96.0 / 25.4}, {"q", 96.0 / 25.4 / 10.0},
    };
    for (const auto& entry : kUnits) {
        if (unit == entry.unit) { factor = entry.factor; return true; }
    }
    return false;
}

// 0: number, 1: length-percentage. Failure via 2.
// Sign of the result, for sign(): 0 unknown/invalid, -1 negative, 1 positive, 2 zero.
inline bool sign_of(const CalcTriple& value, int& sign) {
    if (!value.dimension) {
        sign = value.number < 0 ? -1 : (value.number > 0 ? 1 : 2);
        return true;
    }
    // A length-percentage can be signed only when the unresolved
    // percentage part is not in the way.
    if (value.percent != 0.0) return false;
    sign = value.px < 0 ? -1 : (value.px > 0 ? 1 : 2);
    return true;
}

inline bool eval(const CalcNode& node, const CalcContext& ctx, CalcTriple& out) {
    switch (node.kind) {
        case CalcNode::Number:
            out = CalcTriple{};
            out.number = node.number;
            return true;
        case CalcNode::Dimension: {
            const std::string& unit = node.unit;
            out = CalcTriple{};
            if (unit == "%") {
                out.dimension = true;
                out.percent = node.number;
                return true;
            }
            double factor = 0.0;
            if (absolute_factor(unit, factor)) {
                out.dimension = true;
                out.px = node.number * factor;
                return true;
            }
            if (unit == "em") { out.dimension = true; out.px = node.number * ctx.em; return true; }
            if (unit == "rem") { out.dimension = true; out.px = node.number * ctx.rem; return true; }
            if (unit == "cqw" || unit == "cqi") {
                if (ctx.query_inline_px < 0.0) return false;
                out.dimension = true;
                out.px = node.number * ctx.query_inline_px / 100.0;
                return true;
            }
            // The rest (viewport units, font metrics, ...) are unknown here.
            return false;
        }
        case CalcNode::Negate: {
            CalcTriple inner;
            if (!eval(node.children.front(), ctx, inner)) return false;
            out.px = -inner.px;
            out.percent = -inner.percent;
            out.number = -inner.number;
            out.dimension = inner.dimension;
            return true;
        }
        case CalcNode::Invert: {
            CalcTriple inner;
            if (!eval(node.children.front(), ctx, inner)) return false;
            if (inner.dimension || inner.number == 0.0) return false;
            if (!std::isfinite(inner.number)) return false;
            out = CalcTriple{};
            out.number = 1.0 / inner.number;
            return true;
        }
        case CalcNode::Sum: {
            double px = 0.0, percent = 0.0, number = 0.0;
            bool have_number = false, have_dim = false;
            for (const auto& child : node.children) {
                CalcTriple value;
                if (!eval(child, ctx, value)) return false;
                if (value.dimension) {
                    have_dim = true;
                    px += value.px;
                    percent += value.percent;
                } else {
                    have_number = true;
                    number += value.number;
                }
            }
            if (have_number && have_dim) return false;
            out = CalcTriple{};
            if (have_dim) {
                out.dimension = true;
                out.px = px;
                out.percent = percent;
            } else {
                out.number = number;
            }
            return true;
        }
        case CalcNode::Product: {
            double factor = 1.0;
            double px = 0.0, percent = 0.0;
            bool have_typed = false;
            bool typed_dim = false;
            for (const auto& child : node.children) {
                CalcTriple value;
                if (!eval(child, ctx, value)) return false;
                if (value.dimension) {
                    if (have_typed) return false;
                    have_typed = true;
                    typed_dim = true;
                    px = value.px;
                    percent = value.percent;
                } else {
                    factor *= value.number;
                }
            }
            out = CalcTriple{};
            if (have_typed) {
                out.dimension = true;
                out.px = px * factor;
                out.percent = percent * factor;
            } else {
                out.number = factor;
            }
            (void)typed_dim;
            return true;
        }
        case CalcNode::Function: {
            const std::string& name = node.function;
            if (name == "calc") {
                if (node.children.size() != 1) return false;
                return eval(node.children.front(), ctx, out);
            }
            if (name == "sibling-index") {
                if (ctx.sibling_index == 0.0) return false;
                out = CalcTriple{};
                out.number = ctx.sibling_index;
                return true;
            }
            if (name == "sibling-count") {
                if (ctx.sibling_count == 0.0) return false;
                out = CalcTriple{};
                out.number = ctx.sibling_count;
                return true;
            }
            if (name == "sign") {
                if (node.children.size() != 1) return false;
                CalcTriple inner;
                if (!eval(node.children.front(), ctx, inner)) return false;
                int sign = 0;
                if (!sign_of(inner, sign)) return false;
                out = CalcTriple{};
                out.number = sign == 2 ? 0.0 : static_cast<double>(sign);
                return true;
            }
            if (name == "abs") {
                if (node.children.size() != 1) return false;
                CalcTriple inner;
                if (!eval(node.children.front(), ctx, inner)) return false;
                if (inner.dimension && inner.px != 0.0 && inner.percent != 0.0) return false;
                out = inner;
                out.px = std::fabs(out.px);
                out.percent = std::fabs(out.percent);
                out.number = std::fabs(out.number);
                return true;
            }
            if (name == "min" || name == "max") {
                if (node.children.size() < 1) return false;
                bool have_dim = false, have_number = false;
                double px = 0.0, percent = 0.0, number = 0.0;
                bool first = true;
                for (const auto& child : node.children) {
                    CalcTriple value;
                    if (!eval(child, ctx, value)) return false;
                    if (value.dimension) have_dim = true; else have_number = true;
                    if (first) {
                        px = value.px; percent = value.percent; number = value.number;
                        first = false;
                        continue;
                    }
                    if (!value.dimension) {
                        number = name == "min" ? std::min(number, value.number) : std::max(number, value.number);
                        continue;
                    }
                    if (value.percent != 0.0 || percent != 0.0) return false; // cannot compare yet
                    px = name == "min" ? std::min(px, value.px) : std::max(px, value.px);
                }
                if (have_dim && have_number) return false;
                out = CalcTriple{};
                if (have_dim) {
                    out.dimension = true;
                    out.px = px;
                    out.percent = percent;
                } else {
                    out.number = number;
                }
                return true;
            }
            if (name == "clamp") {
                if (node.children.size() != 3) return false;
                CalcTriple lo, value, hi;
                if (!eval(node.children[0], ctx, lo) || !eval(node.children[1], ctx, value) ||
                    !eval(node.children[2], ctx, hi)) return false;
                if (lo.dimension != value.dimension || value.dimension != hi.dimension) return false;
                out = CalcTriple{};
                if (!value.dimension) {
                    out.number = std::min(std::max(value.number, lo.number), hi.number);
                    return true;
                }
                if (lo.percent != 0.0 || value.percent != 0.0 || hi.percent != 0.0) return false;
                out.dimension = true;
                out.px = std::min(std::max(value.px, lo.px), hi.px);
                return true;
            }
            return false;
        }
    }
    return false;
}

} // namespace calc_detail

inline bool calc_eval(const CalcNode& node, const CalcContext& ctx, CalcTriple& out) {
    return calc_detail::eval(node, ctx, out) && std::isfinite(out.px) && std::isfinite(out.percent) &&
           std::isfinite(out.number);
}

inline bool calc_eval_text(const std::string& text, const CalcContext& ctx, CalcTriple& out) {
    CalcNode root;
    if (!calc_parse(text, root)) return false;
    return calc_eval(root, ctx, out);
}

// A calculation that resolves to a plain number.
inline bool calc_eval_number(const std::string& text, const CalcContext& ctx, double& out) {
    CalcTriple value;
    if (!calc_eval_text(text, ctx, value)) return false;
    if (value.dimension) return false;
    out = value.number;
    return std::isfinite(out);
}

// The type of a calculation: 0 number, 1 length-percentage, 2 invalid.
inline int calc_classify(const CalcNode& node) {
    switch (node.kind) {
        case CalcNode::Number:
        case CalcNode::Invert:
            return 0;
        case CalcNode::Dimension:
            return 1;
        case CalcNode::Negate:
            return node.children.empty() ? 2 : calc_classify(node.children.front());
        case CalcNode::Sum: {
            bool have_number = false, have_dim = false;
            for (const auto& child : node.children) {
                const int child_class = calc_classify(child);
                if (child_class == 0) have_number = true;
                else if (child_class == 1) have_dim = true;
                else return 2;
            }
            if (have_number && have_dim) return 2;
            return have_dim ? 1 : 0;
        }
        case CalcNode::Product: {
            int typed = 0;
            for (const auto& child : node.children) {
                const int child_class = calc_classify(child);
                if (child_class == 2) return 2;
                if (child_class == 1) ++typed;
            }
            return typed > 1 ? 2 : (typed == 1 ? 1 : 0);
        }
        case CalcNode::Function: {
            const std::string& name = node.function;
            if (name == "sign" || name == "sibling-index" || name == "sibling-count") return 0;
            if (name == "calc" || name == "abs")
                return node.children.size() == 1 ? calc_classify(node.children.front()) : 2;
            if (name == "min" || name == "max") {
                bool have_number = false, have_dim = false;
                for (const auto& child : node.children) {
                    const int child_class = calc_classify(child);
                    if (child_class == 0) have_number = true;
                    else if (child_class == 1) have_dim = true;
                    else return 2;
                }
                if (have_number && have_dim) return 2;
                return have_dim ? 1 : 0;
            }
            if (name == "clamp") {
                if (node.children.size() != 3) return 2;
                const int lo = calc_classify(node.children[0]);
                const int value = calc_classify(node.children[1]);
                const int hi = calc_classify(node.children[2]);
                if (lo == 2 || value == 2 || hi == 2) return 2;
                if (lo != value || value != hi) return 2;
                return lo;
            }
            return 2;
        }
    }
    return 2;
}

// ---------------------------------------------------------------------------
// Canonical serialization (CSS Values 4, "serialize a calculation tree")
// ---------------------------------------------------------------------------

namespace calc_detail {

inline int sort_category(const CalcNode& node) {
    switch (node.kind) {
        case CalcNode::Number:
            return 0;
        case CalcNode::Dimension:
            return node.unit == "%" ? 1 : 2;
        default:
            return 3;
    }
}

inline void sort_children(std::vector<CalcNode>& children) {
    std::stable_sort(children.begin(), children.end(), [](const CalcNode& a, const CalcNode& b) {
        const int cat_a = sort_category(a), cat_b = sort_category(b);
        if (cat_a != cat_b) return cat_a < cat_b;
        if (cat_a == 2) {
            std::string unit_a = a.unit, unit_b = b.unit;
            for (char& c : unit_a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : unit_b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return unit_a < unit_b;
        }
        return false;
    });
}

inline bool negative_number(const CalcNode& node) {
    return (node.kind == CalcNode::Number || node.kind == CalcNode::Dimension) && node.number < 0.0;
}

inline CalcNode absolute_of(const CalcNode& node) {
    CalcNode copy = node;
    copy.number = std::fabs(copy.number);
    return copy;
}

inline std::string serialize_node(const CalcNode& node);

inline std::string serialize_sum(const CalcNode& node) {
    std::vector<CalcNode> children = node.children;
    sort_children(children);
    std::string out = "(";
    for (std::size_t i = 0; i < children.size(); ++i) {
        const CalcNode& child = children[i];
        if (i == 0) {
            out += serialize_node(child);
            continue;
        }
        if (child.kind == CalcNode::Negate) {
            out += " - " + serialize_node(child.children.front());
            continue;
        }
        if (negative_number(child)) {
            out += " - " + serialize_node(absolute_of(child));
            continue;
        }
        out += " + " + serialize_node(child);
    }
    out += ")";
    return out;
}

inline std::string serialize_product(const CalcNode& node) {
    std::vector<CalcNode> children = node.children;
    sort_children(children);
    std::string out = "(";
    for (std::size_t i = 0; i < children.size(); ++i) {
        const CalcNode& child = children[i];
        if (i == 0) {
            out += serialize_node(child);
            continue;
        }
        if (child.kind == CalcNode::Invert) {
            out += " / " + serialize_node(child.children.front());
            continue;
        }
        if (negative_number(child)) {
            // A negative factor folds its sign away, never " * -x".
            out += " * " + serialize_node(absolute_of(child));
            continue;
        }
        out += " * " + serialize_node(child);
    }
    out += ")";
    return out;
}

inline std::string serialize_function(const CalcNode& node) {
    if (node.function == "calc" && node.children.size() == 1) {
        // A nested calc() is not a node of the calculation tree; it is the
        // parenthesized child it wraps (a sum keeps precedence that way).
        return serialize_node(node.children.front());
    }
    std::string out = node.function + "(";
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        if (i) out += ", ";
        std::string child = serialize_node(node.children[i]);
        // Children of a function drop one pair of outer parens.
        if (child.size() >= 2 && child.front() == '(' && child.back() == ')') {
            // Only strip balanced outer parens.
            int depth = 0;
            bool balanced = true;
            for (std::size_t j = 0; j < child.size(); ++j) {
                if (child[j] == '(') ++depth;
                else if (child[j] == ')') --depth;
                if (depth == 0 && j + 1 != child.size()) { balanced = false; break; }
            }
            if (balanced) child = child.substr(1, child.size() - 2);
        }
        out += child;
    }
    out += ")";
    return out;
}

inline std::string serialize_node(const CalcNode& node) {
    switch (node.kind) {
        case CalcNode::Number:
            return calc_num_text(node.number);
        case CalcNode::Dimension:
            return calc_num_text(node.number) + node.unit;
        case CalcNode::Sum:
            return serialize_sum(node);
        case CalcNode::Product:
            return serialize_product(node);
        case CalcNode::Negate:
            return "(-1 * " + serialize_node(node.children.front()) + ")";
        case CalcNode::Invert:
            return "(1 / " + serialize_node(node.children.front()) + ")";
        case CalcNode::Function:
            return serialize_function(node);
    }
    return std::string();
}

inline std::string serialize_root(const CalcNode& node) {
    if (node.kind == CalcNode::Sum || node.kind == CalcNode::Product || node.kind == CalcNode::Negate ||
        node.kind == CalcNode::Invert) {
        std::string inner = serialize_node(node);
        // The outermost parens of an operator root are the calc()'s own.
        if (inner.size() >= 2 && inner.front() == '(' && inner.back() == ')') {
            inner = inner.substr(1, inner.size() - 2);
        }
        return "calc(" + inner + ")";
    }
    if (node.kind == CalcNode::Number || node.kind == CalcNode::Dimension) {
        return "calc(" + serialize_node(node) + ")";
    }
    if (node.kind == CalcNode::Function && node.function == "calc" && node.children.size() == 1) {
        return serialize_root(node.children.front());
    }
    return serialize_node(node);
}

} // namespace calc_detail

// Rewrites a calc() the way the specification serializes it: terms sorted
// (numbers, percentages, dimensions by unit, then the rest in order),
// products with the number first, whitespace normalized. A value that does
// not parse, or uses a function the engine does not know, comes back
// unchanged.
inline std::string canonicalize_calc(const std::string& text) {
    CalcNode root;
    if (!calc_parse(text, root)) return text;
    if (!calc_functions_known(root)) return text;
    return calc_detail::serialize_root(root);
}

} // namespace litebird
