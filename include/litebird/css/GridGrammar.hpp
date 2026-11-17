#pragma once

// Validation and canonical serialization for the CSS Grid property values,
// as the CSS Object Model writes them: whitespace between items is a single
// space, function arguments are separated by ", ", empty line-name lists are
// dropped and adjacent ones are merged, a unitless zero is written as
// "0px", and the keywords and units are lower-cased. Custom identifiers
// (line names, area names) keep their case.
//
// The grammar is https://drafts.csswg.org/css-grid-1 §7
// (<track-list>, <auto-track-list>, <explicit-track-list>, <track-size>,
// <fixed-size>, <track-breadth>, <inflexible-breadth>, <fixed-breadth>,
// <line-names>, <track-repeat>, <auto-repeat>, <fixed-repeat> and the
// areas syntax of grid-template), §7.1-7.4 for the grid lines, §7.6 for the
// grid shorthand and §7.7 for grid-auto-flow. Length units are those of CSS
// Values 4; a calc() (and min(), max(), clamp()) stands anywhere a
// <length-percentage> does and is not checked for sign, because its value
// is not known until the cascade. A written negative number is never valid
// in this grammar.
//
// Every function here returns false when the text is not valid for the
// property, in which case the declaration must be dropped (CSS 2.1
// §4.1.7). The WPT files under tests/wpt/css are the oracle for the details
// (which values are accepted, and how they round-trip).

#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace litebird {

// Replaces C-style CSS comments with a single space, respecting strings.
// Comments are not tokens; "auto /**/" is "auto".
inline std::string grid_strip_comments(std::string_view s) {
    std::string out;
    std::size_t i = 0;
    const std::size_t n = s.size();
    char quote = 0;
    while (i < n) {
        const char c = s[i];
        if (quote) {
            out += c;
            if (c == '\\' && i + 1 < n) { out += s[i + 1]; i += 2; continue; }
            ++i;
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; out += c; ++i; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            const std::size_t end = s.find("*/", i + 2);
            if (end == std::string_view::npos) return std::string(); // an unterminated comment is invalid
            out += ' ';
            i = end + 2;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Lexing
// ---------------------------------------------------------------------------

inline bool grid_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

inline char grid_ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline std::string grid_lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = grid_ascii_lower(c);
    return out;
}

// Splits the text into the top-level tokens: runs of characters not
// separated by whitespace, with balanced parentheses and square brackets and
// quoted strings kept intact. "/" is a token of its own. Returns false when
// the brackets do not balance or a string is unterminated.
inline bool grid_is_hex_digit(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

inline bool grid_lex(std::string_view input, std::vector<std::string>& out) {
    out.clear();
    const std::size_t n = input.size();
    std::size_t i = 0;
    while (true) {
        while (i < n && grid_is_space(input[i])) ++i;
        if (i >= n) return true;
        if (input[i] == '/') {
            out.emplace_back("/");
            ++i;
            continue;
        }
        const std::size_t start = i;
        std::size_t pdepth = 0, bdepth = 0;
        char quote = 0;
        while (i < n) {
            const char c = input[i];
            if (quote) {
                if (c == '\\' && i + 1 < n) { i += 2; continue; }
                ++i;
                if (c == quote) quote = 0;
                continue;
            }
            if (c == '"' || c == '\'') { quote = c; ++i; continue; }
            if (c == '\\') {
                // A CSS escape: \hex{1,6} plus one optional terminator
                // space, or any other character standing for itself. The
                // whole sequence is part of this token.
                ++i;
                if (i < n && grid_is_hex_digit(input[i])) {
                    std::size_t hex_count = 0;
                    while (i < n && hex_count < 6 && grid_is_hex_digit(input[i])) { ++i; ++hex_count; }
                    if (i < n && grid_is_space(input[i])) ++i; // one terminator space
                } else if (i < n) {
                    ++i;
                }
                continue;
            }
            if (c == '(') { ++pdepth; ++i; continue; }
            if (c == ')') { if (pdepth == 0) return false; --pdepth; ++i; continue; }
            if (c == '[') { ++bdepth; ++i; continue; }
            if (c == ']') { if (bdepth == 0) return false; --bdepth; ++i; continue; }
            if (c == '/' && pdepth == 0 && bdepth == 0) break; // delim token
            if (grid_is_space(c) && pdepth == 0 && bdepth == 0) break;
            ++i;
        }
        if (quote) return false;
        if (pdepth != 0 || bdepth != 0) return false;
        out.push_back(std::string(input.substr(start, i - start)));
    }
}

inline std::string grid_collapse_ws(std::string_view s) {
    std::string out;
    bool pending_space = false;
    for (const char c : s) {
        if (grid_is_space(c)) { pending_space = true; continue; }
        if (pending_space && !out.empty()) out += ' ';
        pending_space = false;
        out += c;
    }
    return out;
}

// Canonical spacing of a function token: one space after a comma, none after
// "(" or before ")", and single spaces elsewhere.
inline std::string grid_normalize_function(std::string_view token) {
    const std::string t = grid_collapse_ws(token);
    std::string out;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        if (c == ',') {
            out += ", ";
            while (i + 1 < t.size() && t[i + 1] == ' ') ++i;
            continue;
        }
        if (c == '(') {
            out += c;
            while (i + 1 < t.size() && t[i + 1] == ' ') ++i;
            continue;
        }
        if (c == ')') {
            if (!out.empty() && out.back() == ' ') out.pop_back();
            out += c;
            continue;
        }
        out += c;
    }
    return out;
}

// The <custom-ident> production (CSS Values Level 4, which includes the
// dashed-idents): any identifier that is not a reserved keyword.
// The value of a CSS ident token with its escapes decoded ("\\31 st" ->
// "1st"). False when an escape is malformed.
inline bool grid_ident_value(std::string_view token, std::string& decoded) {
    decoded.clear();
    const auto hex_value = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        return static_cast<unsigned>(c - 'A' + 10);
    };
    std::size_t i = 0;
    while (i < token.size()) {
        const char c = token[i];
        if (c != '\\') {
            decoded += c;
            ++i;
            continue;
        }
        ++i;
        if (i >= token.size()) return false;
        if (grid_is_hex_digit(token[i])) {
            unsigned value = 0;
            std::size_t hex_count = 0;
            while (i < token.size() && hex_count < 6 && grid_is_hex_digit(token[i])) {
                value = value * 16 + hex_value(token[i]);
                ++i;
                ++hex_count;
            }
            if (i < token.size() && grid_is_space(token[i])) ++i; // one terminator space
            if (value == 0 || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) value = 0xFFFD;
            if (value < 0x80) {
                decoded += static_cast<char>(value);
            } else if (value < 0x800) {
                decoded += static_cast<char>(0xC0 | (value >> 6));
                decoded += static_cast<char>(0x80 | (value & 0x3F));
            } else {
                decoded += static_cast<char>(0xE0 | (value >> 12));
                decoded += static_cast<char>(0x80 | ((value >> 6) & 0x3F));
                decoded += static_cast<char>(0x80 | (value & 0x3F));
            }
            continue;
        }
        const unsigned char escaped = static_cast<unsigned char>(token[i]);
        if (escaped == '\n' || escaped == '\r' || escaped == '\f') return false;
        decoded += token[i];
        ++i;
    }
    return true;
}

// The CSS text needed to write `decoded` as an ident in a stylesheet
// ("1st" -> "\\31 st"): a leading digit (and a leading "-" followed by one)
// is hex-escaped, as is every other code point that cannot appear in an
// ident; the rest passes through.
inline std::string grid_ident_text(std::string_view decoded) {
    std::string out;
    const auto hex_text = [](unsigned value) {
        static const char* digits = "0123456789abcdef";
        char reversed[8];
        int length = 0;
        do {
            reversed[length++] = digits[value & 0xF];
            value >>= 4;
        } while (value != 0);
        std::string text;
        for (int k = length - 1; k >= 0; --k) text += reversed[k];
        return text;
    };
    const auto escape = [&out, &hex_text](unsigned value) {
        out += '\\';
        out += hex_text(value);
        out += ' ';
    };
    for (std::size_t i = 0; i < decoded.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(decoded[i]);
        const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        const bool digit = c >= '0' && c <= '9';
        if (i == 0) {
            if (digit) {
                escape(c);
                continue;
            }
            if (c == '-') {
                const bool next_digit = decoded.size() > 1 && decoded[1] >= '0' && decoded[1] <= '9';
                if (decoded.size() == 1 || next_digit) {
                    escape(c);
                    continue;
                }
                out += '-';
                continue;
            }
            if (alpha || c >= 0x80) {
                out += static_cast<char>(c);
                continue;
            }
            escape(c);
            continue;
        }
        if (alpha || digit || c == '-' || c == '_' || c >= 0x80) {
            out += static_cast<char>(c);
            continue;
        }
        escape(c);
    }
    return out;
}

inline bool grid_custom_ident(std::string_view s) {
    std::string decoded;
    if (!grid_ident_value(s, decoded)) return false;
    if (decoded.empty()) return false;
    const bool has_escape = s.find('\\') != std::string_view::npos;
    if (has_escape) {
        // The lexer already recognised this as an ident token; still keep
        // the grammar honest: no control characters, no spaces produced by
        // "\20"-style escapes, nothing that would break the grammar when
        // written back.
        for (const char c : decoded) {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (uc < 0x21 || uc == 0x7F) return false;
            if (uc < 0x80) {
                static const char kBad[] = "()[]{}\"'/,:;<>";
                bool bad = false;
                for (const char b : kBad) {
                    if (b == static_cast<char>(uc)) { bad = true; break; }
                }
                if (bad) return false;
            }
        }
    } else {
        std::size_t i = 0;
        if (decoded.front() == '-') {
            i = 1;
            if (i < decoded.size() && decoded[i] == '-') ++i; // a dashed-ident is a custom-ident
            if (i >= decoded.size()) return false;
        }
        const auto ident_start = [](char c) {
            return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   static_cast<unsigned char>(c) >= 0x80;
        };
        const auto ident_char = [ident_start](char c) {
            return ident_start(c) || (c >= '0' && c <= '9') || c == '-';
        };
        if (!ident_start(decoded[i])) return false;
        for (std::size_t j = i + 1; j < decoded.size(); ++j) {
            if (!ident_char(decoded[j])) return false;
        }
    }
    const std::string lower = grid_lower(decoded);
    // The keywords the grid grammar reserves for itself, the CSS-wide
    // keywords, and "default" (CSS Values Level 4).
    static const char* const kReserved[] = {
        "span", "auto", "default",
        "inherit", "initial", "unset", "revert", "revert-layer",
    };
    for (const char* reserved : kReserved) {
        if (lower == reserved) return false;
    }
    return true;
}

inline bool grid_is_line_names(const std::string& token) {
    return token.size() >= 2 && token.front() == '[' && token.back() == ']';
}

// "[one two]" -> the names; an empty list is valid and serializes to "".
inline bool grid_parse_line_names(std::string_view token, std::vector<std::string>& names) {
    names.clear();
    if (!grid_is_line_names(std::string(token))) return false;
    const std::string inside = grid_collapse_ws(token.substr(1, token.size() - 2));
    if (inside.empty()) return true;
    std::string_view view(inside);
    std::size_t i = 0;
    while (i < view.size()) {
        while (i < view.size() && grid_is_space(view[i])) ++i;
        if (i >= view.size()) break;
        std::size_t j = i;
        while (j < view.size() && !grid_is_space(view[j])) ++j;
        const std::string name(view.substr(i, j - i));
        if (!grid_custom_ident(name)) return false;
        names.push_back(name);
        i = j;
    }
    return true;
}

inline std::string grid_line_names_text(const std::vector<std::string>& names) {
    std::string out = "[";
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i) out += ' ';
        out += names[i];
    }
    out += ']';
    return out;
}

// Serialization-side merge: drops empty line-name tokens and merges adjacent
// ones, so "a [x] [] [y] b" serializes as "a [x y] b".
inline std::vector<std::string> grid_collapse_name_runs(const std::vector<std::string>& parts) {
    std::vector<std::string> out;
    std::vector<std::string> pending;
    auto flush = [&]() {
        if (!pending.empty()) {
            out.push_back(grid_line_names_text(pending));
            pending.clear();
        }
    };
    for (const auto& part : parts) {
        std::vector<std::string> names;
        if (grid_is_line_names(part) && grid_parse_line_names(part, names)) {
            pending.insert(pending.end(), names.begin(), names.end());
            continue;
        }
        flush();
        out.push_back(part);
    }
    flush();
    return out;
}

// ---------------------------------------------------------------------------
// Numbers, lengths, percentages, flex
// ---------------------------------------------------------------------------

// Parses a CSS <number> prefix of `s`. On success sets `value` and `end`
// (the offset just after the number) and returns true.
inline bool grid_css_number(std::string_view s, double& value, std::size_t& end) {
    const std::size_t n = s.size();
    std::size_t i = 0;
    if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
    std::size_t digits = 0;
    while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) { ++i; ++digits; }
    if (i < n && s[i] == '.') {
        ++i;
        std::size_t frac = 0;
        while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) { ++i; ++frac; }
        digits += frac;
    }
    if (digits == 0) return false;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        std::size_t j = i + 1;
        if (j < n && (s[j] == '+' || s[j] == '-')) ++j;
        const std::size_t exp_digits = j;
        while (j < n && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
        if (j > exp_digits) i = j; // otherwise the "e" belongs to the unit
    }
    try {
        value = std::stod(std::string(s.substr(0, i)));
    } catch (...) {
        return false;
    }
    end = i;
    return true;
}

inline bool grid_length_unit(const std::string& u) {
    static const char* const kUnits[] = {
        "q", "cm", "mm", "in", "px", "pt", "pc",
        "em", "rem", "ex", "rex", "cap", "rcap", "ch", "rch", "ic", "ric", "lh", "rlh",
        "vw", "lvw", "svw", "dvw", "vh", "lvh", "svh", "dvh", "vi", "lvi", "svi", "dvi",
        "vb", "lvb", "svb", "dvb", "vmin", "lvmin", "svmin", "dvmin",
        "vmax", "lvmax", "svmax", "dvmax", "cqw", "cqh", "cqi", "cqb", "cqmin", "cqmax",
    };
    for (const char* unit : kUnits) {
        if (u == unit) return true;
    }
    return false;
}

enum class GridBreadthKind { LengthPercent, Flex, MinContent, MaxContent, AutoKeyword, CalcFunction, Invalid };

struct GridBreadth {
    GridBreadthKind kind{GridBreadthKind::Invalid};
    double number{0};
    bool zero{false};
    std::string canonical;
};

// One <track-breadth> / <inflexible-breadth> / <fixed-breadth>. A literal
// with a negative value is rejected; a calc() is accepted as-is. "0" is
// written back as "0px".
inline bool grid_parse_breadth(std::string_view token, GridBreadth& breadth) {
    const std::string t = grid_collapse_ws(token);
    if (t.empty()) return false;

    const std::size_t paren = t.find('(');
    if (paren != std::string::npos) {
        const std::string fname = grid_lower(t.substr(0, paren));
        if (t.back() != ')') return false;
        if (fname == "calc" || fname == "min" || fname == "max" || fname == "clamp") {
            const std::string inside = grid_collapse_ws(t.substr(paren + 1, t.size() - paren - 2));
            if (inside.empty()) return false;
            breadth.kind = GridBreadthKind::CalcFunction;
            breadth.canonical = grid_normalize_function(t);
            return true;
        }
        return false;
    }

    const std::string lower = grid_lower(t);
    if (lower == "min-content") { breadth.kind = GridBreadthKind::MinContent; breadth.canonical = "min-content"; return true; }
    if (lower == "max-content") { breadth.kind = GridBreadthKind::MaxContent; breadth.canonical = "max-content"; return true; }
    if (lower == "auto") { breadth.kind = GridBreadthKind::AutoKeyword; breadth.canonical = "auto"; return true; }

    double number = 0;
    std::size_t end = 0;
    if (!grid_css_number(t, number, end)) return false;
    if (number < 0 && number != 0) return false; // a literal negative never fits
    const std::string unit = grid_lower(t.substr(end));
    std::string num(t.substr(0, end));
    if (!num.empty() && num.front() == '+') num.erase(num.begin());
    if (unit.empty()) {
        if (number != 0) return false; // a unitless non-zero is not a length
        breadth.kind = GridBreadthKind::LengthPercent;
        breadth.number = 0;
        breadth.zero = true;
        breadth.canonical = "0px";
        return true;
    }
    if (unit == "%" || grid_length_unit(unit)) {
        breadth.kind = GridBreadthKind::LengthPercent;
        breadth.number = number;
        breadth.zero = number == 0;
        breadth.canonical = num + unit;
        return true;
    }
    if (unit == "fr") {
        breadth.kind = GridBreadthKind::Flex;
        breadth.number = number;
        breadth.zero = number == 0;
        breadth.canonical = num + "fr";
        return true;
    }
    return false;
}

inline bool grid_is_inflexible(const GridBreadth& b) {
    return b.kind != GridBreadthKind::Invalid && b.kind != GridBreadthKind::Flex;
}
inline bool grid_is_track_breadth(const GridBreadth& b) { return b.kind != GridBreadthKind::Invalid; }
inline bool grid_is_fixed_breadth(const GridBreadth& b) {
    return b.kind == GridBreadthKind::LengthPercent || b.kind == GridBreadthKind::CalcFunction;
}

// ---------------------------------------------------------------------------
// Track sizes
// ---------------------------------------------------------------------------

enum class GridTrackSizeKind { Breadth, Minmax, FitContent, Invalid };

struct GridTrackSize {
    GridTrackSizeKind kind{GridTrackSizeKind::Invalid};
    GridBreadth breadth;    // Breadth
    GridBreadth minimum;    // Minmax
    GridBreadth maximum;    // Minmax
    GridBreadth argument;   // FitContent
    bool is_fixed{false};   // fits <fixed-size>
    std::string canonical;
};

// Splits `s` on commas at depth zero, respecting parenthesized functions.
inline bool grid_split_commas(std::string_view s, std::vector<std::string>& parts) {
    parts.clear();
    std::string current;
    std::size_t i = 0, pdepth = 0;
    char quote = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (quote) {
            current += c;
            if (c == '\\' && i + 1 < s.size()) { current += s[++i]; ++i; continue; }
            ++i;
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; current += c; ++i; continue; }
        if (c == '(') { ++pdepth; current += c; ++i; continue; }
        if (c == ')') { if (pdepth == 0) return false; --pdepth; current += c; ++i; continue; }
        if (c == ',' && pdepth == 0) { parts.push_back(grid_collapse_ws(current)); current.clear(); ++i; continue; }
        current += c;
        ++i;
    }
    if (quote) return false;
    if (pdepth != 0) return false;
    parts.push_back(grid_collapse_ws(current));
    return true;
}

// Parses one <track-size>. When `fixed_only` is set, the result must be a
// <fixed-size>.
inline bool grid_parse_track_size(std::string_view token, bool fixed_only, GridTrackSize& size) {
    const std::string t = grid_collapse_ws(token);
    if (t.empty()) return false;

    const std::size_t paren = t.find('(');
    if (paren == std::string::npos) {
        GridBreadth breadth;
        if (!grid_parse_breadth(t, breadth)) return false;
        size = GridTrackSize{};
        size.kind = GridTrackSizeKind::Breadth;
        size.breadth = breadth;
        size.is_fixed = grid_is_fixed_breadth(breadth);
        size.canonical = breadth.canonical;
        return !fixed_only || size.is_fixed;
    }

    const std::string fname = grid_lower(t.substr(0, paren));
    if (t.back() != ')') return false;
    const std::string inside = t.substr(paren + 1, t.size() - paren - 2);

    if (fname == "calc" || fname == "min" || fname == "max" || fname == "clamp") {
        GridBreadth breadth;
        if (!grid_parse_breadth(t, breadth)) return false;
        size = GridTrackSize{};
        size.kind = GridTrackSizeKind::Breadth;
        size.breadth = breadth;
        size.is_fixed = true; // a calc() is a <length-percentage>, which is a <fixed-breadth>
        size.canonical = breadth.canonical;
        return !fixed_only || size.is_fixed;
    }

    std::vector<std::string> args;
    if (!grid_split_commas(inside, args)) return false;

    if (fname == "minmax") {
        if (args.size() != 2) return false;
        GridBreadth minimum, maximum;
        if (!grid_parse_breadth(args[0], minimum)) return false;
        if (!grid_parse_breadth(args[1], maximum)) return false;
        if (!grid_is_inflexible(minimum)) return false; // a flex in the min is not an <inflexible-breadth>
        size.kind = GridTrackSizeKind::Minmax;
        size.minimum = minimum;
        size.maximum = maximum;
        // <fixed-size> = <fixed-breadth> | minmax(<fixed-breadth>, <track-breadth>)
        //              | minmax(<inflexible-breadth>, <fixed-breadth>)
        size.is_fixed = (grid_is_fixed_breadth(minimum) && grid_is_track_breadth(maximum)) ||
                        (grid_is_inflexible(minimum) && grid_is_fixed_breadth(maximum));
        size.canonical = "minmax(" + minimum.canonical + ", " + maximum.canonical + ")";
        return !fixed_only || size.is_fixed;
    }
    if (fname == "fit-content") {
        if (args.size() != 1) return false;
        GridBreadth argument;
        if (!grid_parse_breadth(args[0], argument)) return false;
        if (!grid_is_fixed_breadth(argument)) return false; // a <length-percentage>
        size.kind = GridTrackSizeKind::FitContent;
        size.argument = argument;
        size.is_fixed = false; // fit-content() is not a <fixed-size>
        size.canonical = "fit-content(" + argument.canonical + ")";
        return !fixed_only;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Track sequences: track lists, auto track lists, explicit track lists
// ---------------------------------------------------------------------------

enum class GridTrackMode {
    Plain,           // <track-list>: integer repeats of any track-size, no auto-repeat
    Auto,            // <auto-track-list>: every track a <fixed-size>, at most one auto-repeat
    Explicit,        // <explicit-track-list>: no repeat() at all (grid-template areas form)
    AutoTracks,      // <track-size>+: no line names, no repeat() (grid-auto-columns/rows)
    IntrinsicRepeat, // an auto-repeat's list: <intrinsic-or-fixed-size> (css-grid-3)
};

inline bool grid_is_repeat(const std::string& token) {
    const auto paren = token.find('(');
    if (paren == std::string::npos) return false;
    return grid_lower(token.substr(0, paren)) == "repeat";
}

// <intrinsic-or-fixed-size> (css-grid-3) = <fixed-size> | <intrinsic-size>,
// where <intrinsic-size> = min-content | max-content | auto |
// fit-content(<length-percentage>). Used inside an auto-repeat.
inline bool grid_parse_intrinsic_or_fixed(std::string_view token, GridTrackSize& size) {
    if (grid_parse_track_size(token, true, size)) return true; // a <fixed-size>
    GridTrackSize any;
    if (!grid_parse_track_size(token, false, any)) return false;
    switch (any.kind) {
        case GridTrackSizeKind::Breadth:
            return any.breadth.kind == GridBreadthKind::AutoKeyword ||
                   any.breadth.kind == GridBreadthKind::MinContent ||
                   any.breadth.kind == GridBreadthKind::MaxContent ||
                   any.breadth.kind == GridBreadthKind::CalcFunction;
        case GridTrackSizeKind::Minmax:
            return true; // any minmax with an inflexible minimum is accepted
        case GridTrackSizeKind::FitContent:
            return true;
        default:
            return false;
    }
}

// The first argument of repeat(): an integer >= 1, "auto-fill" or "auto-fit".
inline bool grid_parse_repeat_count(std::string_view part, std::string& canonical, bool& is_auto) {
    const std::string t = grid_collapse_ws(part);
    const std::string lower = grid_lower(t);
    if (lower == "auto-fill" || lower == "auto-fit") {
        canonical = lower;
        is_auto = true;
        return true;
    }
    is_auto = false;
    double number = 0;
    std::size_t end = 0;
    if (!grid_css_number(t, number, end) || end != t.size()) return false;
    if (number != std::floor(number) || number < 1) return false;
    std::string num(t);
    if (!num.empty() && num.front() == '+') num.erase(num.begin());
    canonical = num;
    return true;
}

struct GridSequence {
    std::vector<std::string> parts;
    int tracks{0};
};

// Parses a sequence of tracks. Line names may appear before and after any
// track; an empty group is dropped. Two groups in a row are not valid here
// (adjacent line-name lists are a serialization-side merge, never a parse).
// `mode` decides what is allowed.
inline bool grid_parse_sequence(const std::vector<std::string>& tokens, GridTrackMode mode, GridSequence& out) {
    out = GridSequence{};
    std::vector<std::string> pending_names;
    int auto_repeats = 0;
    bool previous_was_names = false;

    auto flush_names = [&pending_names, &out]() {
        if (!pending_names.empty()) {
            out.parts.push_back(grid_line_names_text(pending_names));
            pending_names.clear();
        }
    };

    for (const std::string& raw : tokens) {
        if (grid_is_line_names(raw)) {
            if (mode == GridTrackMode::AutoTracks) return false; // (IntrinsicRepeat keeps line names)
            if (previous_was_names) return false; // two groups in a row
            std::vector<std::string> names;
            if (!grid_parse_line_names(raw, names)) return false;
            pending_names.insert(pending_names.end(), names.begin(), names.end());
            previous_was_names = true;
            continue;
        }
        previous_was_names = false;
        if (grid_is_repeat(raw)) {
            if (mode == GridTrackMode::Explicit || mode == GridTrackMode::AutoTracks ||
                mode == GridTrackMode::IntrinsicRepeat) return false;
            const auto paren = raw.find('(');
            const std::string inside = raw.substr(paren + 1, raw.size() - paren - 2);
            std::vector<std::string> args;
            if (!grid_split_commas(inside, args)) return false;
            if (args.size() != 2) return false;
            std::string count_text;
            bool count_is_auto = false;
            if (!grid_parse_repeat_count(args[0], count_text, count_is_auto)) return false;
            if (count_is_auto) {
                ++auto_repeats;
                if (auto_repeats > 1) return false;
            }
            std::vector<std::string> inner_tokens;
            if (!grid_lex(args[1], inner_tokens)) return false;
            GridSequence inner;
            const GridTrackMode inner_mode =
                count_is_auto ? GridTrackMode::IntrinsicRepeat
                              : mode == GridTrackMode::Auto ? GridTrackMode::Auto
                                                            : GridTrackMode::Plain;
            if (!grid_parse_sequence(inner_tokens, inner_mode, inner)) {
                return false;
            }
            if (inner.tracks == 0) return false;
            flush_names();
            std::string piece = "repeat(" + count_text + ", ";
            for (std::size_t i = 0; i < inner.parts.size(); ++i) {
                if (i) piece += ' ';
                piece += inner.parts[i];
            }
            piece += ')';
            out.parts.push_back(std::move(piece));
            out.tracks += inner.tracks;
            continue;
        }
        GridTrackSize size;
        bool size_ok;
        if (mode == GridTrackMode::IntrinsicRepeat) size_ok = grid_parse_intrinsic_or_fixed(raw, size);
        else size_ok = grid_parse_track_size(raw, mode == GridTrackMode::Auto, size);
        if (!size_ok) return false;
        flush_names();
        out.parts.push_back(size.canonical);
        ++out.tracks;
    }
    flush_names();
    if (out.tracks == 0) return false;
    return true;
}

inline std::string grid_sequence_text(const GridSequence& sequence) {
    const auto parts = grid_collapse_name_runs(sequence.parts);
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += ' ';
        out += parts[i];
    }
    return out;
}

// Splits token names at "/" into parts (one or more). False on a leading,
// trailing or repeated slash is left to the callers.
inline std::vector<std::vector<std::string>> grid_split_slashes(const std::vector<std::string>& tokens) {
    std::vector<std::vector<std::string>> parts(1);
    for (const auto& token : tokens) {
        if (token == "/") parts.emplace_back();
        else parts.back().push_back(token);
    }
    return parts;
}

inline std::string grid_join_tokens(const std::vector<std::string>& tokens) {
    std::string out;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i) out += ' ';
        out += tokens[i];
    }
    return out;
}

// ---------------------------------------------------------------------------
// The resolved value of a track list (css-grid §7.2.6)
// ---------------------------------------------------------------------------

// Six significant digits, an integer prints integer-shaped; matches the
// spelling lengths get elsewhere.
inline std::string grid_px_text(double value) {
    if (!std::isfinite(value)) return "0px";
    char buffer[48];
    if (std::fabs(value) >= 1e6) std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    else std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    std::string text = buffer;
    if (text == "-0") text = "0";
    return text + "px";
}

inline std::string grid_number_text(double value) {
    if (!std::isfinite(value)) return "0";
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    std::string text = buffer;
    if (text == "-0") text = "0";
    return text;
}

inline double grid_absolute_length_factor(const std::string& unit, double& factor) {
    if (unit == "px") factor = 1;
    else if (unit == "pt") factor = 96.0 / 72.0;
    else if (unit == "pc") factor = 16;
    else if (unit == "in") factor = 96;
    else if (unit == "cm") factor = 96 / 2.54;
    else if (unit == "mm") factor = 96 / 25.4;
    else if (unit == "q") factor = 96 / 101.6;
    else return false;
    return true;
}

// Case-insensitive search for `needle` in `haystack`, from `from`.
inline std::size_t grid_find_ci(const std::string& haystack, const std::string& needle, std::size_t from) {
    if (needle.empty() || haystack.size() < needle.size()) return std::string::npos;
    for (std::size_t i = from; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(haystack[i + j])));
            const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(needle[j])));
            if (a != b) { match = false; break; }
        }
        if (match) return i;
    }
    return std::string::npos;
}

// Simplifies every `calc(...)` subexpression in `value`. The evaluator is
// called with the full "calc(...)" text (as eval_calc accepts it) and writes
// the length part to `px` and the percentage part to `percent`; false when
// the expression is not understood (left as it is). `basis_px` >= 0 turns a
// percentage part into a length. A pure length part that is negative is
// clamped to 0 (a track breadth cannot be negative).
template <typename EvalCalc>
void grid_resolve_calc_in_place(std::string& value, double basis_px, bool clamp_negative, const EvalCalc& eval_calc) {
    std::size_t pos = 0;
    for (;;) {
        pos = grid_find_ci(value, "calc(", pos);
        if (pos == std::string::npos) return;
        // The matching close paren, counting nested parens.
        std::size_t at = pos + 5, depth = 1;
        while (at < value.size() && depth > 0) {
            if (value[at] == '(') ++depth;
            else if (value[at] == ')') --depth;
            ++at;
        }
        if (depth != 0) return; // unbalanced: leave the rest
        const std::string calc_text = value.substr(pos, at - pos);
        double px = 0, percent = 0;
        if (!eval_calc(calc_text, px, percent)) {
            pos = at; // this one stays; look after it
            continue;
        }
        if (basis_px >= 0 && percent != 0) {
            px += percent / 100.0 * basis_px;
            percent = 0;
        }
        std::string replacement;
        if (percent == 0) {
            if (clamp_negative && px < 0) px = 0;
            replacement = grid_px_text(px);
        } else if (px == 0) {
            replacement = grid_number_text(percent) + "%";
        } else {
            replacement = "calc(" + grid_number_text(percent) + "% ";
            replacement += px < 0 ? "- " : "+ ";
            replacement += grid_px_text(std::fabs(px)) + ")";
        }
        value.replace(pos, at - pos, replacement);
        pos += replacement.size();
    }
}

// Resolves `<number>em`, `<number>rem` and (with `basis_px` >= 0)
// `<number>%` at token positions — outside calc(), which has already
// simplified. A resulting length that is negative clamps to 0. The text is
// scanned by hand: a number starts at a boundary that is not an ident
// continuation, and the unit is [a-zA-Z%]* after it.
inline void grid_resolve_units_in_place(std::string& value, double em, double rem, double basis_px) {
    std::string out;
    out.reserve(value.size() + 16);
    const std::size_t n = value.size();
    std::size_t i = 0;
    auto is_ident_continue = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
    };
    while (i < n) {
        const char c = value[i];
        const bool number_start =
            std::isdigit(static_cast<unsigned char>(c)) || c == '.' ||
            ((c == '-' || c == '+') && i + 1 < n &&
             (std::isdigit(static_cast<unsigned char>(value[i + 1])) || value[i + 1] == '.'));
        if (number_start && (i == 0 || !is_ident_continue(value[i - 1]))) {
            double number = 0;
            std::size_t end = 0;
            if (grid_css_number(std::string_view(value).substr(i), number, end)) {
                // `end` is an offset from `i`, not an absolute index.
                std::size_t unit_end = end;
                while (i + unit_end < n && std::isalpha(static_cast<unsigned char>(value[i + unit_end]))) ++unit_end;
                if (i + unit_end < n && value[i + unit_end] == '%') ++unit_end;
                const std::string unit = grid_lower(value.substr(i + end, unit_end - end));
                std::string replacement;
                bool replaced = false;
                if (unit == "em") { replacement = grid_px_text(std::max(0.0, number * em)); replaced = true; }
                else if (unit == "rem") { replacement = grid_px_text(std::max(0.0, number * rem)); replaced = true; }
                else if (unit == "%" && basis_px >= 0) { replacement = grid_px_text(std::max(0.0, number / 100.0 * basis_px)); replaced = true; }
                else if (grid_length_unit(unit)) {
                    double factor = 0;
                    if (grid_absolute_length_factor(unit, factor)) {
                        replacement = grid_px_text(std::max(0.0, number * factor));
                        replaced = true;
                    }
                }
                if (replaced) {
                    out += replacement;
                    i += unit_end;
                    continue;
                }
            }
        }
        out += c;
        ++i;
    }
    value = std::move(out);
}

// True when `text` contains an auto-repeat whose repetition contains a
// track that is not a <fixed-size> — the grid-lanes-only
// <intrinsic-auto-repeat> production (css-grid-3). On anything but a
// grid-lanes container the resolved value is `none`.
inline bool grid_has_intrinsic_auto_repeat(std::string_view text) {
    std::vector<std::string> tokens;
    if (!grid_lex(grid_strip_comments(text), tokens)) return false;
    for (const std::string& token : tokens) {
        if (!grid_is_repeat(token)) continue;
        const auto paren = token.find('(');
        const std::string inside = token.substr(paren + 1, token.size() - paren - 2);
        std::vector<std::string> args;
        if (!grid_split_commas(inside, args) || args.size() != 2) continue;
        std::string count_text;
        bool is_auto = false;
        if (!grid_parse_repeat_count(args[0], count_text, is_auto) || !is_auto) continue;
        std::vector<std::string> inner_tokens;
        if (!grid_lex(args[1], inner_tokens)) continue;
        for (const std::string& inner : inner_tokens) {
            if (grid_is_line_names(inner)) continue;
            GridTrackSize size;
            if (!grid_parse_track_size(inner, true, size)) return true;
        }
    }
    return false;
}

// The fixed px size of one <fixed-size> track, for sizing an auto-repeat.
// False when the track is not a plain length/percentage (a flex, an
// intrinsic sizing, or a calc() that survived resolution).
inline bool grid_breadth_px(const GridBreadth& breadth, double basis_px, double& px) {
    if (breadth.kind != GridBreadthKind::LengthPercent) return false;
    std::size_t end = 0;
    double number = 0;
    if (!grid_css_number(breadth.canonical, number, end)) return false;
    const std::string unit = grid_lower(breadth.canonical.substr(end));
    if (unit == "%") {
        if (basis_px < 0) return false;
        px = number / 100.0 * basis_px;
        return true;
    }
    double factor = 0;
    if (!grid_absolute_length_factor(unit, factor)) return false;
    px = number * factor;
    return true;
}

inline bool grid_track_px(const std::string& token, double basis_px, double& px) {
    GridTrackSize size;
    if (!grid_parse_track_size(token, false, size)) return false;
    if (size.kind == GridTrackSizeKind::Breadth) return grid_breadth_px(size.breadth, basis_px, px);
    if (size.kind == GridTrackSizeKind::Minmax) {
        double minimum = 0, maximum = 0;
        if (!grid_breadth_px(size.minimum, basis_px, minimum)) return false;
        if (!grid_breadth_px(size.maximum, basis_px, maximum)) return false;
        px = std::max(minimum, maximum);
        return true;
    }
    return false; // fit-content() is sized by its content
}

// The resolved value of a <track-list> (css-grid §7.2.6). calc() and the
// font-relative/absolute/percentage units are resolved here; a grid
// container additionally expands repeat(<integer>) and an auto-repeat
// (against `basis_px`, the container's available space; an auto-fit's
// tracks collapse to 0px) — at least one repetition is kept — and merges
// adjacent line names across the repetition boundary. `basis_px` < 0 means
// the available space is not definite; an auto-repeat is then left alone.
template <typename EvalCalc>
inline bool grid_resolved_track_list(const std::string& value, double em, double rem, double basis_px, bool is_grid,
                                     const EvalCalc& eval_calc, std::string& out) {
    std::string text = value;
    grid_resolve_calc_in_place(text, basis_px, /*clamp_negative*/ true, eval_calc);
    grid_resolve_units_in_place(text, em, rem, basis_px);
    if (!is_grid) {
        out = std::move(text);
        return true;
    }
    std::vector<std::string> tokens;
    if (!grid_lex(text, tokens)) return false;
    std::vector<std::string> expanded;
    expanded.reserve(tokens.size() * 2);
    for (const std::string& token : tokens) {
        if (!grid_is_repeat(token)) {
            expanded.push_back(token);
            continue;
        }
        const auto paren = token.find('(');
        const std::string inside = token.substr(paren + 1, token.size() - paren - 2);
        std::vector<std::string> args;
        if (!grid_split_commas(inside, args) || args.size() != 2) {
            expanded.push_back(token);
            continue;
        }
        std::string count_text;
        bool count_is_auto = false;
        if (!grid_parse_repeat_count(args[0], count_text, count_is_auto)) {
            expanded.push_back(token);
            continue;
        }
        std::vector<std::string> inner_tokens;
        if (!grid_lex(args[1], inner_tokens) || inner_tokens.empty()) {
            expanded.push_back(token);
            continue;
        }
        if (!count_is_auto) {
            long n = 1;
            try {
                n = std::stol(count_text);
            } catch (...) {
                expanded.push_back(token);
                continue;
            }
            if (n < 1) n = 1;
            if (n > 1024) n = 1024;
            for (long i = 0; i < n; ++i) expanded.insert(expanded.end(), inner_tokens.begin(), inner_tokens.end());
            continue;
        }
        if (basis_px < 0) {
            expanded.push_back(token); // the space to repeat against is unknown
            continue;
        }
        double set_px = 0;
        bool fixed = true;
        for (const std::string& inner : inner_tokens) {
            if (grid_is_line_names(inner)) continue;
            double track_px = 0;
            if (!grid_track_px(inner, basis_px, track_px)) { fixed = false; break; }
            set_px += track_px;
        }
        bool all_zero = set_px <= 0;
        if (!fixed || (all_zero && grid_lower(count_text) == "auto-fill")) {
            expanded.push_back(token);
            continue;
        }
        std::size_t n = 1;
        if (set_px > 0) {
            n = static_cast<std::size_t>(std::floor(basis_px / set_px + 1e-9));
            if (n < 1) n = 1;
            if (n > 1024) n = 1024;
        }
        const bool collapse = grid_lower(count_text) == "auto-fit";
        for (std::size_t i = 0; i < n; ++i) {
            for (const std::string& inner : inner_tokens) {
                if (collapse && !grid_is_line_names(inner)) expanded.emplace_back("0px");
                else expanded.push_back(inner);
            }
        }
    }
    // Line names merge across repetition boundaries.
    std::vector<std::string> merged;
    merged.reserve(expanded.size());
    for (const std::string& token : expanded) {
        if (!merged.empty() && grid_is_line_names(merged.back()) && grid_is_line_names(token)) {
            std::vector<std::string> names, tail;
            if (grid_parse_line_names(merged.back(), names) && grid_parse_line_names(token, tail)) {
                names.insert(names.end(), tail.begin(), tail.end());
                merged.back() = grid_line_names_text(names);
                continue;
            }
        }
        merged.push_back(token);
    }
    out = grid_join_tokens(merged);
    return true;
}

// none | <track-list> | <auto-track-list> — grid-template-rows and
// grid-template-columns.
inline bool grid_track_list(std::string_view text, std::string& out) {
    const std::string stripped = grid_strip_comments(text);
    const std::string t = grid_collapse_ws(stripped);
    if (grid_lower(t) == "none") { out = "none"; return true; }
    std::vector<std::string> tokens;
    if (!grid_lex(t, tokens) || tokens.empty()) return false;
    // An auto-fill/auto-fit repeat makes the whole list an
    // <auto-track-list>, in which every track must be a <fixed-size>.
    bool saw_auto = false;
    for (const auto& token : tokens) {
        if (!grid_is_repeat(token)) continue;
        const auto paren = token.find('(');
        const std::string inside = token.substr(paren + 1, token.size() - paren - 2);
        std::vector<std::string> args;
        if (!grid_split_commas(inside, args)) return false;
        if (args.size() != 2) return false;
        const std::string count = grid_lower(grid_collapse_ws(args[0]));
        if (count == "auto-fill" || count == "auto-fit") saw_auto = true;
    }
    GridSequence sequence;
    if (!grid_parse_sequence(tokens, saw_auto ? GridTrackMode::Auto : GridTrackMode::Plain, sequence)) {
        return false;
    }
    out = grid_sequence_text(sequence);
    return true;
}

// <track-size>+ — grid-auto-columns and grid-auto-rows.
inline bool grid_auto_tracks(std::string_view text, std::string& out) {
    const std::string stripped = grid_strip_comments(text);
    const std::string t = grid_collapse_ws(stripped);
    std::vector<std::string> tokens;
    if (!grid_lex(t, tokens) || tokens.empty()) return false;
    GridSequence sequence;
    if (!grid_parse_sequence(tokens, GridTrackMode::AutoTracks, sequence)) return false;
    out = grid_sequence_text(sequence);
    return true;
}

// <explicit-track-list> — the column part of the grid-template areas form.
inline bool grid_explicit_track_list(std::string_view text, std::string& out) {
    const std::string stripped = grid_strip_comments(text);
    const std::string t = grid_collapse_ws(stripped);
    std::vector<std::string> tokens;
    if (!grid_lex(t, tokens) || tokens.empty()) return false;
    GridSequence sequence;
    if (!grid_parse_sequence(tokens, GridTrackMode::Explicit, sequence)) return false;
    out = grid_sequence_text(sequence);
    return true;
}

// ---------------------------------------------------------------------------
// Grid lines
// ---------------------------------------------------------------------------

// A static evaluator for the part of a calc that can be computed without
// layout: numbers, + - * /, parentheses, and min()/max()/clamp(). False
// when anything else is in the way (a unit, sibling-index(), ...).
struct GridStaticCalc {
    std::string_view text;
    std::size_t i = 0;

    void ws() {
        while (i < text.size() && grid_is_space(text[i])) ++i;
    }
    bool take(char c) {
        ws();
        if (i < text.size() && text[i] == c) {
            ++i;
            return true;
        }
        return false;
    }
    bool number(double& out) {
        ws();
        const std::string_view rest = text.substr(i);
        double value = 0;
        std::size_t end = 0;
        if (!grid_css_number(rest, value, end)) return false;
        // The number must not be the start of a unit.
        if (i + end < text.size()) {
            const unsigned char c = static_cast<unsigned char>(text[i + end]);
            const bool ident_start = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-' ||
                                     c >= 0x80;
            if (ident_start) return false;
        }
        out = value;
        i += end;
        return true;
    }
    bool identifier(std::string& out) {
        ws();
        const std::size_t start = i;
        if (i < text.size() && text[i] == '-') ++i;
        while (i < text.size()) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                            c == '-' || c == '_' || c >= 0x80;
            if (!ok) break;
            ++i;
        }
        if (i == start) return false;
        out = grid_lower(text.substr(start, i - start));
        return true;
    }
    bool factor(double& out) {
        ws();
        if (take('+')) return factor(out);
        if (take('-')) {
            double v = 0;
            if (!factor(v)) return false;
            out = -v;
            return true;
        }
        const char c = i < text.size() ? text[i] : '\0';
        if (c == '(') {
            ++i;
            double v = 0;
            if (!sum(v)) return false;
            if (!take(')')) return false;
            out = v;
            return true;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            return number(out);
        }
        std::string ident;
        if (!identifier(ident)) return false;
        if (!take('(')) return false;
        double acc = 0;
        if (ident == "calc") {
            if (!sum(acc)) return false;
            if (!take(')')) return false;
            out = acc;
            return true;
        }
        if (ident == "min" || ident == "max") {
            if (!sum(acc)) return false;
            double v = 0;
            while (take(',')) {
                if (!sum(v)) return false;
                acc = ident == "min" ? std::min(acc, v) : std::max(acc, v);
            }
            if (!take(')')) return false;
            out = acc;
            return true;
        }
        if (ident == "clamp") {
            double lo = 0, val = 0, hi = 0;
            if (!sum(lo) || !take(',')) return false;
            if (!sum(val) || !take(',')) return false;
            if (!sum(hi) || !take(')')) return false;
            out = std::min(std::max(val, lo), hi);
            return true;
        }
        return false;
    }
    bool term(double& out) {
        if (!factor(out)) return false;
        while (true) {
            ws();
            if (take('*')) {
                double v = 0;
                if (!factor(v)) return false;
                out *= v;
                continue;
            }
            if (take('/')) {
                double v = 0;
                if (!factor(v)) return false;
                if (v == 0) return false;
                out /= v;
                continue;
            }
            break;
        }
        return true;
    }
    bool sum(double& out) {
        if (!term(out)) return false;
        while (true) {
            ws();
            if (take('+')) {
                double v = 0;
                if (!term(v)) return false;
                out += v;
                continue;
            }
            if (take('-')) {
                double v = 0;
                if (!term(v)) return false;
                out -= v;
                continue;
            }
            break;
        }
        return true;
    }
};

// The calc-family function name at the head of a token, if it is a single
// such token ("calc(", "min(", "max(", "clamp("). Empty otherwise.
inline std::string grid_calc_family(const std::string& w) {
    const std::size_t paren = w.find('(');
    if (paren == std::string::npos || w.size() < paren + 2 || w.back() != ')') return {};
    const std::string name = grid_lower(w.substr(0, paren));
    if (name == "calc" || name == "min" || name == "max" || name == "clamp") return name;
    return {};
}

inline bool grid_static_number(std::string_view whole, double& out) {
    GridStaticCalc parser{whole, 0};
    if (!parser.sum(out)) return false;
    parser.ws();
    if (parser.i != whole.size()) return false;
    if (!std::isfinite(out)) return false;
    return true;
}

// <grid-line>: auto | <custom-ident> | <integer> && <custom-ident>?
//            | span && [ <integer> || <custom-ident> ]
inline bool grid_line_token(std::string_view token, std::string& out) {
    const std::string t = grid_collapse_ws(grid_strip_comments(token));
    const std::string lower = grid_lower(t);
    if (lower == "auto") { out = "auto"; return true; }

    std::vector<std::string> words;
    if (!grid_lex(t, words)) return false;
    for (const auto& word : words) {
        if (word == "/") return false; // a line cannot contain a slash
    }
    if (words.empty()) return false;

    // A calc() (or a static min()/max()/clamp()) in place of an integer:
    // static expressions are computed and written the short way; ones that
    // are not static (sibling-index(), units, ...) are kept as written.
    enum class IntegerKind { None, Numeric, CalcStatic, CalcRaw, FoldedStatic };
    struct IntegerWord {
        IntegerKind kind{IntegerKind::None};
        double value = 0;         // the static value, when there is one
        std::string emission;     // how the word is written in the canonical value
    };
    const auto classify_integer = [](const std::string& s, IntegerWord& out_word) {
        std::size_t i = 0;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        if (i != s.size()) {
            bool all_digits = true;
            for (std::size_t j = i; j < s.size(); ++j) {
                if (!std::isdigit(static_cast<unsigned char>(s[j]))) { all_digits = false; break; }
            }
            if (all_digits) {
                double value = 0;
                std::size_t end = 0;
                if (grid_css_number(s, value, end) && end == s.size()) {
                    out_word.kind = IntegerKind::Numeric;
                    out_word.value = value;
                    std::string w = s;
                    if (!w.empty() && w.front() == '+') w.erase(w.begin());
                    out_word.emission = std::move(w);
                    return true;
                }
            }
        }
        const std::string func = grid_calc_family(s);
        if (func.empty()) return false;
        double stat = 0;
        if (grid_static_number(s, stat)) {
            if (stat == 0) return false; // a calc() of exactly zero is never a grid line
            const double rounded = std::llround(stat);
            if (!std::isfinite(rounded) || std::fabs(rounded) > 1e15) return false;
            out_word.value = rounded;
            if (func == "calc") {
                // A static calc stays a calc in the specified value; it is
                // resolved at computed-value time.
                out_word.kind = IntegerKind::CalcStatic;
                out_word.emission = grid_normalize_function(s);
                return true;
            }
            // min()/max()/clamp() of constants folds into a plain calc().
            if (rounded == 0) return false;
            out_word.kind = IntegerKind::FoldedStatic;
            out_word.emission = "calc(" + grid_number_text(rounded) + ")";
            return true;
        }
        out_word.kind = IntegerKind::CalcRaw;
        out_word.emission = grid_normalize_function(s);
        return true;
    };
    const auto ident = [](const std::string& s) { return grid_custom_ident(s); };

    // "span" may come first or last ("2 i span" is a grid line); at most
    // three words make a line.
    std::size_t span_at = std::string::npos;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (grid_lower(words[i]) == "span") {
            if (span_at != std::string::npos) return false;
            span_at = i;
        }
    }
    const bool span = span_at != std::string::npos;
    if (span && span_at != 0 && span_at != words.size() - 1) return false;
    if (words.size() > 3) return false; // at most "span" plus two operands

    bool have_int = false, have_ident = false;
    double int_value = 0;
    std::string int_word;
    bool int_unknown = false;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i == span_at) continue;
        const std::string& word = words[i];
        IntegerWord integer_word;
        if (classify_integer(word, integer_word)) {
            if (have_int) return false;
            switch (integer_word.kind) {
                case IntegerKind::Numeric:
                    if (integer_word.value == 0) return false;          // a grid line number is never 0
                    if (span && integer_word.value < 0) return false;   // "span" counts forward
                    break;
                case IntegerKind::CalcStatic:
                    if (integer_word.value == 0) return false;          // rounds to 0: never a grid line
                    break; // a negative span was clamped on the way in; negative lines are fine
                case IntegerKind::FoldedStatic:
                    if (integer_word.value == 0) return false;          // min/max of constants, still a zero line
                    break;
                case IntegerKind::CalcRaw:
                    break; // nothing static to check
                case IntegerKind::None:
                    return false;
            }
            have_int = true;
            int_value = integer_word.value;
            int_word = std::move(integer_word.emission);
            if (integer_word.kind == IntegerKind::CalcRaw) int_unknown = true;
            continue;
        }
        if (ident(word)) {
            if (have_ident) return false;
            have_ident = true;
            continue;
        }
        return false;
    }
    if (!have_int && !have_ident) return false;              // "span" on its own is not a grid line
    if (!span && !have_int) {
        // a bare <custom-ident> is a grid line; nothing else is
        if (!have_ident) return false;
    }

    // Canonical order: "span" first, then the integer (a "+" is dropped and a
    // "span 1 <ident>" drops the 1), then the ident, re-escaped as needed.
    std::vector<std::string> canonical_words;
    if (span) canonical_words.push_back("span");
    if (have_int) {
        if (span && have_ident && !int_unknown && int_value == 1) {
            // "span 1 i" serializes as "span i" (the default span is 1).
        } else {
            canonical_words.push_back(std::move(int_word));
        }
    }
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i == span_at) continue;
        IntegerWord integer_word;
        if (classify_integer(words[i], integer_word)) continue; // written above
        std::string decoded;
        if (grid_ident_value(words[i], decoded)) canonical_words.push_back(grid_ident_text(decoded));
        else canonical_words.push_back(words[i]);
    }
    out.clear();
    for (std::size_t i = 0; i < canonical_words.size(); ++i) {
        if (i) out += ' ';
        out += canonical_words[i];
    }
    return true;
}

// The computed value of a canonical <grid-line> (css-grid §8; calc() is
// resolved against what layout knows): a static calc() folds to the integer
// it rounds to, and a span count is clamped to at least one. False when the
// value is not one line or carries a calc that is not static, in which case
// it is kept as written.
inline bool grid_line_computed(std::string_view canonical, std::string& out) {
    std::vector<std::string> words;
    if (!grid_lex(std::string(canonical), words)) return false;
    std::size_t span_at = std::string::npos;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (grid_lower(words[i]) == "span") {
            span_at = i;
            break;
        }
    }
    const bool span = span_at != std::string::npos;
    bool changed = false;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i == span_at) continue;
        if (grid_calc_family(words[i]).empty()) continue;
        double stat = 0;
        if (!grid_static_number(words[i], stat)) return false; // not static: keep as written
        double rounded = std::llround(stat);
        if (span && rounded < 1) rounded = 1;
        if (!span && rounded == 0) return false;
        words[i] = grid_number_text(rounded);
        changed = true;
    }
    if (!changed) return false;
    out.clear();
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i) out += ' ';
        out += words[i];
    }
    return true;
}

// Splits a "<grid-line> / <grid-line> / ..." text into its lines. False when
// the slash structure is wrong (leading, trailing or doubled slash).
inline bool grid_split_lines(std::string_view text, std::vector<std::string>& lines) {
    lines.clear();
    std::vector<std::string> tokens;
    if (!grid_lex(grid_strip_comments(text), tokens)) return false;
    std::string current;
    for (const auto& token : tokens) {
        if (token == "/") {
            if (current.empty()) return false;
            lines.push_back(grid_collapse_ws(current));
            current.clear();
            continue;
        }
        if (!current.empty()) current += ' ';
        current += token;
    }
    if (current.empty()) return false;
    lines.push_back(grid_collapse_ws(current));
    return true;
}

// The value a trailing shorthand component takes when it is omitted from
// the shorthand: a copy of the line it is recovered from when that is a
// bare <custom-ident>, otherwise "auto" (CSS Grid §8.3/§8.4).
inline std::string grid_recoverable_end(const std::string& start) {
    return grid_custom_ident(start) ? start : "auto";
}

// <grid-line> [ / <grid-line> ]? — grid-column and grid-row. The second
// line is dropped when the same value would be recovered from the first
// ("first / first" serializes as "first", "10 / auto" as "10").
inline bool grid_line_pair(std::string_view text, std::string& out) {
    std::vector<std::string> lines;
    if (!grid_split_lines(grid_strip_comments(text), lines) || lines.size() > 2) return false;
    std::vector<std::string> serialized;
    for (const auto& raw : lines) {
        std::string line;
        if (!grid_line_token(raw, line)) return false;
        serialized.push_back(std::move(line));
    }
    if (serialized.size() == 2 && serialized[1] == grid_recoverable_end(serialized[0])) {
        serialized.pop_back();
    }
    out.clear();
    for (std::size_t i = 0; i < serialized.size(); ++i) {
        if (i) out += " / ";
        out += serialized[i];
    }
    return true;
}

// <grid-line> [ / <grid-line> ]{0,3} | <custom-ident> — grid-area and the
// placement longhands.
inline bool grid_area_value(std::string_view text, std::string& out) {
    const std::string stripped = grid_strip_comments(text);
    const std::string t = grid_collapse_ws(stripped);
    if (t.empty()) return false;
    if (grid_custom_ident(t)) { out = t; return true; }
    std::vector<std::string> lines;
    if (!grid_split_lines(t, lines) || lines.size() > 4) return false;
    std::vector<std::string> serialized;
    for (const auto& raw : lines) {
        std::string line;
        if (!grid_line_token(raw, line)) return false;
        serialized.push_back(std::move(line));
    }
    // The shorthand recovers a trailing omitted line from the first one
    // (a copy when the first is a bare ident, otherwise "auto"); the
    // shortest serializable form drops each trailing line that way, from
    // the back.
    if (serialized.size() == 4 && serialized[3] == grid_recoverable_end(serialized[1])) {
        serialized.pop_back(); // column-end is recoverable from column-start
    }
    if (serialized.size() == 3 && serialized[2] == grid_recoverable_end(serialized[0])) {
        serialized.pop_back(); // row-end is recoverable from row-start
    }
    if (serialized.size() == 2 && serialized[1] == grid_recoverable_end(serialized[0])) {
        serialized.pop_back(); // the column line is recoverable from row-start
    }
    out.clear();
    for (std::size_t i = 0; i < serialized.size(); ++i) {
        if (i) out += " / ";
        out += serialized[i];
    }
    return true;
}

// ---------------------------------------------------------------------------
// grid-auto-flow
// ---------------------------------------------------------------------------

// [ row | column ] || dense — the row keyword is implied by "dense" on its own,
// so the serialization writes it only when it stands alone.
inline bool grid_auto_flow(std::string_view text, std::string& out) {
    const std::string t = grid_lower(grid_collapse_ws(grid_strip_comments(text)));
    std::vector<std::string> words;
    {
        std::string_view view = t;
        std::size_t i = 0;
        while (i < view.size()) {
            while (i < view.size() && grid_is_space(view[i])) ++i;
            if (i >= view.size()) break;
            std::size_t j = i;
            while (j < view.size() && !grid_is_space(view[j])) ++j;
            words.emplace_back(view.substr(i, j - i));
            i = j;
        }
    }
    if (words.empty() || words.size() > 2) return false;
    bool row = false, column = false, dense = false;
    for (const auto& word : words) {
        if (word == "row") { if (row) return false; row = true; }
        else if (word == "column") { if (column) return false; column = true; }
        else if (word == "dense") { if (dense) return false; dense = true; }
        else return false;
    }
    if (row && column) return false;
    if (column) out = dense ? "column dense" : "column";
    else if (dense) out = "dense";
    else if (row) out = "row";
    else return false;
    return true;
}

// ---------------------------------------------------------------------------
// grid-template-areas
// ---------------------------------------------------------------------------

// A CSS string token (matching single or double quotes): sets `inner` to
// the raw bytes between the quotes.
inline bool grid_string_token(const std::string& token, std::string_view& inner) {
    if (token.size() < 2) return false;
    const char q = token.front();
    if (q != '"' && q != '\'') return false;
    if (token.back() != q) return false;
    inner = std::string_view(token).substr(1, token.size() - 2);
    return true;
}

// A named or null cell: a run of "." is a null cell; a run of ident code
// points is a named cell; whitespace separates tokens; anything else is a
// trash token and the declaration is invalid (CSS Grid §7.3).
inline bool grid_lex_area_row(std::string_view inner, std::vector<std::string>& cells) {
    cells.clear();
    const auto is_ident_cp = [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               c == '-' || c == '_' || c >= 0x80;
    };
    std::size_t i = 0;
    const std::size_t n = inner.size();
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(inner[i]);
        if (grid_is_space(c)) { ++i; continue; }
        if (c == '.') {
            std::size_t j = i;
            while (j < n && inner[j] == '.') ++j;
            cells.emplace_back(".");
            i = j;
            continue;
        }
        if (is_ident_cp(c)) {
            std::size_t j = i;
            while (j < n && is_ident_cp(static_cast<unsigned char>(inner[j]))) ++j;
            cells.emplace_back(inner.substr(i, j - i));
            i = j;
            continue;
        }
        return false; // a trash token
    }
    return true;
}

struct GridAreaRow {
    std::vector<std::string> cells; // "." for an empty cell
};

// Each row string has at least one cell; all rows agree on the number of
// cells; each named area covers a rectangle. Returns the parsed rows.
inline bool grid_parse_areas(const std::vector<std::string>& strings, std::vector<GridAreaRow>& rows) {
    rows.clear();
    if (strings.empty()) return false;
    std::size_t width = 0;
    std::unordered_map<std::string, std::vector<std::pair<int, int>>> cells_of;
    int grid_row = 0;
    for (const std::string& token : strings) {
        std::string_view inner_sv;
        if (!grid_string_token(token, inner_sv)) return false;
        const std::string inside = grid_collapse_ws(inner_sv);
        GridAreaRow row;
        if (!grid_lex_area_row(inside, row.cells)) return false;
        if (row.cells.empty()) return false;
        if (grid_row == 0) width = row.cells.size();
        else if (row.cells.size() != width) return false;
        for (std::size_t c = 0; c < row.cells.size(); ++c) {
            if (row.cells[c] != ".") cells_of[row.cells[c]].emplace_back(grid_row, static_cast<int>(c));
        }
        rows.push_back(std::move(row));
        ++grid_row;
    }
    for (const auto& [name, cells] : cells_of) {
        int r0 = cells[0].first, r1 = cells[0].first, c0 = cells[0].second, c1 = cells[0].second;
        for (const auto& [r, c] : cells) {
            r0 = std::min(r0, r); r1 = std::max(r1, r);
            c0 = std::min(c0, c); c1 = std::max(c1, c);
        }
        for (int r = r0; r <= r1; ++r) {
            for (int c = c0; c <= c1; ++c) {
                if (rows[static_cast<std::size_t>(r)].cells[static_cast<std::size_t>(c)] != name) return false;
            }
        }
    }
    return true;
}

// grid-template-areas: none | <string>+
inline bool grid_template_areas(std::string_view text, std::string& out) {
    const std::string stripped = grid_strip_comments(text);
    const std::string t = grid_collapse_ws(stripped);
    if (grid_lower(t) == "none") { out = "none"; return true; }
    std::vector<std::string> tokens;
    if (!grid_lex(t, tokens) || tokens.empty()) return false;
    for (const auto& token : tokens) {
        std::string_view inner;
        if (!grid_string_token(token, inner)) return false;
    }
    std::vector<GridAreaRow> rows;
    if (!grid_parse_areas(tokens, rows)) return false;
    out.clear();
    for (std::size_t r = 0; r < rows.size(); ++r) {
        if (r) out += ' ';
        out += '"';
        for (std::size_t c = 0; c < rows[r].cells.size(); ++c) {
            if (c) out += ' ';
            out += rows[r].cells[c];
        }
        out += '"';
    }
    return true;
}

// ---------------------------------------------------------------------------
// grid-template
// ---------------------------------------------------------------------------

struct GridTemplateAreaRow {
    std::vector<std::string> leading_names;
    std::string row_string;
    std::string track_size;      // canonical; "" when absent
    std::vector<std::string> trailing_names;
};

// The "[ <line-names>? <string> <track-size>? <line-names>? ]+" rows of a
// grid-template areas form, and the areas parsed out of the strings.
inline bool grid_parse_template_area_rows(const std::vector<std::string>& row_tokens,
                                          std::vector<GridTemplateAreaRow>& rows,
                                          std::vector<GridAreaRow>& area_rows) {
    rows.clear();
    area_rows.clear();
    std::size_t pos = 0;
    while (pos < row_tokens.size()) {
        GridTemplateAreaRow row;
        if (grid_is_line_names(row_tokens[pos])) {
            if (!grid_parse_line_names(row_tokens[pos], row.leading_names)) return false;
            ++pos;
        }
        if (pos >= row_tokens.size()) return false;
        std::string_view inner;
        if (!grid_string_token(row_tokens[pos], inner)) return false;
        row.row_string = row_tokens[pos];
        ++pos;
        if (pos < row_tokens.size()) {
            GridTrackSize size;
            if (grid_parse_track_size(row_tokens[pos], false, size)) {
                row.track_size = size.canonical;
                ++pos;
            }
        }
        if (pos < row_tokens.size() && grid_is_line_names(row_tokens[pos])) {
            if (!grid_parse_line_names(row_tokens[pos], row.trailing_names)) return false;
            ++pos;
        }
        rows.push_back(std::move(row));
    }
    if (rows.empty()) return false;

    std::vector<std::string> row_strings;
    for (const auto& row : rows) row_strings.push_back(row.row_string);
    return grid_parse_areas(row_strings, area_rows);
}

// none | <grid-template-rows> / <grid-template-columns>
//      | [ <line-names>? <string> <track-size>? <line-names>? ]+ [ / <explicit-track-list> ]?
inline bool grid_template_value(std::string_view text, std::string& out) {
    const std::string stripped = grid_strip_comments(text);
    const std::string t = grid_collapse_ws(stripped);
    if (grid_lower(t) == "none") { out = "none"; return true; }
    std::vector<std::string> tokens;
    if (!grid_lex(t, tokens) || tokens.empty()) return false;

    const bool has_string = std::any_of(tokens.begin(), tokens.end(), [](const std::string& s) {
        std::string_view inner;
        return grid_string_token(s, inner);
    });
    const auto parts = grid_split_slashes(tokens);
    if (parts.size() > 2) return false;
    if (parts.size() == 2 && (parts[0].empty() || parts[1].empty())) return false; // "a /" or "/ a"
    const bool has_columns = parts.size() == 2;

    if (!has_string) {
        if (parts.size() != 2) return false; // "<rows> / <cols>" needs both sides
        std::string rows, columns;
        if (!grid_track_list(grid_join_tokens(parts[0]), rows)) return false;
        if (!grid_track_list(grid_join_tokens(parts[1]), columns)) return false;
        if (rows == "none" && columns == "none") { out = "none"; return true; }
        out = rows + " / " + columns;
        return true;
    }

    // The areas syntax.
    std::vector<GridTemplateAreaRow> rows;
    std::vector<GridAreaRow> area_rows;
    if (!grid_parse_template_area_rows(parts[0], rows, area_rows)) return false;

    std::vector<std::string> pieces;
    for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
        const GridTemplateAreaRow& row = rows[row_index];
        if (!row.leading_names.empty()) pieces.push_back(grid_line_names_text(row.leading_names));
        // The row string is written with double quotes and single spaces.
        std::string canonical_row("\"");
        const std::vector<std::string>& cells = area_rows[row_index].cells;
        for (std::size_t c = 0; c < cells.size(); ++c) {
            if (c) canonical_row += ' ';
            canonical_row += cells[c];
        }
        canonical_row += '"';
        pieces.push_back(std::move(canonical_row));
        if (!row.track_size.empty() && grid_lower(row.track_size) != "auto") pieces.push_back(row.track_size);
        if (!row.trailing_names.empty()) pieces.push_back(grid_line_names_text(row.trailing_names));
    }

    if (has_columns) {
        std::string columns;
        if (!grid_explicit_track_list(grid_join_tokens(parts[1]), columns)) return false;
        pieces.push_back("/");
        pieces.push_back(columns);
    }
    // Re-lex to merge the adjacent line-name groups the rows introduced
    // ("a [x] [] [y] b" serializes as "a [x y] b"), dropping the empties.
    std::vector<std::string> lexed;
    if (!grid_lex(grid_join_tokens(pieces), lexed)) return false;
    const std::vector<std::string> merged_parts = grid_collapse_name_runs(lexed);
    if (merged_parts.empty() && !pieces.empty()) return false;
    out = grid_join_tokens(merged_parts);
    return true;
}


// The longhands a <grid-template> value stands for: rows, columns and the
// areas. The areas form distributes the track sizes and the line names of
// each row onto the rows; a row without a track size gets "auto".
inline bool grid_template_longhands(std::string_view text, std::string& rows_out, std::string& columns_out,
                                    std::string& areas_out) {
    const std::string stripped = grid_strip_comments(text);
    const std::string t = grid_collapse_ws(stripped);
    if (grid_lower(t) == "none") { rows_out = "none"; columns_out = "none"; areas_out = "none"; return true; }
    std::vector<std::string> tokens;
    if (!grid_lex(t, tokens) || tokens.empty()) return false;

    const bool has_string = std::any_of(tokens.begin(), tokens.end(), [](const std::string& s) {
        std::string_view inner;
        return grid_string_token(s, inner);
    });
    const auto parts = grid_split_slashes(tokens);
    if (parts.size() > 2) return false;
    if (parts.size() == 2 && (parts[0].empty() || parts[1].empty())) return false;
    const bool has_columns = parts.size() == 2;

    if (!has_string) {
        if (parts.size() != 2) return false;
        if (!grid_track_list(grid_join_tokens(parts[0]), rows_out)) return false;
        if (!grid_track_list(grid_join_tokens(parts[1]), columns_out)) return false;
        areas_out = "none";
        return true;
    }

    std::vector<GridTemplateAreaRow> rows;
    std::vector<GridAreaRow> area_rows;
    if (!grid_parse_template_area_rows(parts[0], rows, area_rows)) return false;

    // <'grid-template-rows'>: the track sizes and line names per row, in
    // order; adjacent line-name groups merge into one group.
    std::vector<std::string> pieces;
    for (const auto& row : rows) {
        if (!row.leading_names.empty()) pieces.push_back(grid_line_names_text(row.leading_names));
        pieces.push_back(row.track_size.empty() ? "auto" : row.track_size);
        if (!row.trailing_names.empty()) pieces.push_back(grid_line_names_text(row.trailing_names));
    }
    const std::vector<std::string> merged = grid_collapse_name_runs(pieces);
    rows_out = grid_join_tokens(merged);

    // <'grid-template-columns'>: the explicit list on the right of the
    // slash, none when there is none.
    if (has_columns) {
        if (!grid_explicit_track_list(grid_join_tokens(parts[1]), columns_out)) return false;
    } else {
        columns_out = "none";
    }

    // <'grid-template-areas'>: the strings, double-quote delimited.
    areas_out.clear();
    for (const auto& cells : area_rows) {
        if (!areas_out.empty()) areas_out += ' ';
        areas_out += '"';
        for (std::size_t c = 0; c < cells.cells.size(); ++c) {
            if (c) areas_out += ' ';
            areas_out += cells.cells[c];
        }
        areas_out += '"';
    }
    return true;
}

// The longhands a <grid> value stands for: the grid-template longhands
// plus the grid-auto longhands. The grid's auto longhands stay initial
// unless the value packs the implicit axis ("auto-flow").
inline bool grid_shorthand_longhands(std::string_view text,
                                     std::string& rows_out, std::string& columns_out, std::string& areas_out,
                                     std::string& auto_rows_out, std::string& auto_columns_out,
                                     std::string& auto_flow_out) {
    const std::string stripped = grid_strip_comments(text);
    const std::string t = grid_collapse_ws(stripped);
    if (grid_lower(t) == "none") {
        rows_out = columns_out = areas_out = "none";
        auto_rows_out = auto_columns_out = "auto";
        auto_flow_out = "row";
        return true;
    }

    std::string as_template;
    if (grid_template_value(t, as_template)) {
        if (!grid_template_longhands(t, rows_out, columns_out, areas_out)) return false;
        auto_rows_out = auto_columns_out = "auto";
        auto_flow_out = "row";
        return true;
    }

    std::vector<std::string> tokens;
    if (!grid_lex(t, tokens) || tokens.empty()) return false;
    const auto parts = grid_split_slashes(tokens);
    if (parts.size() != 2 || parts[0].empty() || parts[1].empty()) return false;

    auto side_has_auto_flow = [](const std::vector<std::string>& side) {
        for (const auto& s : side) {
            const std::string l = grid_lower(s);
            if (l == "auto-flow" || l == "dense") return true;
        }
        return false;
    };
    const bool left_auto = side_has_auto_flow(parts[0]);
    const bool right_auto = side_has_auto_flow(parts[1]);
    if (left_auto == right_auto) return false;

    const auto parse_auto_flow_side = [&](const std::vector<std::string>& side, std::string& tracks, bool& dense) {
        dense = false;
        tracks = "auto";
        std::size_t i = 0;
        bool auto_flow = false;
        while (i < side.size()) {
            const std::string l = grid_lower(side[i]);
            if (l == "auto-flow" && !auto_flow) { auto_flow = true; ++i; continue; }
            if (l == "dense" && !dense) { dense = true; ++i; continue; }
            break;
        }
        if (!auto_flow) return false;
        if (i < side.size()) {
            if (!grid_auto_tracks(grid_join_tokens({side.begin() + i, side.end()}), tracks)) return false;
        }
        return true;
    };
    bool dense = false;
    std::string tracks = "auto";
    if (left_auto) {
        if (!parse_auto_flow_side(parts[0], tracks, dense)) return false;
        if (!grid_track_list(grid_join_tokens(parts[1]), columns_out)) return false;
        rows_out = areas_out = "none";
        auto_rows_out = tracks;
        auto_columns_out = "auto";
        auto_flow_out = dense ? "dense" : "row";
        return true;
    }
    if (!parse_auto_flow_side(parts[1], tracks, dense)) return false;
    if (!grid_track_list(grid_join_tokens(parts[0]), rows_out)) return false;
    columns_out = areas_out = "none";
    auto_columns_out = tracks;
    auto_rows_out = "auto";
    auto_flow_out = dense ? "column dense" : "column";
    return true;
}

// Composes a <grid-template> value from the grid-template-rows,
// grid-template-columns and grid-template-areas longhands: the areas form
// with the track sizes and the line names distributed onto the area rows
// (a track size of "auto" is implied). False when the rows cannot feed the
// areas (one segment per area row).
inline bool grid_template_from_longhands(const std::string& rows, const std::string& columns,
                                         const std::string& areas, std::string& out) {
    const std::string areas_t = grid_collapse_ws(grid_strip_comments(areas));
    if (grid_lower(areas_t) == "none") return false; // no areas: the caller writes "rows / cols"
    std::vector<std::string> area_tokens;
    if (!grid_lex(areas_t, area_tokens) || area_tokens.empty()) return false;
    std::vector<GridAreaRow> area_rows;
    if (!grid_parse_areas(area_tokens, area_rows)) return false;

    // The rows: a track list with per-track line names.
    std::vector<std::string> rtokens;
    if (!grid_lex(grid_strip_comments(grid_collapse_ws(rows)), rtokens)) return false;
    struct Seg {
        std::vector<std::string> names;
        std::string size;
        std::vector<std::string> trailing;
    };
    std::vector<Seg> segs;
    std::vector<std::string> names;
    for (std::size_t i = 0; i < rtokens.size(); ++i) {
        const std::string& token = rtokens[i];
        if (grid_is_line_names(token)) {
            std::vector<std::string> parsed;
            if (!grid_parse_line_names(token, parsed)) return false;
            names.insert(names.end(), parsed.begin(), parsed.end());
            continue;
        }
        GridTrackSize size;
        if (!grid_parse_track_size(token, false, size)) return false;
        Seg seg;
        seg.names = std::move(names);
        names.clear();
        seg.size = size.canonical;
        // Trailing line-name groups of the previous boundary were consumed
        // by the previous iteration.
        segs.push_back(std::move(seg));
    }
    if (!names.empty()) {
        if (segs.empty()) return false;
        segs.back().trailing.insert(segs.back().trailing.end(), names.begin(), names.end());
    }
    if (segs.size() != area_rows.size()) return false;

    std::vector<std::string> pieces;
    for (std::size_t i = 0; i < segs.size(); ++i) {
        if (!segs[i].names.empty()) pieces.push_back(grid_line_names_text(segs[i].names));
        const std::vector<std::string>& cells = area_rows[i].cells;
        std::string canonical_row("\"");
        for (std::size_t c = 0; c < cells.size(); ++c) {
            if (c) canonical_row += ' ';
            canonical_row += cells[c];
        }
        canonical_row += '"';
        pieces.push_back(std::move(canonical_row));
        if (segs[i].size != "auto") pieces.push_back(segs[i].size);
        // The trailing names of the row are the boundary between it and the
        // next one; merge them with the next row's leading names.
        std::vector<std::string> boundary = std::move(segs[i].trailing);
        if (i + 1 < segs.size()) {
            boundary.insert(boundary.end(), segs[i + 1].names.begin(), segs[i + 1].names.end());
            segs[i + 1].names.clear();
        }
        if (!boundary.empty()) pieces.push_back(grid_line_names_text(boundary));
    }
    out = grid_join_tokens(pieces);
    if (out.empty()) return false;

    const std::string columns_t = grid_collapse_ws(grid_strip_comments(columns));
    if (columns_t == "none") return true; // the slash part is optional
    out += " / " + columns_t;
    return true;
}

// grid: none | <grid-template>
//           | <grid-template-rows> / [ auto-flow && dense? ] <grid-auto-columns>?
//           | [ auto-flow && dense? ] <grid-auto-rows>? / <grid-template-columns>
inline bool grid_shorthand(std::string_view text, std::string& out) {
    const std::string stripped = grid_strip_comments(text);
    const std::string t = grid_collapse_ws(stripped);
    if (grid_lower(t) == "none") { out = "none"; return true; }

    std::string as_template;
    if (grid_template_value(t, as_template)) {
        out = as_template;
        return true;
    }

    std::vector<std::string> tokens;
    if (!grid_lex(t, tokens) || tokens.empty()) return false;
    const auto parts = grid_split_slashes(tokens);
    if (parts.size() != 2 || parts[0].empty() || parts[1].empty()) return false;

    auto side_has_auto_flow = [](const std::vector<std::string>& side) {
        for (const auto& s : side) {
            const std::string l = grid_lower(s);
            if (l == "auto-flow" || l == "dense") return true;
        }
        return false;
    };
    const bool left_auto = side_has_auto_flow(parts[0]);
    const bool right_auto = side_has_auto_flow(parts[1]);
    if (left_auto == right_auto) return false;

    // "auto-flow [ dense ] [ <track-size>+ ]?" in either order.
    auto parse_auto_flow_side = [](const std::vector<std::string>& side, std::string& canonical) {
        std::size_t i = 0;
        bool auto_flow = false, dense = false;
        while (i < side.size()) {
            const std::string l = grid_lower(side[i]);
            if (l == "auto-flow" && !auto_flow) { auto_flow = true; ++i; continue; }
            if (l == "dense" && !dense) { dense = true; ++i; continue; }
            break;
        }
        if (!auto_flow) return false;
        canonical = "auto-flow";
        if (dense) canonical += " dense";
        if (i < side.size()) {
            std::string tracks;
            if (!grid_auto_tracks(grid_join_tokens({side.begin() + i, side.end()}), tracks)) return false;
            canonical += ' ';
            canonical += tracks;
        }
        return true;
    };
    auto tracks_side = [](const std::vector<std::string>& side, std::string& canonical) {
        return grid_track_list(grid_join_tokens(side), canonical);
    };

    std::string rows, columns;
    if (left_auto) {
        if (!parse_auto_flow_side(parts[0], rows)) return false;
        if (!tracks_side(parts[1], columns)) return false;
    } else {
        if (!tracks_side(parts[0], rows)) return false;
        if (!parse_auto_flow_side(parts[1], columns)) return false;
    }
    if (rows == "none" && columns == "none") { out = "none"; return true; }
    out = rows + " / " + columns;
    return true;
}

} // namespace litebird
