// CSSStyleDeclaration: `element.style` and what getComputedStyle returns.
//
// `element.style` is a view of the style attribute. It reads the attribute
// again whenever the attribute has changed, and every write goes back into
// the attribute as the whole list, so the two can never disagree. A value is
// checked against what its property accepts before it is stored: assigning
// "banana" to `width` changes nothing, as in a browser.
//
// The computed style has its own cascade. It is not the layout engine's
// (css/StyleResolver.hpp), which resolves into the typed values layout needs;
// a script wants text back, for every property there is. It starts from the
// defaults the HTML standard gives each element, reads the <style> elements
// of the document, matches their selectors with the script selector engine,
// orders what matched by importance, specificity and position, and applies
// the style attribute on top. What it answers is the computed value: a
// property nobody declared has its inherited or initial value, a colour is
// rgb(), a length is in px. It knows nothing of layout, so `width: auto` and
// `50%` stay what they are.

#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/Selector.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/common/Color.hpp"
#include "litebird/css/GridGrammar.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_set>

namespace litebird {
namespace dom {

namespace {

// ---------------------------------------------------------------------------
// Property names
// ---------------------------------------------------------------------------

const std::unordered_set<std::string>& known_properties() {
    static const std::unordered_set<std::string> s_names = {
        "accent-color", "content-visibility",
        "align-content", "align-items", "align-self", "all", "animation", "animation-delay", "animation-direction",
        "animation-duration", "animation-fill-mode", "animation-iteration-count", "animation-name",
        "animation-play-state", "animation-timing-function", "appearance", "aspect-ratio", "backdrop-filter",
        "backface-visibility", "background", "background-attachment", "background-blend-mode", "background-clip",
        "background-color", "background-image", "background-origin", "background-position", "background-position-x",
        "background-position-y", "background-repeat", "background-repeat-x", "background-repeat-y",
        "background-size", "block-size", "border", "border-bottom", "border-bottom-color",
        "border-bottom-left-radius", "border-bottom-right-radius", "border-bottom-style", "border-bottom-width",
        "border-collapse", "border-color", "border-image", "border-image-outset", "border-image-repeat",
        "border-image-slice", "border-image-source", "border-image-width", "border-left", "border-left-color",
        "border-left-style", "border-left-width", "border-radius", "border-right", "border-right-color",
        "border-right-style", "border-right-width", "border-spacing", "border-style", "border-top",
        "border-top-color", "border-top-left-radius", "border-top-right-radius", "border-top-style",
        "border-top-width", "border-width",
        "bottom", "box-shadow", "box-sizing", "break-after", "break-before", "break-inside", "caption-side",
        "caret-color", "clear", "clip", "clip-path", "color", "color-scheme", "column-count", "column-fill",
        "column-gap", "column-rule", "column-rule-color", "column-rule-style", "column-rule-width", "column-span",
        "column-width", "columns", "contain", "container-type", "content", "counter-increment", "counter-reset", "cursor", "direction",
        "display", "empty-cells", "fill", "fill-opacity", "filter", "flex", "flex-basis", "flex-direction",
        "flex-flow", "flex-grow", "flex-shrink", "flex-wrap", "float", "font", "font-family", "font-feature-settings",
        "font-kerning", "font-size", "font-size-adjust", "font-stretch", "font-style", "font-variant",
        "font-variant-caps", "font-variant-numeric", "font-weight", "gap", "grid", "grid-area", "grid-auto-columns",
        "grid-auto-flow", "grid-auto-rows", "grid-column", "grid-column-end", "grid-column-gap", "grid-column-start",
        "grid-gap", "grid-row", "grid-row-end", "grid-row-gap", "grid-row-start", "grid-template",
        "grid-template-areas", "grid-template-columns", "grid-template-rows", "height", "hyphens",
        "image-rendering", "inline-size", "inset", "isolation", "justify-content", "justify-items", "justify-self",
        "left", "letter-spacing", "line-break", "line-height", "list-style", "list-style-image",
        "list-style-position", "list-style-type", "margin", "margin-block", "margin-block-end",
        "margin-block-start", "margin-bottom", "margin-inline", "margin-inline-end", "margin-inline-start",
        "margin-left", "margin-right", "margin-top", "mask", "mask-image", "max-block-size", "max-height",
        "max-inline-size", "max-width", "min-block-size", "min-height", "min-inline-size", "min-width",
        "mix-blend-mode", "object-fit", "object-position", "opacity", "order", "orphans", "outline",
        "outline-color", "outline-offset", "outline-style", "outline-width", "overflow", "overflow-anchor",
        "overflow-wrap", "overflow-x", "overflow-y", "overscroll-behavior", "padding", "padding-block",
        "padding-block-end", "padding-block-start", "padding-bottom", "padding-inline", "padding-inline-end",
        "padding-inline-start", "padding-left", "padding-right", "padding-top", "page-break-after",
        "page-break-before", "page-break-inside", "perspective", "perspective-origin", "place-content",
        "place-items", "place-self", "pointer-events", "position", "quotes", "resize", "right", "rotate", "row-gap",
        "scale", "scroll-behavior", "scroll-margin", "scroll-padding", "scroll-snap-align", "scroll-snap-type",
        "scrollbar-width", "shape-outside", "stroke", "stroke-dasharray", "stroke-dashoffset", "stroke-linecap",
        "stroke-linejoin", "stroke-opacity", "stroke-width", "tab-size", "table-layout", "text-align",
        "text-align-last", "text-decoration", "text-decoration-color", "text-decoration-line",
        "text-decoration-style", "text-decoration-thickness", "text-indent", "text-overflow", "text-rendering",
        "text-shadow", "text-transform", "text-underline-offset", "top", "touch-action", "transform",
        "transform-box", "transform-origin", "transform-style", "transition", "transition-delay",
        "transition-duration", "transition-property", "transition-timing-function", "translate", "unicode-bidi",
        "user-select", "vertical-align", "visibility", "white-space", "widows", "width", "will-change",
        "word-break", "word-spacing", "word-wrap", "writing-mode", "z-index", "zoom",
        "-webkit-appearance", "-webkit-box-shadow", "-webkit-line-clamp", "-webkit-box-orient",
        "-webkit-overflow-scrolling", "-webkit-tap-highlight-color", "-webkit-text-fill-color",
        "-webkit-text-size-adjust", "-webkit-transform", "-webkit-transition", "-webkit-user-select",
        "-moz-appearance", "-moz-user-select",
    };
    return s_names;
}

std::string canonical_css_property(const std::string& lower) {
    static const std::unordered_map<std::string, std::string> s_aliases = {
        {"-webkit-transform", "transform"},
        {"-webkit-transition", "transition"},
        {"-webkit-transition-delay", "transition-delay"},
        {"-webkit-transition-duration", "transition-duration"},
        {"-webkit-transition-property", "transition-property"},
        {"-webkit-transition-timing-function", "transition-timing-function"},
        {"-webkit-animation", "animation"},
        {"-webkit-animation-delay", "animation-delay"},
        {"-webkit-animation-direction", "animation-direction"},
        {"-webkit-animation-duration", "animation-duration"},
        {"-webkit-animation-fill-mode", "animation-fill-mode"},
        {"-webkit-animation-iteration-count", "animation-iteration-count"},
        {"-webkit-animation-name", "animation-name"},
        {"-webkit-animation-play-state", "animation-play-state"},
        {"-webkit-animation-timing-function", "animation-timing-function"},
        {"-webkit-box-shadow", "box-shadow"},
        {"-webkit-border-radius", "border-radius"},
        {"-webkit-appearance", "appearance"},
    };
    auto it = s_aliases.find(lower);
    return it == s_aliases.end() ? lower : it->second;
}

bool is_custom_property(const std::string& name) {
    return name.size() > 2 && name[0] == '-' && name[1] == '-';
}

// The flexbox properties: the legacy -webkit- names are aliases for the
// standard properties (css-flexbox §3.1.1), read and written as the
// standard property.
const std::unordered_map<std::string, std::string>& webkit_aliases() {
    static const std::unordered_map<std::string, std::string> s_aliases = {
        {"-webkit-align-content", "align-content"},
        {"-webkit-align-items", "align-items"},
        {"-webkit-align-self", "align-self"},
        {"-webkit-flex", "flex"},
        {"-webkit-flex-basis", "flex-basis"},
        {"-webkit-flex-direction", "flex-direction"},
        {"-webkit-flex-flow", "flex-flow"},
        {"-webkit-flex-grow", "flex-grow"},
        {"-webkit-flex-shrink", "flex-shrink"},
        {"-webkit-flex-wrap", "flex-wrap"},
        {"-webkit-justify-content", "justify-content"},
        {"-webkit-order", "order"},
    };
    return s_aliases;
}

std::string canonicalize_property_name(const std::string& name) {
    const auto found = webkit_aliases().find(name);
    return canonical_css_property(found == webkit_aliases().end() ? name : found->second);
}

bool is_css_property(const std::string& name) {
    return is_custom_property(name) || known_properties().count(name) != 0;
}

// The CSS name a script property stands for: "fontSize" and "font-size" are
// both `font-size`, "cssFloat" is `float`. Empty when it is not a property.
std::string css_name_for(const std::string& property) {
    if (property == "cssFloat") return "float";
    if (property.empty() || property[0] == '%' || property[0] == '@') return std::string();
    std::string dashed;
    bool has_upper = false;
    for (char c : property) {
        if (c >= 'A' && c <= 'Z') {
            has_upper = true;
            dashed += '-';
            dashed += static_cast<char>(c - 'A' + 'a');
        } else {
            dashed += c;
        }
    }
    if (has_upper) {
        // "webkitTransform" is `-webkit-transform`.
        if (dashed.compare(0, 7, "webkit-") == 0 || dashed.compare(0, 4, "moz-") == 0 || dashed.compare(0, 3, "ms-") == 0) {
            dashed = "-" + dashed;
        }
        if (property.find('-') != std::string::npos) return std::string();
    }
    dashed = canonicalize_property_name(dashed);
    return known_properties().count(dashed) ? dashed : std::string();
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

std::string trim(const std::string& text) {
    return strip_ascii_space(text);
}

bool is_global_keyword(const std::string& lower) {
    return lower == "inherit" || lower == "initial" || lower == "unset" || lower == "revert" || lower == "revert-layer";
}

bool is_function_value(const std::string& lower, const char* name) {
    const std::size_t length = std::char_traits<char>::length(name);
    return lower.size() > length + 1 && lower.compare(0, length, name) == 0 && lower[length] == '(' && lower.back() == ')';
}

bool parse_css_number(const std::string& text, std::size_t& at, double& out) {
    const std::size_t start = at;
    std::size_t i = at;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
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

std::string format_number(double value) {
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

bool is_length_unit(const std::string& unit) {
    static const char* const kUnits[] = {"px", "em", "rem", "%", "vh", "vw", "vmin", "vmax", "pt", "pc", "in", "cm",
                                         "mm", "ex", "ch", "q", "fr", "dvh", "dvw", "svh", "svw", "lvh", "lvw"};
    for (const char* known : kUnits) {
        if (unit == known) return true;
    }
    return false;
}

// A length or percentage. "0" becomes "0px"; any other bare number is not one.
bool normalize_length(const std::string& text, std::string& out) {
    const std::string lower = ascii_lower(text);
    if (is_function_value(lower, "calc") || is_function_value(lower, "var") || is_function_value(lower, "min") ||
        is_function_value(lower, "max") || is_function_value(lower, "clamp") || is_function_value(lower, "env")) {
        out = text;
        return true;
    }
    std::size_t at = 0;
    double number = 0;
    if (!parse_css_number(lower, at, number)) return false;
    const std::string unit = lower.substr(at);
    if (unit.empty()) {
        if (number != 0) return false;
        out = "0px";
        return true;
    }
    if (!is_length_unit(unit)) return false;
    // The number the way a number is written: 1.50px is 1.5px, .5em is
    // 0.5em, 2e1px is 20px.
    out = format_number(number) + unit;
    return true;
}

// A length that cannot be less than nothing. A calc() is taken on trust.
bool is_negative_length(const std::string& normalized) {
    std::size_t at = 0;
    double number = 0;
    return parse_css_number(normalized, at, number) && number < 0;
}

bool may_be_negative(const std::string& property) {
    static const std::unordered_set<std::string> s_never = {
        "width", "height", "min-width", "min-height", "max-width", "max-height", "block-size", "inline-size",
        "padding-top", "padding-right", "padding-bottom", "padding-left", "font-size", "line-height", "flex-basis",
        "row-gap", "column-gap", "outline-width", "border-top-width", "border-right-width", "border-bottom-width",
        "border-left-width", "border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius",
        "border-bottom-left-radius", "column-width", "column-rule-width",
    };
    return s_never.count(property) == 0;
}

// A colour, written the way a browser gives it back (CSS Color 4 / 5).
bool normalize_color(const std::string& text, std::string& out) {
    const std::string lower = ascii_lower(text);
    unsigned named = 0;
    if (Color::named_color_value(lower, named) ||
        lower == "transparent" || lower == "currentcolor" ||
        is_global_keyword(lower)) {
        out = lower;
        return true;
    }
    if (is_function_value(lower, "var")) {
        out = text;
        return true;
    }
    // Deferred light-dark(): validate both branches strictly and preserve
    // the specified expression so getComputedStyle resolves against color-scheme.
    if (is_function_value(lower, "light-dark")) {
        if (!CssColor::is_valid_syntax(text)) return false;
        out = text;
        return true;
    }
    CssColor parsed;
    if (CssColor::parse(text, parsed) && !parsed.is_currentcolor) {
        out = parsed.serialize_computed();
        return true;
    }
    // Valid color-mix() or relative color referencing currentcolor: preserve
    // the specified expression for computed-value resolution against `color`.
    if (CssColor::is_valid_syntax(text)) {
        out = text;
        return true;
    }
    return false;
}

bool one_of(const std::string& value, std::initializer_list<const char*> options) {
    for (const char* option : options) {
        if (value == option) return true;
    }
    return false;
}

bool is_border_style(const std::string& lower) {
    return one_of(lower, {"none", "hidden", "dotted", "dashed", "solid", "double", "groove", "ridge", "inset", "outset"});
}

bool normalize_border_width(const std::string& text, std::string& out) {
    const std::string lower = ascii_lower(text);
    if (one_of(lower, {"thin", "medium", "thick"}) || is_global_keyword(lower)) {
        out = lower;
        return true;
    }
    return normalize_length(text, out) && out.back() != '%' && !is_negative_length(out);
}

enum class ValueKind { Any, Length, LengthOrAuto, LengthOrNone, Color, BorderWidth, BorderStyle, Keyword, Number, Integer, FontSize, LineHeight };

struct PropertyRule {
    ValueKind kind;
    std::vector<const char*> keywords;
};

const std::unordered_map<std::string, PropertyRule>& property_rules() {
    static const std::unordered_map<std::string, PropertyRule> s_rules = [] {
        std::unordered_map<std::string, PropertyRule> rules;
        for (const char* name : {"width", "height", "top", "right", "bottom", "left", "margin-top", "margin-right",
                                 "margin-bottom", "margin-left", "flex-basis"}) {
            rules[name] = {ValueKind::LengthOrAuto, {"auto", "min-content", "max-content", "fit-content", "content"}};
        }
        for (const char* name : {"min-width", "min-height"}) {
            rules[name] = {ValueKind::LengthOrAuto, {"auto", "min-content", "max-content", "fit-content"}};
        }
        for (const char* name : {"max-width", "max-height"}) {
            rules[name] = {ValueKind::LengthOrNone, {"none", "min-content", "max-content", "fit-content"}};
        }
        for (const char* name : {"padding-top", "padding-right", "padding-bottom", "padding-left", "text-indent",
                                 "outline-offset", "border-top-left-radius", "border-top-right-radius",
                                 "border-bottom-right-radius", "border-bottom-left-radius"}) {
            rules[name] = {ValueKind::Length, {}};
        }
        for (const char* name : {"letter-spacing", "word-spacing", "column-gap", "row-gap"}) {
            rules[name] = {ValueKind::Length, {"normal"}};
        }
        for (const char* name : {"color", "background-color", "border-top-color", "border-right-color",
                                 "border-bottom-color", "border-left-color", "outline-color", "caret-color",
                                 "text-decoration-color"}) {
            rules[name] = {ValueKind::Color, {}};
        }
        for (const char* name : {"border-top-width", "border-right-width", "border-bottom-width",
                                 "border-left-width", "outline-width"}) {
            rules[name] = {ValueKind::BorderWidth, {}};
        }
        for (const char* name : {"border-top-style", "border-right-style", "border-bottom-style", "border-left-style"}) {
            rules[name] = {ValueKind::BorderStyle, {}};
        }
        rules["outline-style"] = {ValueKind::Keyword,
                                  {"none", "hidden", "dotted", "dashed", "solid", "double", "groove", "ridge", "inset",
                                   "outset", "auto"}};
        rules["display"] = {ValueKind::Keyword,
                            {"block", "inline", "inline-block", "flex", "inline-flex", "grid", "inline-grid", "none",
                             "contents", "table", "inline-table", "table-row", "table-cell", "table-row-group",
                             "table-header-group", "table-footer-group", "table-column", "table-column-group",
                             "table-caption", "list-item", "flow-root", "run-in", "ruby"}};
        rules["position"] = {ValueKind::Keyword, {"static", "relative", "absolute", "fixed", "sticky"}};
        rules["visibility"] = {ValueKind::Keyword, {"visible", "hidden", "collapse"}};
        rules["float"] = {ValueKind::Keyword, {"left", "right", "none", "inline-start", "inline-end"}};
        rules["clear"] = {ValueKind::Keyword, {"left", "right", "both", "none", "inline-start", "inline-end"}};
        rules["box-sizing"] = {ValueKind::Keyword, {"content-box", "border-box"}};
        for (const char* name : {"overflow-x", "overflow-y"}) {
            rules[name] = {ValueKind::Keyword, {"visible", "hidden", "scroll", "auto", "clip"}};
        }
        rules["flex-direction"] = {ValueKind::Keyword, {"row", "row-reverse", "column", "column-reverse"}};
        rules["flex-wrap"] = {ValueKind::Keyword, {"nowrap", "wrap", "wrap-reverse"}};
        rules["list-style-position"] = {ValueKind::Keyword, {"inside", "outside"}};
        rules["text-align"] = {ValueKind::Keyword,
                               {"left", "right", "center", "justify", "start", "end", "match-parent", "-webkit-center"}};
        rules["font-style"] = {ValueKind::Keyword, {"normal", "italic", "oblique"}};
        rules["text-transform"] = {ValueKind::Keyword, {"none", "capitalize", "uppercase", "lowercase", "full-width"}};
        rules["opacity"] = {ValueKind::Number, {}};
        rules["flex-direction"] = {ValueKind::Keyword, {"row", "row-reverse", "column", "column-reverse"}};
        rules["flex-wrap"] = {ValueKind::Keyword, {"nowrap", "wrap", "wrap-reverse"}};
        rules["flex-grow"] = {ValueKind::Number, {}};
        rules["flex-shrink"] = {ValueKind::Number, {}};
        rules["z-index"] = {ValueKind::Integer, {"auto"}};
        rules["order"] = {ValueKind::Integer, {}};
        rules["font-size"] = {ValueKind::FontSize, {"xx-small", "x-small", "small", "medium", "large", "x-large",
                                                    "xx-large", "xxx-large", "smaller", "larger"}};
        rules["line-height"] = {ValueKind::LineHeight, {"normal"}};
        return rules;
    }();
    return s_rules;
}

// The grid properties have a grammar of their own (include/litebird/css/
// GridGrammar.hpp): whether the text is one of the values they accept, and
// how it is written back. False for the properties that are not grid
// properties; for a grid property, the grammar decides.
bool grid_property_canonical(const std::string& property, const std::string& value, std::string& out) {
    if (property == "grid-template-columns" || property == "grid-template-rows") return grid_track_list(value, out);
    if (property == "grid-auto-columns" || property == "grid-auto-rows") return grid_auto_tracks(value, out);
    if (property == "grid-template") return grid_template_value(value, out);
    if (property == "grid-template-areas") return grid_template_areas(value, out);
    if (property == "grid") return grid_shorthand(value, out);
    if (property == "grid-auto-flow") return grid_auto_flow(value, out);
    if (property == "grid-column-start" || property == "grid-column-end" || property == "grid-row-start" ||
        property == "grid-row-end") {
        return grid_line_token(value, out);
    }
    if (property == "grid-column" || property == "grid-row") return grid_line_pair(value, out);
    if (property == "grid-area") return grid_area_value(value, out);
    return false;
}

bool is_grid_property(const std::string& property) {
    static const std::unordered_set<std::string> kNames = {
        "grid-template-columns", "grid-template-rows", "grid-auto-columns", "grid-auto-rows",
        "grid-template", "grid-template-areas", "grid", "grid-auto-flow",
        "grid-column-start", "grid-column-end", "grid-row-start", "grid-row-end",
        "grid-column", "grid-row", "grid-area",
    };
    return kNames.count(property) != 0;
}

std::string canonicalize_css_value(const std::string& property, const std::string& raw) {
    const std::string trimmed = trim(raw);
    if (trimmed.empty() || is_custom_property(property)) return trimmed;
    const bool preserve_case = property == "font-family" || property == "animation-name" ||
                               property == "list-style-type" || property == "grid-area" ||
                               property == "grid-row" || property == "grid-column" ||
                               property == "font" || property == "list-style";

    auto is_ident_start = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
    };
    auto is_ident_char = [&](char c) {
        return is_ident_start(c) || (c >= '0' && c <= '9');
    };
    auto append_space = [](std::string& dst) {
        if (!dst.empty() && dst.back() != ' ' && dst.back() != '(') dst += ' ';
    };

    std::string out;
    int paren_depth = 0;
    for (std::size_t i = 0; i < trimmed.size();) {
        const char c = trimmed[i];
        if (c == '"' || c == '\'') {
            const char q = c;
            out += q;
            ++i;
            while (i < trimmed.size()) {
                const char sc = trimmed[i++];
                out += sc;
                if (sc == '\\' && i < trimmed.size()) out += trimmed[i++];
                else if (sc == q) break;
            }
            continue;
        }
        if (is_ascii_space(c)) {
            while (i < trimmed.size() && is_ascii_space(trimmed[i])) ++i;
            if (i < trimmed.size() && trimmed[i] != ',' && trimmed[i] != ')') append_space(out);
            continue;
        }
        if (c == ',') {
            while (!out.empty() && out.back() == ' ') out.pop_back();
            out += ',';
            ++i;
            while (i < trimmed.size() && is_ascii_space(trimmed[i])) ++i;
            if (i < trimmed.size() && trimmed[i] != ')') out += ' ';
            continue;
        }
        if (c == '(') {
            if (property == "transform" && !out.empty()) {
                for (const char* tf : {"translatex", "translatey", "translatez", "scalex", "scaley", "scalez",
                                       "rotatex", "rotatey", "rotatez", "skewx", "skewy"}) {
                    const std::size_t n = std::char_traits<char>::length(tf);
                    if (out.size() >= n && out.compare(out.size() - n, n, tf) == 0 &&
                        (out.size() == n || !is_ident_char(out[out.size() - n - 1]))) {
                        out.back() = static_cast<char>(out.back() - 'a' + 'A');
                        break;
                    }
                }
            }
            ++paren_depth;
            out += '(';
            ++i;
            while (i < trimmed.size() && is_ascii_space(trimmed[i])) ++i;
            continue;
        }
        if (c == ')') {
            while (!out.empty() && out.back() == ' ') out.pop_back();
            if (paren_depth > 0) --paren_depth;
            out += ')';
            ++i;
            continue;
        }
        // Check for url(...) case-insensitively at an identifier boundary
        if (i + 3 < trimmed.size() && (trimmed[i] | 0x20) == 'u' && (trimmed[i + 1] | 0x20) == 'r' &&
            (trimmed[i + 2] | 0x20) == 'l' && trimmed[i + 3] == '(' &&
            (i == 0 || !is_ident_char(trimmed[i - 1]))) {
            std::size_t j = i + 4;
            char q = 0;
            int depth = 1;
            std::string inner;
            while (j < trimmed.size() && depth > 0) {
                const char uc = trimmed[j++];
                if (q) {
                    inner += uc;
                    if (uc == '\\' && j < trimmed.size()) inner += trimmed[j++];
                    else if (uc == q) q = 0;
                } else if (uc == '"' || uc == '\'') {
                    q = uc;
                    inner += uc;
                } else if (uc == '(') {
                    ++depth;
                    inner += uc;
                } else if (uc == ')') {
                    --depth;
                    if (depth > 0) inner += uc;
                } else {
                    inner += uc;
                }
            }
            std::string target = trim(inner);
            if (target.size() >= 2 && ((target.front() == '"' && target.back() == '"') ||
                                       (target.front() == '\'' && target.back() == '\''))) {
                const char oldq = target.front();
                std::string unq = target.substr(1, target.size() - 2);
                if (oldq == '\'') {
                    std::string esc;
                    for (char ch : unq) {
                        if (ch == '"') esc += "\\\"";
                        else esc += ch;
                    }
                    target = std::move(esc);
                } else {
                    target = std::move(unq);
                }
            }
            out += "url(\"";
            out += target;
            out += "\")";
            i = j;
            continue;
        }
        if (paren_depth > 0 || !preserve_case) {
            out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        } else {
            out += c;
        }
        ++i;
    }
    return trim(out);
}

// Checks `text` against what `property` accepts and gives it back the way
// it is stored. False when the declaration must be dropped.
bool normalize_value(const std::string& property, const std::string& text, std::string& out, bool inline_mode = false) {
    const std::string value = trim(text);
    if (value.empty()) return false;
    if (is_custom_property(property)) {
        out = value;
        return true;
    }
    const std::string lower = ascii_lower(value);
    if (is_grid_property(property)) {
        if (is_global_keyword(lower)) {
            out = lower;
            return true;
        }
        if (is_function_value(lower, "var")) {
            out = value;
            return true;
        }
        return grid_property_canonical(property, value, out);
    }
    auto found = property_rules().find(property);
    if (found == property_rules().end()) {
        if (!inline_mode || lower.find("var(") != std::string::npos) {
            out = value;
        } else {
            out = canonicalize_css_value(property, value);
        }
        return true;
    }
    const PropertyRule& rule = found->second;
    if (is_global_keyword(lower) || is_function_value(lower, "var")) {
        out = is_global_keyword(lower) ? lower : value;
        return true;
    }
    for (const char* keyword : rule.keywords) {
        if (lower == keyword) {
            out = lower;
            return true;
        }
    }
    switch (rule.kind) {
        case ValueKind::Any:
            out = (inline_mode && lower.find("var(") == std::string::npos)
                      ? canonicalize_css_value(property, value)
                      : value;
            return true;
        case ValueKind::Length:
        case ValueKind::LengthOrAuto:
        case ValueKind::LengthOrNone:
        case ValueKind::FontSize:
            return normalize_length(value, out) && (may_be_negative(property) || !is_negative_length(out));
        case ValueKind::LineHeight: {
            std::size_t at = 0;
            double number = 0;
            if (parse_css_number(lower, at, number) && at == lower.size()) {
                if (number < 0) return false;
                out = format_number(number);
                return true;
            }
            return normalize_length(value, out) && !is_negative_length(out);
        }
        case ValueKind::Color:
            return normalize_color(value, out);
        case ValueKind::BorderWidth:
            return normalize_border_width(value, out);
        case ValueKind::BorderStyle:
            if (!is_border_style(lower)) return false;
            out = lower;
            return true;
        case ValueKind::Keyword:
            return false;
        case ValueKind::Number: {
            std::size_t at = 0;
            double number = 0;
            if (parse_css_number(lower, at, number)) {
                const std::string unit = lower.substr(at);
                if (!unit.empty() && !(unit == "%" && property == "opacity")) return false;
                out = format_number(number);
                return true;
            }
            // A calc() is a number: a static one folds into "calc(<n>)", the
            // rest is kept written the way a calculation serializes.
            if (!litebird::grid_calc_family(lower).empty()) {
                litebird::CalcTriple triple;
                if (litebird::calc_eval_text(lower, litebird::CalcContext{}, triple) && !triple.dimension) {
                    out = "calc(" + litebird::calc_num_text(triple.number) + ")";
                } else {
                    out = litebird::canonicalize_calc(lower);
                }
                return true;
            }
            return false;
        }
        case ValueKind::Integer: {
            std::size_t at = 0;
            double number = 0;
            if (!parse_css_number(lower, at, number) || at != lower.size() || number != std::floor(number)) return false;
            out = format_number(number);
            return true;
        }
    }
    return false;
}

// Splits a value on whitespace that is not inside parentheses or quotes.
std::vector<std::string> split_components(const std::string& value) {
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    char quote = 0;
    for (char c : value) {
        if (quote) {
            current += c;
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            current += c;
        } else if (c == '(') {
            ++depth;
            current += c;
        } else if (c == ')') {
            --depth;
            current += c;
        } else if (is_ascii_space(c) && depth <= 0) {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

bool normalize_css_time(const std::string& text, bool non_negative, std::string& out) {
    const std::string lower = ascii_lower(trim(text));
    std::size_t at = 0;
    double number = 0;
    if (!parse_css_number(lower, at, number)) return false;
    if (non_negative && number < 0) return false;
    const std::string unit = lower.substr(at);
    if (unit != "s" && unit != "ms") return false;
    out = format_number(number) + unit;
    return true;
}

bool has_unquoted_comma(const std::string& text) {
    int depth = 0;
    char quote = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (quote) {
            if (c == '\\' && i + 1 < text.size()) ++i;
            else if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')') {
            if (depth > 0) --depth;
        } else if (c == ',' && depth == 0) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Shorthands
// ---------------------------------------------------------------------------

using Parts = std::vector<std::pair<std::string, std::string>>;
bool decompose(const std::string& name, const std::string& value, Parts& out);
bool take_apart(const std::string& name, const std::string& value, Parts& out);
bool covered_by(const std::string& shorthand, const std::string& name);
std::string put_together(const std::string& name, const std::vector<std::string>& v, bool collapse = true);
const std::vector<std::string>* parts_of(const std::string& name);

enum class ShorthandKind { FourSides, Side, Border, Background, GridLinePair, GridArea, GridTemplate, Grid, FlexFlow, Flex };

struct Shorthand {
    std::string name;
    ShorthandKind kind;
    std::vector<std::string> longhands;
};

const std::vector<Shorthand>& shorthands() {
    static const std::vector<Shorthand> s_table = [] {
        std::vector<Shorthand> table;
        const char* const sides[] = {"top", "right", "bottom", "left"};
        auto four = [&](const char* name, const char* prefix, const char* suffix) {
            Shorthand entry{name, ShorthandKind::FourSides, {}};
            for (const char* side : sides) entry.longhands.push_back(std::string(prefix) + side + suffix);
            table.push_back(entry);
        };
        // The widest first: a wider shorthand covers the narrower ones.
        Shorthand border{"border", ShorthandKind::Border, {}};
        for (const char* part : {"-width", "-style", "-color"}) {
            for (const char* side : sides) border.longhands.push_back(std::string("border-") + side + part);
        }
        table.push_back(border);
        four("margin", "margin-", "");
        four("padding", "padding-", "");
        four("border-width", "border-", "-width");
        four("border-style", "border-", "-style");
        four("border-color", "border-", "-color");
        for (const char* side : sides) {
            Shorthand entry{std::string("border-") + side, ShorthandKind::Side, {}};
            for (const char* part : {"-width", "-style", "-color"}) {
                entry.longhands.push_back(std::string("border-") + side + part);
            }
            table.push_back(entry);
        }
        table.push_back({"background", ShorthandKind::Background, {"background-color"}});
        // The grid shorthands: the widest covers the narrower ones.
        Shorthand grid{"grid", ShorthandKind::Grid,
                       {"grid-template-rows", "grid-template-columns", "grid-template-areas",
                        "grid-auto-rows", "grid-auto-columns", "grid-auto-flow"}};
        table.push_back(grid);
        Shorthand grid_template{"grid-template", ShorthandKind::GridTemplate,
                                {"grid-template-rows", "grid-template-columns", "grid-template-areas"}};
        table.push_back(grid_template);
        Shorthand grid_area{"grid-area", ShorthandKind::GridArea,
                            {"grid-row-start", "grid-column-start", "grid-row-end", "grid-column-end"}};
        table.push_back(grid_area);
        table.push_back({"grid-row", ShorthandKind::GridLinePair, {"grid-row-start", "grid-row-end"}});
        table.push_back({"grid-column", ShorthandKind::GridLinePair, {"grid-column-start", "grid-column-end"}});
        table.push_back({"flex", ShorthandKind::Flex, {"flex-grow", "flex-shrink", "flex-basis"}});
        table.push_back({"flex-flow", ShorthandKind::FlexFlow, {"flex-direction", "flex-wrap"}});
        return table;
    }();
    return s_table;
}

const Shorthand* find_shorthand(const std::string& name) {
    for (const auto& entry : shorthands()) {
        if (name == entry.name) return &entry;
    }
    return nullptr;
}

bool covers(const Shorthand& shorthand, const std::string& longhand) {
    return std::find(shorthand.longhands.begin(), shorthand.longhands.end(), longhand) != shorthand.longhands.end();
}

struct InlineShorthandDef {
    std::string name;
    std::vector<std::string> longhands;
};

const std::vector<InlineShorthandDef>& inline_shorthands() {
    static const std::vector<InlineShorthandDef> s_table = {
        // Widest first for serialization collapsing
        {"border",
         {"border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
          "border-top-style", "border-right-style", "border-bottom-style", "border-left-style",
          "border-top-color", "border-right-color", "border-bottom-color", "border-left-color",
          "border-image-source", "border-image-slice", "border-image-width", "border-image-outset",
          "border-image-repeat"}},
        {"border-width", {"border-top-width", "border-right-width", "border-bottom-width", "border-left-width"}},
        {"border-style", {"border-top-style", "border-right-style", "border-bottom-style", "border-left-style"}},
        {"border-color", {"border-top-color", "border-right-color", "border-bottom-color", "border-left-color"}},
        {"border-image",
         {"border-image-source", "border-image-slice", "border-image-width", "border-image-outset",
          "border-image-repeat"}},
        {"border-top", {"border-top-width", "border-top-style", "border-top-color"}},
        {"border-right", {"border-right-width", "border-right-style", "border-right-color"}},
        {"border-bottom", {"border-bottom-width", "border-bottom-style", "border-bottom-color"}},
        {"border-left", {"border-left-width", "border-left-style", "border-left-color"}},
        {"margin", {"margin-top", "margin-right", "margin-bottom", "margin-left"}},
        {"padding", {"padding-top", "padding-right", "padding-bottom", "padding-left"}},
        {"inset", {"top", "right", "bottom", "left"}},
        {"border-radius",
         {"border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius",
          "border-bottom-left-radius"}},
        {"overflow", {"overflow-x", "overflow-y"}},
        {"gap", {"row-gap", "column-gap"}},
        {"flex", {"flex-grow", "flex-shrink", "flex-basis"}},
        {"flex-flow", {"flex-direction", "flex-wrap"}},
        {"font", {"font-style", "font-variant", "font-weight", "font-size", "line-height", "font-family"}},
        {"list-style", {"list-style-position", "list-style-image", "list-style-type"}},
        {"text-decoration", {"text-decoration-line", "text-decoration-style", "text-decoration-color"}},
        {"outline", {"outline-width", "outline-style", "outline-color"}},
        {"transition",
         {"transition-property", "transition-duration", "transition-timing-function", "transition-delay"}},
        {"background",
         {"background-image", "background-position", "background-size", "background-repeat",
          "background-attachment", "background-origin", "background-clip", "background-color"}},
    };
    return s_table;
}

const InlineShorthandDef* find_inline_shorthand(const std::string& name) {
    for (const auto& def : inline_shorthands()) {
        if (def.name == name) return &def;
    }
    return nullptr;
}

bool inline_covers(const InlineShorthandDef& def, const std::string& prop) {
    return std::find(def.longhands.begin(), def.longhands.end(), prop) != def.longhands.end();
}

bool expand_inline_shorthand(const InlineShorthandDef& def, const std::string& text,
                             std::vector<std::pair<std::string, std::string>>& out) {
    out.clear();
    const std::string value = trim(text);
    if (value.empty()) return false;
    const std::string lower = ascii_lower(value);
    const std::vector<std::string> parts = split_components(value);
    if (parts.empty()) return false;

    if (parts.size() == 1 && is_global_keyword(lower)) {
        for (const auto& lh : def.longhands) out.emplace_back(lh, lower);
        return true;
    }

    const std::string& name = def.name;
    if (name == "margin" || name == "padding" || name == "border-width" || name == "border-style" ||
        name == "border-color" || name == "inset" || name == "border-radius") {
        if (parts.size() > 4) return false;
        if (name == "border-radius" && lower.find('/') != std::string::npos) return false;
        std::vector<std::string> sides;
        for (const auto& part : parts) {
            std::string normalized;
            if (!normalize_value(def.longhands[0], part, normalized, true)) return false;
            sides.push_back(normalized);
        }
        const std::string& top = sides[0];
        const std::string& right = sides.size() > 1 ? sides[1] : top;
        const std::string& bottom = sides.size() > 2 ? sides[2] : top;
        const std::string& left = sides.size() > 3 ? sides[3] : right;
        out.emplace_back(def.longhands[0], top);
        out.emplace_back(def.longhands[1], right);
        out.emplace_back(def.longhands[2], bottom);
        out.emplace_back(def.longhands[3], left);
        return true;
    }

    if (name == "overflow" || name == "gap") {
        if (parts.size() > 2) return false;
        std::string a, b;
        if (!normalize_value(def.longhands[0], parts[0], a, true)) return false;
        if (!normalize_value(def.longhands[1], parts.back(), b, true)) return false;
        out.emplace_back(def.longhands[0], a);
        out.emplace_back(def.longhands[1], b);
        return true;
    }

    if (name == "border" || name == "border-top" || name == "border-right" || name == "border-bottom" ||
        name == "border-left") {
        if (parts.size() > 3) return false;
        std::string width, style, color;
        for (const auto& part : parts) {
            std::string normalized;
            const std::string pl = ascii_lower(part);
            if (style.empty() && is_border_style(pl)) style = pl;
            else if (width.empty() && normalize_border_width(part, normalized)) width = normalized;
            else if (color.empty() && normalize_color(part, normalized)) color = normalized;
            else return false;
        }
        if (width.empty()) width = "medium";
        if (style.empty()) style = "none";
        if (color.empty()) color = "currentcolor";
        if (name == "border") {
            for (const std::string* p : {&width, &style, &color}) {
                for (int s = 0; s < 4; ++s) out.emplace_back(def.longhands[out.size()], *p);
            }
            out.emplace_back("border-image-source", "none");
            out.emplace_back("border-image-slice", "100%");
            out.emplace_back("border-image-width", "1");
            out.emplace_back("border-image-outset", "0");
            out.emplace_back("border-image-repeat", "stretch");
        } else {
            out.emplace_back(def.longhands[0], width);
            out.emplace_back(def.longhands[1], style);
            out.emplace_back(def.longhands[2], color);
        }
        return true;
    }

    if (name == "border-image") {
        if (lower == "none") {
            out.emplace_back("border-image-source", "none");
            out.emplace_back("border-image-slice", "100%");
            out.emplace_back("border-image-width", "1");
            out.emplace_back("border-image-outset", "0");
            out.emplace_back("border-image-repeat", "stretch");
            return true;
        }
        return false;
    }

    if (name == "flex-flow") {
        Parts decomposed;
        if (!decompose(name, trim(text), decomposed)) return false;
        out = std::move(decomposed);
        return true;
    }

    if (name == "flex") {
        Parts decomposed;
        if (!decompose(name, trim(text), decomposed)) return false;
        out = std::move(decomposed);
        return true;
    }

    if (name == "outline") {
        if (parts.size() > 3) return false;
        std::string width, style, color;
        for (const auto& part : parts) {
            std::string normalized;
            const std::string pl = ascii_lower(part);
            if (style.empty() && (is_border_style(pl) || pl == "auto")) style = pl;
            else if (width.empty() && normalize_border_width(part, normalized)) width = normalized;
            else if (color.empty() && (pl == "invert" || normalize_color(part, normalized))) {
                color = (pl == "invert") ? "invert" : normalized;
            } else {
                return false;
            }
        }
        out.emplace_back("outline-width", width.empty() ? "medium" : width);
        out.emplace_back("outline-style", style.empty() ? "none" : style);
        out.emplace_back("outline-color", color.empty() ? "currentcolor" : color);
        return true;
    }

    if (name == "text-decoration") {
        std::string line, style = "solid", color = "currentcolor";
        bool seen_style = false, seen_color = false;
        for (const auto& part : parts) {
            const std::string pl = ascii_lower(part);
            std::string normalized;
            if (pl == "none" || pl == "underline" || pl == "overline" || pl == "line-through" || pl == "blink") {
                if (pl == "none" && !line.empty()) return false;
                if (line == "none") return false;
                if (!line.empty()) line += ' ';
                line += pl;
            } else if (!seen_style && (pl == "solid" || pl == "double" || pl == "dotted" || pl == "dashed" || pl == "wavy")) {
                style = pl;
                seen_style = true;
            } else if (!seen_color && normalize_color(part, normalized)) {
                color = normalized;
                seen_color = true;
            } else {
                return false;
            }
        }
        out.emplace_back("text-decoration-line", line.empty() ? "none" : line);
        out.emplace_back("text-decoration-style", style);
        out.emplace_back("text-decoration-color", color);
        return true;
    }

    if (name == "list-style") {
        std::string pos, img, type;
        bool none = false;
        for (const auto& part : parts) {
            const std::string pl = ascii_lower(part);
            if (pos.empty() && (pl == "inside" || pl == "outside")) {
                pos = pl;
            } else if (pl == "none") {
                none = true;
            } else if (img.empty() && (pl.rfind("url(", 0) == 0 || pl.find("gradient(") != std::string::npos)) {
                img = canonicalize_css_value("list-style-image", part);
            } else if (type.empty()) {
                type = (part[0] == '"' || part[0] == '\'') ? part : pl;
            } else {
                return false;
            }
        }
        out.emplace_back("list-style-position", pos.empty() ? "outside" : pos);
        out.emplace_back("list-style-image", img.empty() ? "none" : img);
        out.emplace_back("list-style-type", type.empty() ? (none ? "none" : "disc") : type);
        return true;
    }

    if (name == "font") {
        std::string style = "normal", variant = "normal", weight = "normal";
        std::size_t at = 0;
        for (; at < parts.size(); ++at) {
            const std::string pl = ascii_lower(parts[at]);
            if (pl == "normal") continue;
            if (pl == "italic" || pl == "oblique") style = pl;
            else if (pl == "small-caps") variant = pl;
            else if (pl == "bold" || pl == "bolder" || pl == "lighter") weight = pl;
            else {
                std::size_t nat = 0;
                double wnum = 0;
                if (parse_css_number(pl, nat, wnum) && nat == pl.size() && wnum >= 1 && wnum <= 1000) {
                    weight = format_number(wnum);
                } else {
                    break;
                }
            }
        }
        if (at >= parts.size()) return false;
        std::string raw_size = parts[at++];
        std::string raw_line = "normal";
        const auto slash = raw_size.find('/');
        if (slash != std::string::npos) {
            raw_line = raw_size.substr(slash + 1);
            raw_size = raw_size.substr(0, slash);
        } else if (at < parts.size() && parts[at][0] == '/') {
            raw_line = parts[at++].substr(1);
        } else {
            raw_line.clear();
        }
        if (slash != std::string::npos || !raw_line.empty() || (at > 0 && parts[at - 1] == "/")) {
            if (raw_line.empty()) {
                if (at >= parts.size()) return false;
                raw_line = parts[at++];
            }
        } else {
            raw_line = "normal";
        }
        if (raw_size.empty() || at >= parts.size()) return false;
        std::string norm_size, norm_line;
        if (!normalize_value("font-size", raw_size, norm_size, true)) return false;
        if (!normalize_value("line-height", raw_line, norm_line, true)) return false;
        std::string family;
        for (; at < parts.size(); ++at) {
            if (!family.empty()) family += ' ';
            family += parts[at];
        }
        family = canonicalize_css_value("font-family", family);
        out.emplace_back("font-style", style);
        out.emplace_back("font-variant", variant);
        out.emplace_back("font-weight", weight);
        out.emplace_back("font-size", norm_size);
        out.emplace_back("line-height", norm_line);
        out.emplace_back("font-family", family);
        return true;
    }

    if (name == "transition") {
        if (has_unquoted_comma(value)) return false;
        std::string prop = "all", dur = "0s", tim = "ease", del = "0s";
        bool seen_dur = false, seen_del = false, seen_tim = false, seen_prop = false;
        for (const auto& part : parts) {
            const std::string pl = ascii_lower(part);
            std::string tnorm;
            if (normalize_css_time(pl, !seen_dur, tnorm)) {
                if (!seen_dur) { dur = tnorm; seen_dur = true; }
                else if (!seen_del) { del = tnorm; seen_del = true; }
                else return false;
            } else if (!seen_tim && (pl == "ease" || pl == "linear" || pl == "ease-in" || pl == "ease-out" ||
                                     pl == "ease-in-out" || pl == "step-start" || pl == "step-end" ||
                                     pl.rfind("cubic-bezier(", 0) == 0 || pl.rfind("steps(", 0) == 0)) {
                tim = canonicalize_css_value("transition-timing-function", part);
                seen_tim = true;
            } else if (!seen_prop) {
                prop = pl;
                seen_prop = true;
            } else {
                return false;
            }
        }
        out.emplace_back("transition-property", prop);
        out.emplace_back("transition-duration", dur);
        out.emplace_back("transition-timing-function", tim);
        out.emplace_back("transition-delay", del);
        return true;
    }

    if (name == "background") {
        if (has_unquoted_comma(value)) return false;
        std::string img = "none", pos = "0% 0%", size = "auto", rep = "repeat", att = "scroll";
        std::string orig = "padding-box", clip = "border-box", color = "transparent";
        std::vector<std::string> pos_tokens, size_tokens;
        bool after_slash = false, seen_img = false, seen_rep = false, seen_att = false, seen_color = false;
        int seen_boxes = 0;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            std::string part = parts[i];
            if (part == "/") {
                if (pos_tokens.empty() || after_slash) return false;
                after_slash = true;
                continue;
            }
            if (!after_slash) {
                const auto sl = part.find('/');
                if (sl != std::string::npos && part.find("url(") == std::string::npos) {
                    const std::string left = part.substr(0, sl);
                    const std::string right = part.substr(sl + 1);
                    if (!left.empty()) pos_tokens.push_back(ascii_lower(left));
                    after_slash = true;
                    if (!right.empty()) size_tokens.push_back(ascii_lower(right));
                    continue;
                }
            }
            const std::string pl = ascii_lower(part);
            std::string norm;
            if (after_slash) {
                if (pl == "cover" || pl == "contain" || pl == "auto") {
                    size_tokens.push_back(pl);
                } else if (normalize_length(part, norm)) {
                    size_tokens.push_back(norm);
                } else {
                    after_slash = false;
                    --i;
                }
                continue;
            }
            if (!seen_img && (pl == "none" || pl.rfind("url(", 0) == 0 || pl.find("gradient(") != std::string::npos)) {
                img = (pl == "none") ? "none" : canonicalize_css_value("background-image", part);
                seen_img = true;
            } else if (!seen_rep && (pl == "repeat" || pl == "repeat-x" || pl == "repeat-y" ||
                                     pl == "no-repeat" || pl == "space" || pl == "round")) {
                rep = pl;
                seen_rep = true;
            } else if (!seen_att && (pl == "scroll" || pl == "fixed" || pl == "local")) {
                att = pl;
                seen_att = true;
            } else if (seen_boxes < 2 && (pl == "border-box" || pl == "padding-box" || pl == "content-box" || pl == "text")) {
                if (seen_boxes == 0) { orig = (pl == "text") ? "padding-box" : pl; clip = pl; }
                else { clip = pl; }
                ++seen_boxes;
            } else if (!seen_color && normalize_color(part, norm)) {
                color = norm;
                seen_color = true;
            } else if (pl == "left" || pl == "center" || pl == "right" || pl == "top" || pl == "bottom") {
                pos_tokens.push_back(pl);
            } else if (normalize_length(part, norm)) {
                pos_tokens.push_back(norm);
            } else {
                return false;
            }
        }
        if (!pos_tokens.empty()) {
            if (pos_tokens.size() == 1) pos_tokens.push_back("center");
            pos = pos_tokens[0] + " " + pos_tokens[1];
        }
        if (!size_tokens.empty()) {
            size = size_tokens[0];
            if (size_tokens.size() > 1) size += " " + size_tokens[1];
        }
        out.emplace_back("background-image", img);
        out.emplace_back("background-position", pos);
        out.emplace_back("background-size", size);
        out.emplace_back("background-repeat", rep);
        out.emplace_back("background-attachment", att);
        out.emplace_back("background-origin", orig);
        out.emplace_back("background-clip", clip);
        out.emplace_back("background-color", color);
        return true;
    }

    return false;
}

struct Entry {
    std::string name;
    std::string value;
    bool important{false};
    /// A longhand that a listed shorthand stands for: stored, not listed.
    bool implicit{false};
};

class Block {
public:
    std::vector<Entry> entries;

    void clear() { entries.clear(); }

    const Entry* find(const std::string& name) const {
        for (const auto& entry : entries) {
            if (entry.name == name) return &entry;
        }
        return nullptr;
    }
    Entry* find(const std::string& name) {
        return const_cast<Entry*>(static_cast<const Block*>(this)->find(name));
    }

    std::size_t listed_count() const {
        std::size_t count = 0;
        for (const auto& entry : entries) {
            if (!entry.implicit) ++count;
        }
        return count;
    }
    const Entry* listed(std::size_t index) const {
        for (const auto& entry : entries) {
            if (entry.implicit) continue;
            if (index-- == 0) return &entry;
        }
        return nullptr;
    }

    static std::string serialize_inline(const InlineShorthandDef& def, const Block& block) {
        std::vector<std::string> values;
        values.reserve(def.longhands.size());
        bool first_imp = false;
        for (std::size_t i = 0; i < def.longhands.size(); ++i) {
            const Entry* e = block.find(def.longhands[i]);
            if (!e) return std::string();
            if (i == 0) first_imp = e->important;
            else if (e->important != first_imp) return std::string();
            values.push_back(e->value);
        }
        // A shorthand whose longhands all carry the same CSS-wide keyword is
        // written as that keyword (like put_together does).
        if (std::all_of(values.begin(), values.end(),
                        [&](const std::string& part) { return part == values.front(); }) &&
            is_global_keyword(ascii_lower(values.front()))) {
            return values.front();
        }
        const std::string& name = def.name;
        if (name == "margin" || name == "padding" || name == "border-width" || name == "border-style" ||
            name == "border-color" || name == "inset" || name == "border-radius") {
            return four_sides(values[0], values[1], values[2], values[3]);
        }
        if (name == "overflow" || name == "gap") {
            return values[0] == values[1] ? values[0] : values[0] + " " + values[1];
        }
        if (name == "border-image") {
            if (values[0] == "none" && values[1] == "100%" && values[2] == "1" && values[3] == "0" &&
                values[4] == "stretch") {
                return "none";
            }
            return std::string();
        }
        if (name == "border") {
            for (int part = 0; part < 3; ++part) {
                for (int side = 1; side < 4; ++side) {
                    if (values[part * 4 + side] != values[part * 4]) return std::string();
                }
            }
            if (values[12] != "none" || values[13] != "100%" || values[14] != "1" || values[15] != "0" ||
                values[16] != "stretch") {
                return std::string();
            }
            const std::string& w = values[0];
            const std::string& s = values[4];
            const std::string& c = values[8];
            if (w == "medium" && s == "none" && c == "currentcolor") return "none";
            return join_parts({w == "medium" ? std::string() : w,
                               s == "none" ? std::string() : s,
                               c == "currentcolor" ? std::string() : c});
        }
        if (name == "border-top" || name == "border-right" || name == "border-bottom" || name == "border-left" ||
            name == "outline") {
            const std::string& w = values[0];
            const std::string& s = values[1];
            const std::string& c = values[2];
            if (w == "medium" && s == "none" && c == "currentcolor") return "none";
            return join_parts({w == "medium" ? std::string() : w,
                               s == "none" ? std::string() : s,
                               c == "currentcolor" ? std::string() : c});
        }
        if (name == "flex") {
            return values[0] + " " + values[1] + " " + values[2];
        }
        if (name == "flex-flow") {
            // The shortest serialization that round-trips: nowrap is the
            // initial wrap, row the initial direction.
            if (values[1] == "nowrap") return values[0];
            if (values[0] == "row") return values[1];
            return values[0] + " " + values[1];
        }
        if (name == "font") {
            std::string out;
            if (values[0] != "normal") out += values[0] + " ";
            if (values[1] != "normal") out += values[1] + " ";
            if (values[2] != "normal") out += values[2] + " ";
            out += values[3];
            if (values[4] != "normal") out += " / " + values[4];
            return out + " " + values[5];
        }
        if (name == "list-style") {
            if (values[0] == "outside" && values[1] == "none" && values[2] == "disc") return "disc";
            std::vector<std::string> parts;
            if (values[0] != "outside") parts.push_back(values[0]);
            if (values[1] != "none") parts.push_back(values[1]);
            if (values[2] != "disc" || parts.empty()) parts.push_back(values[2]);
            std::string out;
            for (const auto& p : parts) {
                if (!out.empty()) out += ' ';
                out += p;
            }
            return out;
        }
        if (name == "text-decoration") {
            if (values[0] == "none" && values[1] == "solid" && values[2] == "currentcolor") return "none";
            return join_parts({values[0] == "none" ? std::string() : values[0],
                               values[1] == "solid" ? std::string() : values[1],
                               values[2] == "currentcolor" ? std::string() : values[2]});
        }
        if (name == "transition") {
            if (values[0] == "none" && values[1] == "0s" && values[2] == "ease" && values[3] == "0s") return "none";
            if (values[0] == "all" && values[1] == "0s" && values[2] == "ease" && values[3] == "0s") return "all";
            std::vector<std::string> parts;
            if (values[0] != "all") parts.push_back(values[0]);
            if (values[1] != "0s" || values[3] != "0s") parts.push_back(values[1]);
            if (values[2] != "ease") parts.push_back(values[2]);
            if (values[3] != "0s") parts.push_back(values[3]);
            std::string out;
            for (const auto& p : parts) {
                if (!out.empty()) out += ' ';
                out += p;
            }
            return out;
        }
        if (name == "background") {
            std::vector<std::string> parts;
            if (values[0] != "none") parts.push_back(values[0]);
            if (values[1] != "0% 0%" || values[2] != "auto") {
                std::string pos = values[1];
                if (values[2] != "auto") pos += " / " + values[2];
                parts.push_back(pos);
            }
            if (values[3] != "repeat") parts.push_back(values[3]);
            if (values[4] != "scroll") parts.push_back(values[4]);
            if (values[5] != "padding-box" || values[6] != "border-box") {
                parts.push_back(values[5]);
                if (values[6] != values[5]) parts.push_back(values[6]);
            }
            if (values[7] != "transparent") parts.push_back(values[7]);
            if (parts.empty()) return "none";
            std::string out;
            for (const auto& p : parts) {
                if (!out.empty()) out += ' ';
                out += p;
            }
            return out;
        }
        return std::string();
    }

    // The value of a property, a shorthand's put together from its parts.
    std::string get(const std::string& name, bool inline_mode = false) const {
        if (inline_mode) {
            if (const InlineShorthandDef* def = find_inline_shorthand(name)) {
                if (const Entry* pending = find(name)) return pending->value;
                return serialize_inline(*def, *this);
            }
            if (!parts_of(name) && !find_shorthand(name)) {
                const Entry* entry = find(name);
                return entry ? entry->value : std::string();
            }
            // Otherwise: assemble from the longhands like the non-inline
            // case does (the shorthand machinery wrote them).
        }
        // A declaration whose var() the grammar could not look through is
        // kept whole under the shorthand's name: no longhands exist, and the
        // declaration reads back as written.
        if (const Shorthand* shorthand = find_shorthand(name)) {
            if (const Entry* marker = find(name)) {
                bool any_longhand = false;
                for (const auto& longhand : shorthand->longhands) {
                    if (find(longhand)) { any_longhand = true; break; }
                }
                if (!any_longhand) return marker->value;
            }
        }
        if (const std::vector<std::string>* parts = parts_of(name)) {
            std::vector<std::string> values;
            values.reserve(parts->size());
            bool all_known = true;
            for (const auto& longhand : *parts) {
                const Entry* entry = find(longhand);
                if (!entry || entry->value.empty()) {
                    all_known = false;
                    break;
                }
                values.push_back(entry->value);
            }
            if (all_known) {
                const std::string assembled = put_together(name, values);
                if (!assembled.empty()) return assembled;
            }
            const Entry* entry = find(name);
            return entry ? entry->value : std::string();
        }
        const Shorthand* shorthand = find_shorthand(name);
        if (!shorthand) {
            const Entry* entry = find(name);
            return entry ? entry->value : std::string();
        }
        if (shorthand->kind == ShorthandKind::Background) {
            const Entry* entry = find(name);
            if (entry) return entry->value;
            return std::string();
        }
        std::vector<std::string> values;
        for (const auto& longhand : shorthand->longhands) {
            const Entry* entry = find(longhand);
            if (!entry) return std::string();
            values.push_back(entry->value);
        }
        switch (shorthand->kind) {
            case ShorthandKind::GridLinePair: {
                const std::string& start = values[0];
                const std::string& end = values[1];
                if (start.empty()) return std::string();
                if (end == litebird::grid_recoverable_end(start)) return start;
                return start + " / " + end;
            }
            case ShorthandKind::GridArea: {
                std::vector<std::string> lines = {values[0], values[1], values[2], values[3]};
                for (const auto& line : lines) {
                    if (line.empty()) return std::string();
                }
                if (lines[3] == litebird::grid_recoverable_end(lines[1])) lines.resize(3);
                if (lines.size() == 3 && lines[2] == litebird::grid_recoverable_end(lines[0])) lines.resize(2);
                if (lines.size() == 2 && lines[1] == litebird::grid_recoverable_end(lines[0])) lines.resize(1);
                std::string out = lines[0];
                for (std::size_t i = 1; i < lines.size(); ++i) out += " / " + lines[i];
                return out;
            }
            case ShorthandKind::GridTemplate: {
                const std::string& rows = values[0];
                const std::string& cols = values[1];
                const std::string& areas = values[2];
                const auto global = [](const std::string& v) {
                    const std::string lower = ascii_lower(v);
                    return lower == "inherit" || lower == "initial" || lower == "unset" || lower == "revert" ||
                           lower == "revert-layer";
                };
                if (global(rows) || global(cols) || global(areas)) return std::string();
                if (areas == "none") {
                    if (rows == "none" && cols == "none") return "none";
                    return rows + " / " + cols;
                }
                std::string composed;
                if (!litebird::grid_template_from_longhands(rows, cols, areas, composed)) return std::string();
                return composed;
            }
            case ShorthandKind::Grid: {
                // Serializes whenever the longhands stand for a value the
                // shorthand can round-trip; representability is what licenses
                // it, not whether the shorthand was declared.
                const std::string& rows = values[0];
                const std::string& cols = values[1];
                const std::string& areas = values[2];
                const std::string& auto_rows = values[3];
                const std::string& auto_cols = values[4];
                const std::string& auto_flow = values[5];
                const auto global = [](const std::string& v) {
                    const std::string lower = ascii_lower(v);
                    return lower == "inherit" || lower == "initial" || lower == "unset" || lower == "revert" ||
                           lower == "revert-layer";
                };
                if (global(rows) || global(cols) || global(areas) || global(auto_rows) || global(auto_cols) ||
                    global(auto_flow)) {
                    return std::string();
                }
                const bool dense = auto_flow == "dense" || auto_flow == "column dense";
                if (auto_flow == "row" && auto_rows == "auto" && auto_cols == "auto") {
                    // The <grid-template> form round-trips: an auto-flow grid
                    // with initial implicit-track sizes is rewritten to the
                    // equivalent template.
                    if (areas == "none") {
                        if (rows == "none" && cols == "none") return "none"; // everything at its initial value
                        return rows + " / " + cols;
                    }
                    std::string composed;
                    if (!litebird::grid_template_from_longhands(rows, cols, areas, composed)) return std::string();
                    return composed;
                }
                // The auto-flow forms cannot express an areas declaration.
                if (areas != "none") return std::string();
                if (auto_flow == "row" || auto_flow == "dense") {
                    // [ auto-flow && dense? ] <grid-auto-rows>? / <grid-template-columns>
                    if (rows == "none" && auto_cols == "auto") {
                        std::string side = dense ? "auto-flow dense" : "auto-flow";
                        if (auto_rows != "auto") side += ' ' + auto_rows;
                        return side + " / " + cols;
                    }
                    return std::string();
                }
                if (auto_flow == "column" || auto_flow == "column dense") {
                    // <grid-template-rows> / [ auto-flow && dense? ] <grid-auto-columns>?
                    if (cols == "none" && auto_rows == "auto") {
                        std::string side = dense ? "auto-flow dense" : "auto-flow";
                        if (auto_cols != "auto") side += ' ' + auto_cols;
                        return rows + " / " + side;
                    }
                    return std::string();
                }
                return std::string();
            }
            case ShorthandKind::FourSides:
                return four_sides(values[0], values[1], values[2], values[3]);
            case ShorthandKind::Side:
                return join_parts({values[0], values[1], values[2]});
            case ShorthandKind::Border: {
                // Only when the four sides agree is there one value to give.
                for (int part = 0; part < 3; ++part) {
                    for (int side = 1; side < 4; ++side) {
                        if (values[part * 4 + side] != values[part * 4]) return std::string();
                    }
                }
                return join_parts({values[0], values[4], values[8]});
            }
            case ShorthandKind::FlexFlow:
            case ShorthandKind::Flex:
                // Not reachable: the parts_of() branch above returns first.
                break;
            case ShorthandKind::Background:
                break;
        }
        return std::string();
    }

    bool is_important(const std::string& name, bool inline_mode = false) const {
        if (inline_mode) {
            if (const InlineShorthandDef* def = find_inline_shorthand(name)) {
                if (const Entry* entry = find(name)) return entry->important;
                for (const auto& longhand : def->longhands) {
                    const Entry* entry = find(longhand);
                    if (!entry || !entry->important) return false;
                }
                return true;
            }
        } else if (const Shorthand* shorthand = find_shorthand(name)) {
            if (const Entry* entry = find(name)) return entry->important;
            for (const auto& longhand : shorthand->longhands) {
                const Entry* entry = find(longhand);
                if (!entry || !entry->important) return false;
            }
            return true;
        }
        const Entry* entry = find(name);
        return entry && entry->important;
    }

    // Stores a declaration. False when the value is not one the property
    // accepts, in which case nothing changes.
    bool set(const std::string& name, const std::string& text, bool important, bool inline_mode = false) {
        if (inline_mode) {
            if (const InlineShorthandDef* def = find_inline_shorthand(name)) {
                const std::string trimmed = trim(text);
                if (trimmed.empty()) return false;
                if (ascii_lower(trimmed).find("var(") != std::string::npos) {
                    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Entry& e) {
                        return inline_covers(*def, e.name);
                    }), entries.end());
                    store(name, trimmed, important, false);
                    return true;
                }
                std::vector<std::pair<std::string, std::string>> expanded;
                if (!expand_inline_shorthand(*def, trimmed, expanded)) return false;
                entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Entry& e) {
                    if (e.name == name) return true;
                    const InlineShorthandDef* other = find_inline_shorthand(e.name);
                    return other != nullptr && std::any_of(other->longhands.begin(), other->longhands.end(),
                                                           [&](const std::string& lh) { return inline_covers(*def, lh); });
                }), entries.end());
                for (const auto& [lh, val] : expanded) store(lh, val, important, false);
                return true;
            }
        }
        if (const Shorthand* shorthand = find_shorthand(name)) {
            std::vector<std::string> values;
            std::string raw;
            bool understood = expand(*shorthand, text, values, raw);
            bool kept_whole = false;
            if (!understood) {
                // A var() the grammar cannot look through is kept as it came,
                // like any longhand with a var() in it; its longhands are not
                // known. Everything else is invalid.
                if (ascii_lower(trim(text)).find("var(") == std::string::npos) return false;
                raw = trim(text);
                kept_whole = true;
            }
            const bool covered = covered_by_listed(*shorthand);
            if (!covered) {
                // What this shorthand stands for is no longer listed on its own.
                std::size_t position = entries.size();
                for (std::size_t i = 0; i < entries.size(); ++i) {
                    if (entries[i].name == name) {
                        position = i;
                        break;
                    }
                }
                Entry listed{name, raw, important, false};
                if (position < entries.size()) entries[position] = listed;
                else entries.push_back(listed);
                entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Entry& entry) {
                    if (entry.name == name) return false;
                    if (covers(*shorthand, entry.name)) return true;
                    const Shorthand* other = find_shorthand(entry.name);
                    return other && is_part_of(*other, *shorthand);
                }), entries.end());
            }
            if (!kept_whole) {
                for (std::size_t i = 0; i < shorthand->longhands.size(); ++i) {
                    store(shorthand->longhands[i], values[i], important, true);
                }
                // The listed entry stands for the longhands: give it the
                // assembled (canonical) text, falling back to the
                // canonicalizer on the declaration as written, so reads of
                // the shorthand and the attribute text agree.
                std::string assembled = get(name, inline_mode);
                if (assembled.empty()) {
                    std::string canonical;
                    if (grid_property_canonical(name, trim(text), canonical)) assembled = canonical;
                }
                if (assembled.empty()) assembled = trim(text);
                if (Entry* marker = find(name)) marker->value = assembled;
            }
            return true;
        }
        const std::string trimmed = trim(text);
        if (parts_of(name)) {
            Parts decomposed;
            const bool understood = take_apart(name, trimmed, decomposed);
            const bool kept_whole = !understood && ascii_lower(trimmed).find("var(") != std::string::npos;
            if (!understood && !kept_whole) return false;
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                                         [&](const Entry& kept) {
                                             return kept.name == name || covered_by(name, kept.name);
                                         }),
                          entries.end());
            entries.push_back({name, trimmed, important, false});
            for (const auto& [longhand, value] : decomposed) store(longhand, value, important, true);
            return true;
        }
        std::string value;
        if (!normalize_value(name, text, value, inline_mode)) return false;
        bool covered = false;
        for (const auto& shorthand : shorthands()) {
            if (covers(shorthand, name) && find(shorthand.name)) {
                covered = true;
                if (shorthand.kind == ShorthandKind::Background) find(shorthand.name)->value = value;
            }
        }
        if (inline_mode) {
            // A longhand written on its own no longer stands for the inline
            // shorthand that covered it.
            entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Entry& e) {
                const InlineShorthandDef* def = find_inline_shorthand(e.name);
                return def != nullptr && inline_covers(*def, name);
            }), entries.end());
        }
        store(name, value, important, covered);
        return true;
    }

    // A listed shorthand whose longhands are no longer all there cannot be
    // written as one value any more: it is unfolded into the longhands that
    // remain, which become listed themselves. A marker with no longhands at
    // all is kept: it stands for a declaration the grammar kept whole (a var()).
    void unfold_dead_shorthands() {
        const std::vector<Shorthand> table = shorthands();
        for (std::size_t i = 0; i < entries.size();) {
            const std::string listed_name = entries[i].name;
            const Shorthand* shorthand = nullptr;
            for (const auto& candidate : table) {
                if (candidate.name == listed_name) { shorthand = &candidate; break; }
            }
            if (!shorthand) { ++i; continue; }
            std::size_t present = 0;
            for (const auto& longhand : shorthand->longhands) {
                if (find(longhand)) ++present;
            }
            if (present == shorthand->longhands.size() || present == 0) { ++i; continue; }
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                                         [&](const Entry& e) { return e.name == listed_name; }),
                          entries.end());
        }
        for (auto& other : entries) {
            for (const auto& candidate : table) {
                if (covers(candidate, other.name)) {
                    const Entry* marker = find(candidate.name);
                    if (!marker) {
                        // The listed shorthand is gone; the parts it covered are listed themselves.
                        for (auto& candidate2 : entries) {
                            if (covers(candidate, candidate2.name)) candidate2.implicit = false;
                        }
                    }
                }
            }
        }
    }

    // Removes a property; returns the value it had.
    std::string remove(const std::string& name, bool inline_mode = false) {
        const std::string old = remove_impl(name, inline_mode);
        unfold_dead_shorthands();
        return old;
    }

    std::string remove_impl(const std::string& name, bool inline_mode = false) {
        const std::string old = get(name, inline_mode);
        if (inline_mode) {
            if (const InlineShorthandDef* def = find_inline_shorthand(name)) {
                entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Entry& e) {
                    return e.name == name || inline_covers(*def, e.name);
                }), entries.end());
                return old;
            }
            // Otherwise: the shared longhand bookkeeping below (removing one
            // part of a listed shorthand un-marks the rest).
        }
        if (const std::vector<std::string>* parts = parts_of(name)) {
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                                         [&](const Entry& entry) {
                                             if (entry.name == name) return true;
                                             for (const auto& longhand : *parts) {
                                                 if (entry.name == longhand) return true;
                                             }
                                             return false;
                                         }),
                          entries.end());
            return old;
        }
        if (const Shorthand* shorthand = find_shorthand(name)) {
            entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Entry& entry) {
                if (entry.name == name || covers(*shorthand, entry.name)) return true;
                const Shorthand* other = find_shorthand(entry.name);
                return other && is_part_of(*other, *shorthand);
            }), entries.end());
            return old;
        }
        Entry* entry = find(name);
        if (!entry) return old;
        if (entry->implicit) {
            // One part of a listed shorthand goes: the shorthand can no
            // longer stand for the rest, which are listed themselves from now on.
            for (const auto& shorthand : shorthands()) {
                if (!covers(shorthand, name)) continue;
                entries.erase(std::remove_if(entries.begin(), entries.end(),
                                             [&](const Entry& listed) { return listed.name == shorthand.name; }),
                              entries.end());
                for (auto& other : entries) {
                    if (covers(shorthand, other.name)) other.implicit = false;
                }
            }
        }
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const Entry& candidate) { return candidate.name == name; }),
                      entries.end());
        return old;
    }

    std::string text(bool inline_mode = false) const {
        std::string out;
        auto write = [&](const std::string& name, const std::string& value, bool important) {
            if (!out.empty()) out += ' ';
            out += name + ": " + value + (important ? " !important;" : ";");
        };
        if (inline_mode) {
            std::unordered_set<std::string> emitted;
            // CSSOM serialization prints the shortest form the declaration
            // can be expressed in: the six grid longhands are one `grid`
            // whenever they are all present and the value round-trips. (The
            // `grid` marker exists while the shorthand stands; this is for
            // the unfolded form left when a part of it was removed and set
            // again.)
            if (!find("grid")) {
                static const char* const kGridLonghands[] = {"grid-template-rows", "grid-template-columns",
                                                             "grid-template-areas", "grid-auto-rows",
                                                             "grid-auto-columns", "grid-auto-flow"};
                bool all_present = true;
                bool important = false;
                for (const char* longhand : kGridLonghands) {
                    const Entry* part = find(longhand);
                    if (!part) { all_present = false; break; }
                    important = important || part->important;
                }
                if (all_present) {
                    const std::string assembled = get("grid", /*inline_mode=*/true);
                    if (!assembled.empty()) {
                        write("grid", assembled, important);
                        for (const char* longhand : kGridLonghands) emitted.insert(longhand);
                        emitted.insert("grid-template"); // the nested marker stands for the same tracks
                    }
                }
            }
            for (const auto& entry : entries) {
                if (entry.implicit || emitted.count(entry.name)) continue;
                bool collapsed = false;
                for (const auto& def : inline_shorthands()) {
                    if (!inline_covers(def, entry.name)) continue;
                    bool already = false;
                    for (const auto& lh : def.longhands) {
                        if (emitted.count(lh)) { already = true; break; }
                    }
                    if (already) continue;
                    const std::string serialized = serialize_inline(def, *this);
                    if (serialized.empty()) continue;
                    write(def.name, serialized, entry.important);
                    for (const auto& lh : def.longhands) emitted.insert(lh);
                    collapsed = true;
                    break;
                }
                if (!collapsed) {
                    const Shorthand* shorthand = find_shorthand(entry.name);
                    std::string value = shorthand ? get(entry.name) : entry.value;
                    if (value.empty()) value = entry.value;
                    write(entry.name, value, entry.important);
                    emitted.insert(entry.name);
                }
            }
            return out;
        }
        for (const auto& entry : entries) {
            if (entry.implicit) continue;
            const Shorthand* shorthand = find_shorthand(entry.name);
            if (!shorthand) {
                write(entry.name, entry.value, entry.important);
                continue;
            }
            const std::string value = get(entry.name);
            if (!value.empty()) {
                write(entry.name, value, entry.important);
                continue;
            }
            if (entry.name == "grid") {
                // The grid shorthand no longer serializes: try the nested
                // grid-template shorthand for the track part, then the rest.
                const std::string template_value = get("grid-template");
                if (!template_value.empty()) write("grid-template", template_value, entry.important);
                for (const char* longhand : {"grid-auto-rows", "grid-auto-columns", "grid-auto-flow"}) {
                    const Entry* part = find(longhand);
                    if (part && !part->value.empty()) write(longhand, part->value, part->important);
                }
                continue;
            }
            // The parts no longer fit one value: they are written out.
            for (const auto& longhand : shorthand->longhands) {
                const Entry* part = find(longhand);
                if (part && !part->value.empty()) write(longhand, part->value, part->important);
            }
        }
        return out;
    }

private:
    static std::string join_parts(std::initializer_list<std::string> parts) {
        std::string out;
        for (const auto& part : parts) {
            if (part.empty()) continue;
            if (!out.empty()) out += ' ';
            out += part;
        }
        return out;
    }

    static std::string four_sides(const std::string& top, const std::string& right, const std::string& bottom,
                                  const std::string& left) {
        if (top.empty() || right.empty() || bottom.empty() || left.empty()) return std::string();
        if (left == right) {
            if (top == bottom) return top == right ? top : top + " " + right;
            return top + " " + right + " " + bottom;
        }
        return top + " " + right + " " + bottom + " " + left;
    }

    // True when every longhand of `inner` is one of `outer`'s.
    static bool is_part_of(const Shorthand& inner, const Shorthand& outer) {
        if (&inner == &outer) return false;
        for (const auto& longhand : inner.longhands) {
            if (!covers(outer, longhand)) return false;
        }
        return true;
    }

    bool covered_by_listed(const Shorthand& shorthand) const {
        for (const auto& other : shorthands()) {
            if (is_part_of(shorthand, other) && find(other.name)) return true;
        }
        return false;
    }

    void store(const std::string& name, const std::string& value, bool important, bool implicit) {
        if (Entry* existing = find(name)) {
            existing->value = value;
            existing->important = important;
            existing->implicit = implicit;
            return;
        }
        entries.push_back({name, value, important, implicit});
    }

    // Splits "a / b / c" into its lines (the lines never contain "/").
    static std::vector<std::string> split_slashes(const std::string& text) {
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (true) {
            const std::size_t slash = text.find(" / ", start);
            if (slash == std::string::npos) {
                parts.push_back(trim(text.substr(start)));
                break;
            }
            parts.push_back(trim(text.substr(start, slash - start)));
            start = slash + 3;
        }
        return parts;
    }

    // The values of a shorthand's longhands, in the order of its table entry.
    static bool expand(const Shorthand& shorthand, const std::string& text, std::vector<std::string>& values,
                       std::string& raw) {
        const std::string value = trim(text);
        if (value.empty()) return false;
        const std::vector<std::string> parts = split_components(value);
        if (parts.empty()) return false;
        raw.clear();
        switch (shorthand.kind) {
            case ShorthandKind::FourSides: {
                if (parts.size() > 4) return false;
                std::vector<std::string> sides;
                for (std::size_t i = 0; i < parts.size(); ++i) {
                    std::string normalized;
                    if (!normalize_value(shorthand.longhands[0], parts[i], normalized)) return false;
                    sides.push_back(normalized);
                }
                const std::string& top = sides[0];
                const std::string& right = sides.size() > 1 ? sides[1] : top;
                const std::string& bottom = sides.size() > 2 ? sides[2] : top;
                const std::string& left = sides.size() > 3 ? sides[3] : right;
                values = {top, right, bottom, left};
                return true;
            }
            case ShorthandKind::Side:
            case ShorthandKind::Border: {
                if (parts.size() > 3) return false;
                std::string width, style, color;
                for (const auto& part : parts) {
                    std::string normalized;
                    const std::string lower = ascii_lower(part);
                    if (style.empty() && is_border_style(lower)) style = lower;
                    else if (width.empty() && normalize_border_width(part, normalized)) width = normalized;
                    else if (color.empty() && normalize_color(part, normalized)) color = normalized;
                    else return false;
                }
                if (shorthand.kind == ShorthandKind::Side) {
                    values = {width, style, color};
                } else {
                    values.clear();
                    for (const std::string* part : {&width, &style, &color}) {
                        for (int side = 0; side < 4; ++side) values.push_back(*part);
                    }
                }
                return true;
            }
            case ShorthandKind::FlexFlow: {
                Parts decomposed;
                if (!decompose("flex-flow", value, decomposed)) return false;
                values.clear();
                for (const char* longhand : {"flex-direction", "flex-wrap"}) {
                    for (const auto& [name, part] : decomposed) {
                        if (name == longhand) values.push_back(part);
                    }
                }
                return values.size() == 2;
            }
            case ShorthandKind::Flex: {
                Parts decomposed;
                if (!decompose("flex", value, decomposed)) return false;
                values.clear();
                for (const char* longhand : {"flex-grow", "flex-shrink", "flex-basis"}) {
                    for (const auto& [name, part] : decomposed) {
                        if (name == longhand) values.push_back(part);
                    }
                }
                return values.size() == 3;
            }
            case ShorthandKind::GridLinePair: {
                std::string canonical;
                if (!litebird::grid_line_pair(value, canonical)) return false;
                const std::vector<std::string> lines = split_slashes(canonical);
                const std::string first = lines[0];
                const std::string second = lines.size() == 2 ? lines[1] : litebird::grid_recoverable_end(first);
                values = {first, second};
                return true;
            }
            case ShorthandKind::GridArea: {
                std::string canonical;
                if (!litebird::grid_area_value(value, canonical)) return false;
                const std::vector<std::string> lines = split_slashes(canonical);
                const std::size_t n = lines.size();
                std::string rs = lines[0];
                std::string cs, re, ce;
                if (n == 1) {
                    cs = litebird::grid_recoverable_end(rs);
                    re = litebird::grid_recoverable_end(rs);
                    ce = litebird::grid_recoverable_end(rs);
                } else if (n == 2) {
                    cs = lines[1];
                    re = litebird::grid_recoverable_end(rs);
                    ce = litebird::grid_recoverable_end(cs);
                } else if (n == 3) {
                    cs = lines[1];
                    re = lines[2];
                    ce = litebird::grid_recoverable_end(cs);
                } else {
                    cs = lines[1];
                    re = lines[2];
                    ce = lines[3];
                }
                values = {rs, cs, re, ce}; // row-start, column-start, row-end, column-end
                return true;
            }
            case ShorthandKind::GridTemplate: {
                std::string rows, columns, areas;
                if (!litebird::grid_template_longhands(value, rows, columns, areas)) return false;
                values = {rows, columns, areas};
                return true;
            }
            case ShorthandKind::Grid: {
                std::string rows, columns, areas, auto_rows, auto_columns, auto_flow;
                if (!litebird::grid_shorthand_longhands(value, rows, columns, areas, auto_rows, auto_columns,
                                                        auto_flow)) {
                    return false;
                }
                values = {rows, columns, areas, auto_rows, auto_columns, auto_flow};
                return true;
            }
            case ShorthandKind::Background: {
                raw = value;
                std::string color;
                for (const auto& part : parts) {
                    std::string normalized;
                    if (normalize_color(part, normalized)) color = normalized;
                }
                if (parts.size() == 1 && !color.empty()) raw = color;
                values = {color};
                return true;
            }
        }
        return false;
    }
};

// ---------------------------------------------------------------------------
// Parsing declarations
// ---------------------------------------------------------------------------

struct RawDeclaration {
    std::string name;
    std::string value;
    bool important{false};
};

std::vector<RawDeclaration> parse_declarations(const std::string& text) {
    std::vector<RawDeclaration> out;
    std::string current;
    int depth = 0;
    char quote = 0;
    auto finish = [&]() {
        const auto colon = current.find(':');
        if (colon != std::string::npos) {
            RawDeclaration declaration;
            declaration.name = trim(current.substr(0, colon));
            declaration.value = trim(current.substr(colon + 1));
            if (!is_custom_property(declaration.name)) declaration.name = canonicalize_property_name(ascii_lower(declaration.name));
            // "!important", in any case, with or without space after the "!".
            const auto bang = declaration.value.rfind('!');
            if (bang != std::string::npos && ascii_lower(trim(declaration.value.substr(bang + 1))) == "important") {
                declaration.important = true;
                declaration.value = trim(declaration.value.substr(0, bang));
            }
            if (!declaration.name.empty() && !declaration.value.empty()) out.push_back(std::move(declaration));
        }
        current.clear();
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote) {
            current += c;
            if (c == '\\' && i + 1 < text.size()) current += text[++i];
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            const auto end = text.find("*/", i + 2);
            if (end == std::string::npos) break;
            i = end + 1;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            current += c;
        } else if (c == '(') {
            ++depth;
            current += c;
        } else if (c == ')') {
            if (depth > 0) --depth;
            current += c;
        } else if (c == ';' && depth == 0) {
            finish();
        } else {
            current += c;
        }
    }
    finish();
    return out;
}

void apply_declarations(Block& block, const std::string& text, bool inline_mode = false) {
    for (const auto& declaration : parse_declarations(text)) {
        if (!is_css_property(declaration.name)) continue;
        block.set(declaration.name, declaration.value, declaration.important, inline_mode);
    }
}

// ---------------------------------------------------------------------------
// The computed style
// ---------------------------------------------------------------------------

struct SheetRule {
    SelectorList selector;
    std::vector<RawDeclaration> declarations;
};

struct ParsedSheet {
    std::vector<SheetRule> rules;
};

void parse_rules(const std::string& css, std::size_t& at, std::vector<SheetRule>& out, bool nested) {
    while (at < css.size()) {
        // The prelude: up to "{", ";" or the "}" that ends the block we are in.
        std::string prelude;
        bool opened = false;
        while (at < css.size()) {
            const char c = css[at];
            if (c == '/' && at + 1 < css.size() && css[at + 1] == '*') {
                const auto end = css.find("*/", at + 2);
                at = end == std::string::npos ? css.size() : end + 2;
                continue;
            }
            if (c == '{') {
                opened = true;
                ++at;
                break;
            }
            if (c == ';') {
                ++at;
                prelude.clear();
                continue;
            }
            if (c == '}') {
                ++at;
                if (nested) return;
                prelude.clear();
                continue;
            }
            prelude += c;
            ++at;
        }
        if (!opened) return;
        prelude = trim(prelude);
        const std::string lower = ascii_lower(prelude);
        if (!prelude.empty() && prelude[0] == '@') {
            const bool applies = (lower.compare(0, 6, "@media") == 0 && lower.find("print") == std::string::npos) ||
                                 lower.compare(0, 9, "@supports") == 0 || lower.compare(0, 6, "@layer") == 0;
            if (applies) {
                parse_rules(css, at, out, true);
            } else {
                // Skip the block of a rule that is not understood.
                int depth = 1;
                while (at < css.size() && depth > 0) {
                    if (css[at] == '{') ++depth;
                    else if (css[at] == '}') --depth;
                    ++at;
                }
            }
            continue;
        }
        std::string body;
        int depth = 1;
        while (at < css.size()) {
            const char c = css[at++];
            if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) break;
            body += c;
        }
        SheetRule rule;
        if (!SelectorList::parse(prelude, rule.selector)) continue;
        rule.declarations = parse_declarations(body);
        out.push_back(std::move(rule));
    }
}

struct StylePart : RealmPart {
    std::unordered_map<std::string, std::shared_ptr<ParsedSheet>> sheets;
    /// The <style> elements of the tree `root`, and their rules, at `version`.
    std::vector<std::shared_ptr<Element>> style_elements;
    std::vector<std::shared_ptr<ParsedSheet>> style_sheets;
    uint64_t version{0};
    const Node* root{nullptr};
    /// The rules of kElementDefaults.
    std::shared_ptr<ParsedSheet> defaults;
};

std::shared_ptr<ParsedSheet> sheet_for(Realm& realm, const std::string& css) {
    StylePart& part = realm.part<StylePart>();
    auto found = part.sheets.find(css);
    if (found != part.sheets.end()) return found->second;
    auto sheet = std::make_shared<ParsedSheet>();
    std::size_t at = 0;
    parse_rules(css, at, sheet->rules, false);
    if (part.sheets.size() >= 64) part.sheets.clear();
    part.sheets[css] = sheet;
    return sheet;
}

const std::vector<std::shared_ptr<Element>>& style_elements_of(Realm& realm, const Node& root) {
    StylePart& part = realm.part<StylePart>();
    if (part.version == Node::tree_version() && part.root == &root) return part.style_elements;
    part.style_elements.clear();
    for_each_descendant(root, [&](Node& node) {
        if (is_html_element(node, "style")) {
            auto& element = static_cast<Element&>(node);
            const std::string media = ascii_lower(element.get_attribute("media"));
            if (media.find("print") == std::string::npos) part.style_elements.push_back(shared(element));
        }
        return true;
    });
    // Parsed once for each state of the tree, not once for each element asked about.
    part.style_sheets.clear();
    for (const auto& style : part.style_elements) part.style_sheets.push_back(sheet_for(realm, style->text_content()));
    part.version = Node::tree_version();
    part.root = &root;
    return part.style_elements;
}

// The top of the tree a node is in. A loop over the ancestors: the callers
// that go up the tree themselves find it once and pass it on, or asking about
// an element two hundred thousand levels down would take the square of that.
const Node* tree_root_of(const Node& node) {
    const Node* root = &node;
    std::shared_ptr<Node> hold;
    while (auto parent = root->parent()) {
        hold = parent;
        root = hold.get();
    }
    return root;
}

const char* default_display(const Element& element) {
    if (element.is_foreign()) return "inline";
    static const std::unordered_map<std::string, const char*> s_table = [] {
        std::unordered_map<std::string, const char*> table;
        for (const char* tag : {"html", "body", "div", "p", "ul", "ol", "dl", "dt", "dd", "h1", "h2", "h3", "h4",
                                "h5", "h6", "section", "article", "aside", "header", "footer", "nav", "main",
                                "form", "fieldset", "legend", "pre", "blockquote", "figure", "figcaption", "hr",
                                "address", "details", "summary", "center", "dir", "menu", "hgroup", "optgroup",
                                "option", "search", "dialog", "listing", "xmp", "plaintext", "frameset", "frame"}) {
            table[tag] = "block";
        }
        for (const char* tag : {"head", "script", "style", "title", "meta", "link", "base", "template", "noscript",
                                "area", "datalist", "param", "source", "track", "noembed", "noframes", "rp"}) {
            table[tag] = "none";
        }
        for (const char* tag : {"input", "button", "select", "textarea", "meter", "progress", "img", "video",
                                "canvas", "object", "embed", "iframe", "marquee"}) {
            table[tag] = "inline-block";
        }
        table["li"] = "list-item";
        table["table"] = "table";
        table["tr"] = "table-row";
        table["td"] = "table-cell";
        table["th"] = "table-cell";
        table["thead"] = "table-header-group";
        table["tbody"] = "table-row-group";
        table["tfoot"] = "table-footer-group";
        table["caption"] = "table-caption";
        table["col"] = "table-column";
        table["colgroup"] = "table-column-group";
        table["ruby"] = "ruby";
        table["rt"] = "ruby-text";
        table["img"] = "inline";
        table["video"] = "inline";
        table["canvas"] = "inline";
        table["object"] = "inline";
        table["embed"] = "inline";
        table["iframe"] = "inline";
        return table;
    }();
    auto found = s_table.find(element.tag_name());
    return found == s_table.end() ? "inline" : found->second;
}

bool is_inherited(const std::string& property) {
    static const std::unordered_set<std::string> s_inherited = {
        "visibility", "color", "cursor", "direction", "font", "font-family", "font-size", "font-style",
        "font-variant", "font-weight", "letter-spacing", "line-height", "list-style", "list-style-image",
        "list-style-position", "list-style-type", "pointer-events", "quotes", "text-align", "text-indent",
        "text-transform", "white-space", "word-spacing", "word-break", "overflow-wrap", "border-collapse",
        "border-spacing", "caption-side", "tab-size", "writing-mode", "text-rendering", "caret-color",
        "accent-color", "fill", "stroke", "text-shadow", "hyphens", "empty-cells", "orphans", "widows",
        "text-align-last", "font-kerning", "font-stretch", "color-scheme",
    };
    return is_custom_property(property) || s_inherited.count(property) != 0;
}

// What a property is when nothing says otherwise, as its computed value is
// written. "" for a property this table does not have: the answer to a
// script is then the empty string, which says "not known" and is better than
// a value made up.
const char* initial_value(const std::string& property) {
    static const std::unordered_map<std::string, const char*> s_initial = {
        {"accent-color", "auto"}, {"align-content", "normal"}, {"align-items", "normal"}, {"align-self", "auto"},
        {"animation-delay", "0s"}, {"animation-direction", "normal"}, {"animation-duration", "0s"},
        {"animation-fill-mode", "none"}, {"animation-iteration-count", "1"}, {"animation-name", "none"},
        {"animation-play-state", "running"}, {"animation-timing-function", "ease"}, {"appearance", "none"},
        {"aspect-ratio", "auto"}, {"backdrop-filter", "none"}, {"backface-visibility", "visible"},
        {"background-attachment", "scroll"}, {"background-blend-mode", "normal"}, {"background-clip", "border-box"},
        {"background-color", "rgba(0, 0, 0, 0)"}, {"background-image", "none"}, {"background-origin", "padding-box"},
        {"background-position", "0% 0%"}, {"background-repeat", "repeat"}, {"background-size", "auto"},
        {"block-size", "auto"}, {"border-bottom-color", "currentcolor"}, {"border-left-color", "currentcolor"},
        {"border-right-color", "currentcolor"}, {"border-top-color", "currentcolor"}, {"border-bottom-style", "none"},
        {"border-left-style", "none"}, {"border-right-style", "none"}, {"border-top-style", "none"},
        {"border-bottom-width", "medium"}, {"border-left-width", "medium"}, {"border-right-width", "medium"},
        {"border-top-width", "medium"}, {"border-bottom-left-radius", "0px"}, {"border-bottom-right-radius", "0px"},
        {"border-top-left-radius", "0px"}, {"border-top-right-radius", "0px"}, {"border-collapse", "separate"},
        {"border-spacing", "0px 0px"}, {"bottom", "auto"}, {"box-shadow", "none"}, {"box-sizing", "content-box"},
        {"break-after", "auto"}, {"break-before", "auto"}, {"break-inside", "auto"}, {"caption-side", "top"},
        {"caret-color", "currentcolor"}, {"clear", "none"}, {"clip", "auto"}, {"clip-path", "none"},
        {"color", "rgb(0, 0, 0)"}, {"column-count", "auto"}, {"column-gap", "normal"},
        {"column-rule-color", "currentcolor"}, {"column-rule-style", "none"}, {"column-rule-width", "medium"},
        {"column-width", "auto"}, {"contain", "none"}, {"content", "normal"}, {"content-visibility", "visible"},
        {"counter-increment", "none"}, {"counter-reset", "none"}, {"cursor", "auto"}, {"direction", "ltr"},
        {"display", "inline"}, {"empty-cells", "show"}, {"fill", "rgb(0, 0, 0)"}, {"fill-opacity", "1"},
        {"filter", "none"}, {"flex-basis", "auto"}, {"flex-direction", "row"}, {"flex-grow", "0"},
        {"flex-shrink", "1"}, {"flex-wrap", "nowrap"}, {"float", "none"}, {"font-family", "serif"},
        {"font-kerning", "auto"}, {"font-size", "medium"}, {"font-stretch", "100%"}, {"font-style", "normal"},
        {"font-variant", "normal"}, {"font-weight", "normal"}, {"grid-auto-columns", "auto"},
        {"grid-auto-flow", "row"}, {"grid-auto-rows", "auto"}, {"grid-column-end", "auto"},
        {"grid-column-start", "auto"}, {"grid-row-end", "auto"}, {"grid-row-start", "auto"},
        {"grid-template-areas", "none"}, {"grid-template-columns", "none"}, {"grid-template-rows", "none"},
        {"height", "auto"}, {"hyphens", "manual"}, {"inline-size", "auto"}, {"isolation", "auto"},
        {"justify-content", "normal"}, {"justify-items", "normal"}, {"justify-self", "auto"}, {"left", "auto"},
        {"letter-spacing", "normal"}, {"line-height", "normal"}, {"list-style-image", "none"},
        {"list-style-position", "outside"}, {"list-style-type", "disc"}, {"margin-bottom", "0px"},
        {"margin-left", "0px"}, {"margin-right", "0px"}, {"margin-top", "0px"}, {"max-height", "none"},
        {"max-width", "none"}, {"min-height", "auto"}, {"min-width", "auto"}, {"mix-blend-mode", "normal"},
        {"object-fit", "fill"}, {"object-position", "50% 50%"}, {"opacity", "1"}, {"order", "0"}, {"orphans", "2"},
        {"outline-color", "currentcolor"}, {"outline-offset", "0px"}, {"outline-style", "none"},
        {"outline-width", "medium"}, {"overflow-anchor", "auto"}, {"overflow-wrap", "normal"},
        {"overflow-x", "visible"}, {"overflow-y", "visible"}, {"padding-bottom", "0px"}, {"padding-left", "0px"},
        {"padding-right", "0px"}, {"padding-top", "0px"}, {"perspective", "none"}, {"pointer-events", "auto"},
        {"position", "static"}, {"quotes", "auto"}, {"resize", "none"}, {"right", "auto"}, {"row-gap", "normal"},
        {"scroll-behavior", "auto"}, {"stroke", "none"}, {"stroke-opacity", "1"}, {"stroke-width", "1px"},
        {"tab-size", "8"}, {"table-layout", "auto"}, {"text-align", "start"},
        {"text-decoration-color", "currentcolor"}, {"text-decoration-line", "none"},
        {"text-decoration-style", "solid"}, {"text-indent", "0px"}, {"text-overflow", "clip"},
        {"text-rendering", "auto"}, {"text-shadow", "none"}, {"text-transform", "none"}, {"top", "auto"},
        {"touch-action", "auto"}, {"transform", "none"}, {"transform-style", "flat"}, {"transition-delay", "0s"},
        {"transition-duration", "0s"}, {"transition-property", "all"}, {"transition-timing-function", "ease"},
        {"unicode-bidi", "normal"}, {"user-select", "auto"}, {"vertical-align", "baseline"},
        {"visibility", "visible"}, {"white-space", "normal"}, {"widows", "2"}, {"width", "auto"},
        {"will-change", "auto"}, {"word-break", "normal"}, {"word-spacing", "0px"},
        {"writing-mode", "horizontal-tb"}, {"z-index", "auto"}, {"zoom", "1"},
        // Shorthands nothing here takes apart: what they are when not declared.
        {"transition", "all"}, {"animation", "none"}, {"columns", "auto auto"},
    };
    auto found = s_initial.find(property);
    return found == s_initial.end() ? "" : found->second;
}

// What the HTML standard's Rendering section gives each element, as far as a
// script is likely to ask. `display` is in default_display(), `hidden` in
// compute_block(). The layout engine has a sheet of its own
// (StyleResolver::default_user_agent_stylesheet) with other numbers in it: a
// heading is 28px there and 2em here. What is painted follows that one; what
// a script is told follows the standard, because that is what scripts were
// written against.
const char* const kElementDefaults = R"CSS(
address, blockquote, center, dialog, div, figure, figcaption, footer, form, header, hr, legend, listing, main, p,
plaintext, pre, search, xmp, article, aside, h1, h2, h3, h4, h5, h6, hgroup, nav, section, table, caption, colgroup,
col, thead, tbody, tfoot, tr, td, th, dir, dd, dl, dt, menu, ol, ul, li, bdi, output { unicode-bidi: isolate }
address, cite, dfn, em, i, var { font-style: italic }
b, strong { font-weight: bolder }
h1, h2, h3, h4, h5, h6, th { font-weight: bold }
h1 { font-size: 2em; margin-top: 0.67em; margin-bottom: 0.67em }
h2 { font-size: 1.5em; margin-top: 0.83em; margin-bottom: 0.83em }
h3 { font-size: 1.17em; margin-top: 1em; margin-bottom: 1em }
h4 { margin-top: 1.33em; margin-bottom: 1.33em }
h5 { font-size: 0.83em; margin-top: 1.67em; margin-bottom: 1.67em }
h6 { font-size: 0.67em; margin-top: 2.33em; margin-bottom: 2.33em }
body { margin: 8px }
p, blockquote, figure, pre, listing, xmp, plaintext, dl, ol, ul, menu, dir { margin-top: 1em; margin-bottom: 1em }
blockquote, figure { margin-left: 40px; margin-right: 40px }
dd { margin-left: 40px }
ol, ul, menu, dir { padding-left: 40px }
ol { list-style-type: decimal }
:is(ol, ul, menu, dir) :is(ol, ul, menu, dir) { margin-top: 0px; margin-bottom: 0px }
:is(ol, ul, menu, dir) :is(ul, menu, dir) { list-style-type: circle }
:is(ol, ul, menu, dir) :is(ol, ul, menu, dir) :is(ul, menu, dir) { list-style-type: square }
pre, listing, xmp, plaintext { white-space: pre }
pre, listing, xmp, plaintext, code, kbd, samp, tt { font-family: monospace }
big { font-size: larger }
small, sub, sup { font-size: smaller }
sub { vertical-align: sub }
sup { vertical-align: super }
mark { background-color: yellow; color: black }
u, ins { text-decoration-line: underline }
s, strike, del { text-decoration-line: line-through }
a:link { color: rgb(0, 0, 238); text-decoration-line: underline; cursor: pointer }
td, th { padding: 1px }
th, caption, button { text-align: center }
table { box-sizing: border-box; border-spacing: 2px 2px; text-indent: 0px }
thead, tbody, tfoot { vertical-align: middle }
td, th, tr { vertical-align: inherit }
hr { border-style: inset; border-width: 1px; margin-top: 0.5em; margin-bottom: 0.5em; margin-left: auto; margin-right: auto; color: gray; overflow: hidden }
fieldset { margin-left: 2px; margin-right: 2px; padding: 0.35em 0.75em 0.625em; border-width: 2px; border-style: groove }
legend { padding-left: 2px; padding-right: 2px }
textarea { white-space: pre-wrap }
)CSS";

// ---------------------------------------------------------------------------
// Shorthands of the computed style
// ---------------------------------------------------------------------------
//
// The block takes margin, padding and the border family apart itself. These
// are the others a script reads the parts of: `overflow: hidden` must answer
// for `overflowY`, `font: 12px/1.5 Arial` for `fontSize`.

const std::vector<std::string>* parts_of(const std::string& name) {
    static const std::unordered_map<std::string, std::vector<std::string>> s_parts = {
        {"overflow", {"overflow-x", "overflow-y"}},
        {"gap", {"row-gap", "column-gap"}},
        {"inset", {"top", "right", "bottom", "left"}},
        {"border-radius",
         {"border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius",
          "border-bottom-left-radius"}},
        {"flex", {"flex-grow", "flex-shrink", "flex-basis"}},
        {"flex-flow", {"flex-direction", "flex-wrap"}},
        {"font", {"font-style", "font-variant", "font-weight", "font-size", "line-height", "font-family"}},
        {"list-style", {"list-style-position", "list-style-image", "list-style-type"}},
        {"text-decoration", {"text-decoration-line", "text-decoration-style", "text-decoration-color"}},
        {"outline", {"outline-color", "outline-style", "outline-width"}},
    };
    auto found = s_parts.find(name);
    return found == s_parts.end() ? nullptr : &found->second;
}

bool is_plain_number(const std::string& text) {
    std::size_t at = 0;
    double number = 0;
    return parse_css_number(text, at, number) && at == text.size();
}

bool starts_like_a_number(const std::string& text) {
    return !text.empty() && ((text[0] >= '0' && text[0] <= '9') || text[0] == '.' || text[0] == '+' || text[0] == '-');
}

// The longhands a shorthand declaration stands for. False when the value is
// not understood (a var(), a system font): the declaration is then kept
// under its own name and its longhands are not known (see covered_by).
// A number word for flex-grow or flex-shrink: a plain number, or a calc()
// whose type is a number (a static one folds into "calc(<number>)"; the rest
// is kept, rewritten the way it serializes). A calc that is really a length
// or a percentage — even when only its operands carry units, like
// sign(20cqw - 10px) — is not a number and is refused.
bool flex_number(const std::string& part, std::string& word) {
    if (is_plain_number(part)) {
        word = part;
        return true;
    }
    if (litebird::grid_calc_family(part).empty()) return false;
    litebird::CalcNode ast;
    if (!litebird::calc_parse(part, ast)) {
        // Something the grammar does not know: keep it as it was written.
        word = part;
        return true;
    }
    if (litebird::calc_classify(ast) != 0) return false; // a <length-percentage> calc is not a number
    litebird::CalcTriple triple;
    if (litebird::calc_eval(ast, litebird::CalcContext{}, triple) && !triple.dimension) {
        word = "calc(" + litebird::calc_num_text(triple.number) + ")";
        return true;
    }
    word = litebird::canonicalize_calc(part);
    return true;
}

bool decompose(const std::string& name, const std::string& value, Parts& out) {
    const std::vector<std::string>* longhands = parts_of(name);
    if (!longhands) return false;
    const std::string lower = ascii_lower(trim(value));
    if (lower.empty() || lower.find("var(") != std::string::npos) return false;
    const std::vector<std::string> parts = split_components(trim(value));
    if (parts.empty()) return false;
    if (parts.size() == 1 && is_global_keyword(lower)) {
        for (const auto& longhand : *longhands) out.emplace_back(longhand, lower);
        return true;
    }
    if (name == "overflow" || name == "gap") {
        if (parts.size() > 2) return false;
        out.emplace_back((*longhands)[0], parts[0]);
        out.emplace_back((*longhands)[1], parts.back());
        return true;
    }
    if (name == "inset" || name == "border-radius") {
        // One value is all four; with two the third is the first and the
        // fourth the second; with three the fourth is the second. A radius
        // with "/" in it has two radii for a corner and is left alone.
        if (parts.size() > 4 || lower.find('/') != std::string::npos) return false;
        const std::string& first = parts[0];
        const std::string& second = parts.size() > 1 ? parts[1] : first;
        const std::string& third = parts.size() > 2 ? parts[2] : first;
        const std::string& fourth = parts.size() > 3 ? parts[3] : second;
        out.emplace_back((*longhands)[0], first);
        out.emplace_back((*longhands)[1], second);
        out.emplace_back((*longhands)[2], third);
        out.emplace_back((*longhands)[3], fourth);
        return true;
    }
    if (name == "flex-flow") {
        std::string direction, wrap;
        for (const auto& part : parts) {
            const std::string word = ascii_lower(part);
            const bool is_direction = word == "row" || word == "row-reverse" || word == "column" || word == "column-reverse";
            const bool is_wrap = word == "nowrap" || word == "wrap" || word == "wrap-reverse";
            if (is_direction && direction.empty()) direction = word;
            else if (is_wrap && wrap.empty()) wrap = word;
            else return false;
        }
        out.emplace_back("flex-direction", direction.empty() ? "row" : direction);
        out.emplace_back("flex-wrap", wrap.empty() ? "nowrap" : wrap);
        return true;
    }
    if (name == "flex") {
        if (parts.size() > 3) return false;
        std::string grow, shrink, basis;
        if (lower == "none") {
            grow = shrink = "0";
            basis = "auto";
        } else {
            for (const auto& part : parts) {
                if (ascii_lower(part) == "none") return false; // "none" stands alone or not at all
                std::string number_word;
                const bool is_number = flex_number(part, number_word);
                if (is_number && grow.empty()) {
                    grow = number_word;
                    continue;
                }
                if (is_number && shrink.empty()) {
                    shrink = number_word;
                    continue;
                }
                if (basis.empty()) {
                    if (is_number) {
                        // Three numbers: the last one would be a unitless
                        // basis, where only a literal zero is a length.
                        if (!is_plain_number(part)) return false;
                        double literal = 0;
                        std::size_t at = 0;
                        parse_css_number(part, at, literal);
                        if (literal != 0) return false;
                        basis = "0px";
                        continue;
                    }
                    if (!litebird::grid_calc_family(part).empty()) {
                        litebird::CalcNode ast;
                        if (!litebird::calc_parse(part, ast)) {
                            // Something the grammar does not know: keep it as
                            // it was written.
                            basis = part;
                            continue;
                        }
                        if (litebird::calc_classify(ast) == 0) return false; // a number, not a <length-percentage>
                        basis = litebird::canonicalize_calc(part);
                        continue;
                    }
                    basis = part; // auto, content, a length, a percentage, a non-static calc(...)
                    continue;
                }
                return false;
            }
            // `flex: 30px` grows and shrinks; `flex: 2` has no basis to keep.
            if (grow.empty()) grow = "1";
            if (shrink.empty()) shrink = "1";
            if (basis.empty()) basis = "0%";
        }
        out.emplace_back("flex-grow", grow);
        out.emplace_back("flex-shrink", shrink);
        out.emplace_back("flex-basis", basis);
        return true;
    }
    if (name == "list-style") {
        std::string type, position, image;
        bool none = false;
        for (const auto& part : parts) {
            const std::string word = ascii_lower(part);
            if (word == "inside" || word == "outside") position = word;
            else if (word == "none") none = true;
            else if (word.rfind("url(", 0) == 0 || word.find("gradient(") != std::string::npos) image = part;
            else type = part[0] == '"' || part[0] == '\'' ? part : word;
        }
        out.emplace_back("list-style-type", type.empty() ? (none ? "none" : "disc") : type);
        out.emplace_back("list-style-position", position.empty() ? "outside" : position);
        out.emplace_back("list-style-image", image.empty() ? "none" : image);
        return true;
    }
    if (name == "text-decoration" || name == "outline") {
        const bool outline = name == "outline";
        std::string line, style = outline ? "none" : "solid", color = "currentcolor", width = "medium";
        for (const auto& part : parts) {
            const std::string word = ascii_lower(part);
            std::string normal;
            if (!outline && (word == "none" || word == "underline" || word == "overline" || word == "line-through" ||
                             word == "blink")) {
                if (!line.empty()) line += ' ';
                line += word;
            } else if (word == "solid" || word == "double" || word == "dotted" || word == "dashed" ||
                       (!outline && word == "wavy") ||
                       (outline && (word == "none" || word == "auto" || word == "groove" || word == "ridge" ||
                                    word == "inset" || word == "outset"))) {
                style = word;
            } else if (word == "thin" || word == "medium" || word == "thick" || word == "auto" || word == "from-font" ||
                       starts_like_a_number(word)) {
                width = word;
            } else if (normalize_color(part, normal)) {
                color = normal;
            } else {
                return false;
            }
        }
        if (outline) {
            out.emplace_back("outline-width", width);
            out.emplace_back("outline-style", style);
            out.emplace_back("outline-color", color);
        } else {
            out.emplace_back("text-decoration-line", line.empty() ? "none" : line);
            out.emplace_back("text-decoration-style", style);
            out.emplace_back("text-decoration-color", color);
        }
        return true;
    }
    // font: [style, variant, weight in any order] size[/line-height] family
    std::string style = "normal", variant = "normal", weight = "normal";
    std::size_t at = 0;
    for (; at < parts.size(); ++at) {
        const std::string word = ascii_lower(parts[at]);
        if (word == "normal") continue;
        if (word == "italic" || word == "oblique") style = word;
        else if (word == "small-caps") variant = word;
        else if (word == "bold" || word == "bolder" || word == "lighter" || is_plain_number(word)) weight = word;
        else break;
    }
    if (at >= parts.size()) return false;
    std::string size = parts[at++];
    std::string line = "normal";
    const auto slash = size.find('/');
    if (slash != std::string::npos) {
        line = size.substr(slash + 1);
        size = size.substr(0, slash);
    } else if (at < parts.size() && parts[at][0] == '/') {
        line = parts[at++].substr(1);
    } else {
        line.clear();
    }
    if (slash != std::string::npos || !line.empty() || (at > 0 && parts[at - 1] == "/")) {
        // "12px/ 1.5" and "12px / 1.5": the height is the next part.
        if (line.empty()) {
            if (at >= parts.size()) return false;
            line = parts[at++];
        }
    } else {
        line = "normal";
    }
    // A family is required, which is also what tells `font: caption` apart.
    if (size.empty() || at >= parts.size()) return false;
    std::string family;
    for (; at < parts.size(); ++at) {
        if (!family.empty()) family += ' ';
        family += parts[at];
    }
    out.emplace_back("font-style", style);
    out.emplace_back("font-variant", variant);
    out.emplace_back("font-weight", weight);
    out.emplace_back("font-size", size);
    out.emplace_back("line-height", line);
    out.emplace_back("font-family", family);
    return true;
}

// take_apart is decompose minus the properties the shorthand table takes
// care of (the table path is what inline style goes through).
bool take_apart(const std::string& name, const std::string& value, Parts& out) {
    if (find_shorthand(name)) return false;
    return decompose(name, value, out);
}

// The value of a shorthand, from the computed values of its parts in the
// order parts_of() has them. "" when one of them is not known.
std::string put_together(const std::string& name, const std::vector<std::string>& v, bool collapse) {
    for (const auto& part : v) {
        if (part.empty()) return std::string();
    }
    if (std::all_of(v.begin(), v.end(), [&](const std::string& part) { return part == v.front(); }) &&
        is_global_keyword(ascii_lower(v.front()))) {
        return v.front();
    }
    if (collapse && name == "flex-flow") {
        // The shortest serialization that round-trips: nowrap is the
        // initial wrap, row the initial direction.
        if (v[1] == "nowrap") return v[0];
        if (v[0] == "row") return v[1];
        return v[0] + " " + v[1];
    }
    if (name == "overflow" || name == "gap") return v[0] == v[1] ? v[0] : v[0] + " " + v[1];
    if (name == "inset" || name == "border-radius") {
        if (v[3] == v[1]) {
            if (v[2] == v[0]) return v[1] == v[0] ? v[0] : v[0] + " " + v[1];
            return v[0] + " " + v[1] + " " + v[2];
        }
        return v[0] + " " + v[1] + " " + v[2] + " " + v[3];
    }
    if (name == "font") {
        std::string out;
        if (v[0] != "normal") out += v[0] + " ";
        if (v[1] != "normal") out += v[1] + " ";
        if (v[2] != "400") out += v[2] + " ";
        out += v[3];
        if (v[4] != "normal") out += " / " + v[4];
        return out + " " + v[5];
    }
    std::string out;
    for (const auto& part : v) {
        if (!out.empty()) out += ' ';
        out += part;
    }
    return out;
}

// Whether a declaration of `shorthand` sets `name`. This is for the
// shorthands nothing here takes apart (transition, animation, grid, ...) and
// for the ones above when their value was not understood: such a declaration
// says something about its longhands that cannot be told, so they are
// forgotten when it is applied, and asked for one of them the answer is ""
// and not an initial value that would be wrong.
bool covered_by(const std::string& shorthand, const std::string& name) {
    static const std::unordered_set<std::string> s_by_prefix = {
        "transition", "animation", "text-decoration", "column-rule", "grid-template", "border-image", "mask",
        "background", "font", "list-style", "text-emphasis", "scroll-margin", "scroll-padding", "font-variant",
        "font-synthesis",
    };
    static const std::unordered_map<std::string, std::vector<std::string>> s_listed = {
        {"outline", {"outline-width", "outline-style", "outline-color"}},
        {"overflow", {"overflow-x", "overflow-y"}},
        {"flex", {"flex-grow", "flex-shrink", "flex-basis"}},
        {"flex-flow", {"flex-direction", "flex-wrap"}},
        {"gap", {"row-gap", "column-gap"}},
        {"inset", {"top", "right", "bottom", "left"}},
        {"border-radius",
         {"border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius",
          "border-bottom-left-radius"}},
        {"place-items", {"align-items", "justify-items"}},
        {"place-content", {"align-content", "justify-content"}},
        {"place-self", {"align-self", "justify-self"}},
        {"columns", {"column-count", "column-width"}},
        {"grid-area", {"grid-row-start", "grid-row-end", "grid-column-start", "grid-column-end"}},
        {"grid-row", {"grid-row-start", "grid-row-end"}},
        {"grid-column", {"grid-column-start", "grid-column-end"}},
        {"grid",
         {"grid-template-rows", "grid-template-columns", "grid-template-areas", "grid-auto-rows",
          "grid-auto-columns", "grid-auto-flow"}},
        {"font", {"line-height"}},
    };
    if (s_by_prefix.count(shorthand) && name.size() > shorthand.size() + 1 &&
        name.compare(0, shorthand.size(), shorthand) == 0 && name[shorthand.size()] == '-') {
        // The block keeps the colour of a `background` itself.
        return !(shorthand == "background" && name == "background-color");
    }
    auto found = s_listed.find(shorthand);
    return found != s_listed.end() && std::find(found->second.begin(), found->second.end(), name) != found->second.end();
}

// ---------------------------------------------------------------------------
// The cascade
// ---------------------------------------------------------------------------

struct Matched {
    int layer{0};
    long specificity{0};
    const RawDeclaration* declaration{nullptr};
};

// What the cascade gives one element: the declarations that apply to it, the
// last word on each property. In order of weight: the element's defaults,
// the rules of the document's style elements, the style attribute, and then
// the `!important` ones of the same three; within the rules by specificity,
// then by position.
void compute_block(Realm& realm, const Element& element, Block& block, const Node* root = nullptr) {
    block.clear();
    if (element.has_attribute("hidden") && !element.is_foreign()) block.set("display", "none", false);
    else block.set("display", default_display(element), false);

    std::vector<Matched> matched;
    auto collect = [&](const ParsedSheet& sheet, int layer) {
        for (const auto& rule : sheet.rules) {
            const long specificity = rule.selector.matching_specificity(element);
            if (specificity < 0) continue;
            for (const auto& declaration : rule.declarations) {
                matched.push_back({declaration.important ? layer + 3 : layer, specificity, &declaration});
            }
        }
    };
    StylePart& part = realm.part<StylePart>();
    if (!element.is_foreign()) {
        if (!part.defaults) {
            part.defaults = std::make_shared<ParsedSheet>();
            const std::string css = kElementDefaults;
            std::size_t at = 0;
            parse_rules(css, at, part.defaults->rules, false);
        }
        collect(*part.defaults, 0);
    }

    // The tree the element is in decides which style elements apply.
    if (!root) root = tree_root_of(element);
    if (root->is_document() || root->is_document_fragment()) {
        style_elements_of(realm, *root);
        for (const auto& sheet : part.style_sheets) collect(*sheet, 1);
    }
    std::vector<RawDeclaration> inline_declarations;
    if (const std::string* inline_text = element.attribute("style")) {
        inline_declarations = parse_declarations(*inline_text);
        for (const auto& declaration : inline_declarations) {
            matched.push_back({declaration.important ? 5 : 2, 0, &declaration});
        }
    }
    std::stable_sort(matched.begin(), matched.end(), [](const Matched& a, const Matched& b) {
        return a.layer != b.layer ? a.layer < b.layer : a.specificity < b.specificity;
    });

    for (const auto& entry : matched) {
        const RawDeclaration& declaration = *entry.declaration;
        if (!is_css_property(declaration.name)) continue;
        Parts parts;
        if (take_apart(declaration.name, declaration.value, parts)) {
            for (const auto& [name, value] : parts) block.set(name, value, declaration.important);
            // An earlier one that was kept whole has been replaced.
            block.entries.erase(std::remove_if(block.entries.begin(), block.entries.end(),
                                               [&](const Entry& kept) { return kept.name == declaration.name; }),
                                block.entries.end());
            continue;
        }
        if (!block.set(declaration.name, declaration.value, declaration.important)) continue;
        block.entries.erase(std::remove_if(block.entries.begin(), block.entries.end(),
                                           [&](const Entry& kept) { return !kept.implicit && covered_by(declaration.name, kept.name); }),
                            block.entries.end());
    }
}

// ---------------------------------------------------------------------------
// Computed values
// ---------------------------------------------------------------------------

std::string px_text(double value) {
    if (!std::isfinite(value)) return "0px";
    char buffer[48];
    // Six significant digits, as browsers print: 13.3333px, 21.44px.
    if (std::fabs(value) >= 1e6) std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    else std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    std::string text = buffer;
    if (text == "-0") text = "0";
    return text + "px";
}

// A length whose size does not depend on anything else, in px.
bool absolute_length(double number, const std::string& unit, double& out) {
    if (unit == "px") out = number;
    else if (unit == "pt") out = number * 96 / 72;
    else if (unit == "pc") out = number * 16;
    else if (unit == "in") out = number * 96;
    else if (unit == "cm") out = number * 96 / 2.54;
    else if (unit == "mm") out = number * 96 / 25.4;
    else if (unit == "q") out = number * 96 / 101.6;
    else return false;
    return true;
}

// calc(): sums and products of lengths, percentages and numbers. What it
// cannot know (the viewport, a font's metrics, min() and max()) makes it
// fail, and the value is then given back as it was declared.
struct CalcValue {
    double px{0};           // the length part
    double percent{0};      // the percentage part, which only layout can turn into a length
    double number{0};       // a plain number, when `dimension` is false
    bool dimension{false};
};

class CalcParser {
public:
    CalcParser(const std::string& text, double em, double rem) : m_text(text), m_em(em), m_rem(rem) {}

    bool parse(CalcValue& out) {
        if (!sum(out, 0)) return false;
        skip();
        return m_at == m_text.size();
    }

private:
    // Parentheses nest by recursion. Nobody writes more than a few; a page
    // that writes a hundred thousand gets its text back.
    static constexpr int kMaxDepth = 16;

    void skip() {
        while (m_at < m_text.size() && is_ascii_space(m_text[m_at])) ++m_at;
    }

    bool sum(CalcValue& out, int depth) {
        if (!product(out, depth)) return false;
        for (;;) {
            skip();
            if (m_at >= m_text.size() || (m_text[m_at] != '+' && m_text[m_at] != '-')) return true;
            const double sign = m_text[m_at++] == '+' ? 1 : -1;
            CalcValue right;
            if (!product(right, depth) || right.dimension != out.dimension) return false;
            out.px += sign * right.px;
            out.percent += sign * right.percent;
            out.number += sign * right.number;
        }
    }

    bool product(CalcValue& out, int depth) {
        if (!value(out, depth)) return false;
        for (;;) {
            skip();
            if (m_at >= m_text.size() || (m_text[m_at] != '*' && m_text[m_at] != '/')) return true;
            const bool times = m_text[m_at++] == '*';
            CalcValue right;
            if (!value(right, depth)) return false;
            if (times && right.dimension) std::swap(out, right);        // 2 * 3px
            if (right.dimension) return false;                          // 2px * 3px, 1 / 2px
            if (!times && right.number == 0) return false;
            const double factor = times ? right.number : 1 / right.number;
            out.px *= factor;
            out.percent *= factor;
            out.number *= factor;
        }
    }

    bool value(CalcValue& out, int depth) {
        skip();
        if (m_at >= m_text.size()) return false;
        const bool nested = m_text.compare(m_at, 5, "calc(") == 0;
        if (nested || m_text[m_at] == '(') {
            if (depth >= kMaxDepth) return false;
            m_at += nested ? 5 : 1;
            if (!sum(out, depth + 1)) return false;
            skip();
            if (m_at >= m_text.size() || m_text[m_at] != ')') return false;
            ++m_at;
            return true;
        }
        double number = 0;
        if (!parse_css_number(m_text, m_at, number)) return false;
        const std::size_t start = m_at;
        while (m_at < m_text.size() && ((m_text[m_at] >= 'a' && m_text[m_at] <= 'z') || m_text[m_at] == '%')) ++m_at;
        const std::string unit = m_text.substr(start, m_at - start);
        out = CalcValue{};
        if (unit.empty()) {
            out.number = number;
            return true;
        }
        out.dimension = true;
        if (unit == "%") out.percent = number;
        else if (unit == "em") out.px = number * m_em;
        else if (unit == "rem") out.px = number * m_rem;
        else if (!absolute_length(number, unit, out.px)) return false;
        return true;
    }

    const std::string& m_text;
    std::size_t m_at{0};
    double m_em;
    double m_rem;
};

// `lower` is "calc(...)". `em` and `rem` are the font sizes those units mean.
bool eval_calc(const std::string& lower, double em, double rem, CalcValue& out) {
    return CalcParser(lower, em, rem).parse(out) && out.dimension;
}

bool absolute_font_size(const std::string& lower, double& out) {
    static const std::pair<const char*, double> kWords[] = {
        {"xx-small", 9}, {"x-small", 10}, {"small", 13},     {"medium", 16},   {"large", 18},
        {"x-large", 24}, {"xx-large", 32}, {"xxx-large", 48}, {"initial", 16},
    };
    for (const auto& [word, size] : kWords) {
        if (lower == word) {
            out = size;
            return true;
        }
    }
    std::size_t at = 0;
    double number = 0;
    if (!parse_css_number(lower, at, number)) return false;
    return absolute_length(number, lower.substr(at), out);
}

// A font size that is said in terms of the parent's (or, for rem, the root's).
double relative_font_size(const std::string& lower, double parent, double root) {
    if (is_function_value(lower, "calc")) {
        CalcValue sum;
        if (!eval_calc(lower, parent, root, sum)) return parent;
        return sum.px + sum.percent / 100 * parent;
    }
    if (lower == "larger") return parent * 1.2;
    if (lower == "smaller") return parent * 5 / 6;
    std::size_t at = 0;
    double number = 0;
    if (!parse_css_number(lower, at, number)) return parent;
    const std::string unit = lower.substr(at);
    if (unit == "em") return parent * number;
    if (unit == "%") return parent * number / 100;
    if (unit == "rem") return root * number;
    if (unit == "ex" || unit == "ch") return parent * number / 2;
    return parent;
}

const Element* parent_element_of(const Element& element, std::shared_ptr<Node>& keep) {
    keep = element.parent();
    return keep && keep->is_element() ? static_cast<const Element*>(keep.get()) : nullptr;
}

double root_font_size(Realm& realm, const Element& element) {
    const Element* root = &element;
    std::shared_ptr<Node> keep;
    std::shared_ptr<Node> next;
    while (const Element* parent = parent_element_of(*root, next)) {
        keep = next;
        root = parent;
    }
    Block block;
    compute_block(realm, *root, block, tree_root_of(*root));
    const Entry* entry = block.find("font-size");
    const std::string lower = entry ? ascii_lower(entry->value) : std::string();
    double size = 16;
    if (absolute_font_size(lower, size)) return size;
    return relative_font_size(lower, 16, 16);
}

// The font size of an element, in px. The declared sizes from the element up
// to the first that does not depend on its parent, and then back down: a
// loop, because the chain is as long as the tree is deep.
double font_size_px(Realm& realm, const Element& element) {
    std::vector<std::string> chain;
    double size = 16;
    Block scratch;
    std::shared_ptr<Node> keep;
    std::shared_ptr<Node> next;
    const Node* top = tree_root_of(element);
    for (const Element* at = &element; at; at = parent_element_of(*at, next), keep = next) {
        compute_block(realm, *at, scratch, top);
        const Entry* entry = scratch.find("font-size");
        const std::string lower = entry ? ascii_lower(entry->value) : std::string();
        if (absolute_font_size(lower, size)) break;
        chain.push_back(lower);
    }
    double root = -1;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const bool in_rem = it->size() > 3 && it->compare(it->size() - 3, 3, "rem") == 0;
        if (in_rem && root < 0) root = root_font_size(realm, element);
        size = relative_font_size(*it, size, root < 0 ? 16 : root);
    }
    return size;
}

// The same for the weight: `bolder` and `lighter` are steps from the parent's.
double font_weight_of(Realm& realm, const Element& element) {
    std::vector<int> steps;
    double weight = 400;
    Block scratch;
    std::shared_ptr<Node> keep;
    std::shared_ptr<Node> next;
    const Node* top = tree_root_of(element);
    for (const Element* at = &element; at; at = parent_element_of(*at, next), keep = next) {
        compute_block(realm, *at, scratch, top);
        const Entry* entry = scratch.find("font-weight");
        const std::string lower = entry ? ascii_lower(entry->value) : std::string();
        if (lower == "bolder") { steps.push_back(1); continue; }
        if (lower == "lighter") { steps.push_back(-1); continue; }
        if (lower == "normal" || lower == "initial") break;
        if (lower == "bold") { weight = 700; break; }
        if (is_plain_number(lower)) { weight = std::strtod(lower.c_str(), nullptr); break; }
    }
    for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
        if (*it > 0) weight = weight < 350 ? 400 : weight < 550 ? 700 : std::max(weight, 900.0);
        else weight = weight < 100 ? weight : weight < 550 ? 100 : weight < 750 ? 400 : 700;
    }
    return weight;
}

// One length, in px. False for anything else: a keyword, a percentage, a unit
// that needs the viewport or a font's metrics.
bool length_px(Realm& realm, const Element& from, const std::string& lower, double& px) {
    std::size_t at = 0;
    double number = 0;
    if (!parse_css_number(lower, at, number) || at == lower.size()) return false;
    const std::string unit = lower.substr(at);
    if (unit == "em") px = number * font_size_px(realm, from);
    else if (unit == "rem") px = number * root_font_size(realm, from);
    else return absolute_length(number, unit, px);
    return true;
}

bool is_color_property(const std::string& name) {
    return name == "color" || name == "fill" || name == "stroke" ||
           (name.size() > 6 && name.compare(name.size() - 6, 6, "-color") == 0);
}

enum class Found { No, Yes, Unknown };

Found declared_in(const Block& block, const std::string& name, std::string& value) {
    if (const Entry* entry = block.find(name); entry && !entry->value.empty()) {
        value = entry->value;
        return Found::Yes;
    }
    for (const auto& entry : block.entries) {
        if (!entry.value.empty() && covered_by(entry.name, name)) return Found::Unknown;
    }
    return Found::No;
}

// What a calc() may not know by itself: the font size, the query container's
// inline size (the nearest ancestor with a container-type of inline-size or
// size, when its width is definite) and where the element sits among its
// siblings (for sibling-index()/sibling-count()).
litebird::CalcContext make_calc_context(Realm& realm, const Element& subject, double em, double rem) {
    litebird::CalcContext ctx;
    ctx.em = em;
    ctx.rem = rem;
    if (const std::shared_ptr<Node> parent = subject.parent()) {
        double index = 0, count = 0;
        bool found = false;
        for (const auto& child : parent->children()) {
            if (!child->is_element()) continue;
            ++count;
            if (!found) ++index;
            if (child.get() == &subject) found = true;
        }
        if (found) {
            ctx.sibling_index = index;
            ctx.sibling_count = count;
        }
    }
    const Node* top = tree_root_of(subject);
    std::shared_ptr<Node> keep;
    for (const Element* at = parent_element_of(subject, keep); at; at = parent_element_of(*at, keep)) {
        Block block;
        compute_block(realm, *at, block, top);
        std::string container_type;
        if (declared_in(block, "container-type", container_type) != Found::Yes) continue;
        const std::string lower = ascii_lower(container_type);
        if (lower != "inline-size" && lower != "size") continue; // not a query container for the inline axis
        std::string width;
        double px = 0;
        if (declared_in(block, "width", width) == Found::Yes && length_px(realm, *at, ascii_lower(width), px)) {
            ctx.query_inline_px = px;
        }
        return ctx; // the nearest query container decides, even when its size is not definite
    }
    return ctx;
}

std::string computed_value(Realm& realm, const Element& element, const Block& block, const std::string& name,
                           int depth);

// `value` is what the cascade (or inheritance, or the table of initial
// values) gave `subject` for `name`; it was declared on `from`, whose block
// is `block`. This makes the computed value of it: an absolute colour, an
// absolute length.
std::string resolved(Realm& realm, const Element& subject, const Element& from, const Block& block,
                     const std::string& name, const std::string& value, int depth) {
    // What a calculation may need from the tree, built at most once per
    // resolved value and only when one needs it.
    litebird::CalcContext calc_ctx;
    bool calc_ctx_built = false;
    const auto ensure_calc_ctx = [&]() -> const litebird::CalcContext& {
        if (!calc_ctx_built) {
            calc_ctx = make_calc_context(realm, subject, font_size_px(realm, from), root_font_size(realm, from));
            calc_ctx_built = true;
        }
        return calc_ctx;
    };
    if (is_custom_property(name)) return value;
    const std::string lower = ascii_lower(value);
    // A flex grow or shrink factor computes to a plain number. A negative
    // one clamps to zero, like any number whose grammar has a zero floor
    // (CSS Values §10.4.1).
    if (name == "flex-grow" || name == "flex-shrink") {
        std::size_t at = 0;
        double number = 0;
        if (parse_css_number(lower, at, number) && at == lower.size()) {
            return format_number(std::max(0.0, number));
        }
        if (!litebird::grid_calc_family(lower).empty()) {
            litebird::CalcTriple triple;
            if (litebird::calc_eval_text(lower, ensure_calc_ctx(), triple) && !triple.dimension) {
                return format_number(std::max(0.0, triple.number));
            }
            return value;
        }
        return value;
    }
    if (is_color_property(name)) {
        if (lower == "currentcolor") {
            return name == "color" ? std::string("rgb(0, 0, 0)") : computed_value(realm, from, block, "color", depth + 1);
        }
        if (lower == "transparent") return "rgba(0, 0, 0, 0)";
        ColorResolveContext ctx;
        if (lower.find("light-dark(") != std::string::npos) {
            const std::string scheme = ascii_lower(computed_value(realm, from, block, "color-scheme", depth + 1));
            ctx.dark_scheme = (scheme == "dark" || scheme == "only dark");
        }
        CssColor cc;
        if (lower.find("currentcolor") != std::string::npos) {
            std::string cc_str = "rgb(0, 0, 0)";
            if (name == "color") {
                std::shared_ptr<Node> keep;
                if (const Element* parent_el = parent_element_of(from, keep)) {
                    Block parent_block;
                    compute_block(realm, *parent_el, parent_block, tree_root_of(from));
                    cc_str = computed_value(realm, *parent_el, parent_block, "color", depth + 1);
                }
            } else {
                cc_str = computed_value(realm, from, block, "color", depth + 1);
            }
            if (CssColor::parse(cc_str, cc)) ctx.current_color = &cc;
        }
        CssColor parsed;
        if (CssColor::parse(value, parsed, ctx)) {
            if (parsed.is_currentcolor) {
                return name == "color" ? std::string("rgb(0, 0, 0)")
                                       : computed_value(realm, from, block, "color", depth + 1);
            }
            return parsed.serialize_computed();
        }
        return value;
    }
    if (name == "font-size") return px_text(font_size_px(realm, from));
    if (name == "font-weight") return format_number(font_weight_of(realm, from));
    // A grid line computes with its static calc() folded and its span count
    // clamped forward like any span.
    if (name == "grid-column-start" || name == "grid-column-end" || name == "grid-row-start" ||
        name == "grid-row-end") {
        std::string line;
        if (litebird::grid_line_computed(value, line, &ensure_calc_ctx())) return line;
        return value;
    }

    // A grid track list computes with its lengths absolute and, on a grid
    // container, with its repeats expanded (css-grid §7.2.6, the resolved
    // value). The percentage basis is the container's content size, read
    // from the same block when it is definite.
    if (name == "grid-template-rows" || name == "grid-template-columns" || name == "grid-auto-rows" ||
        name == "grid-auto-columns") {
        const double em = font_size_px(realm, from);
        const double rem = root_font_size(realm, from);
        const std::string display = ascii_lower(computed_value(realm, from, block, "display", depth + 1));
        const bool is_grid = display == "grid" || display == "inline-grid";
        // An intrinsic auto-repeat is a grid-lanes feature (css-grid-3); on
        // anything but a grid-lanes container the resolved value is `none`.
        if (grid_has_intrinsic_auto_repeat(ascii_lower(value)) && display != "grid-lanes" &&
            display != "inline-grid-lanes" && (name == "grid-template-rows" || name == "grid-template-columns")) {
            return "none";
        }
        double basis = -1.0;
        if (is_grid && (name == "grid-template-rows" || name == "grid-template-columns")) {
            const char* basis_name = name == "grid-template-rows" ? "height" : "width";
            const std::string basis_value = ascii_lower(computed_value(realm, from, block, basis_name, depth + 1));
            double px = 0;
            if (length_px(realm, from, basis_value, px)) basis = px;
        }
        const auto simplify_calc = [&](const std::string& calc_text, double& px, double& percent) {
            CalcValue sum;
            const std::string lowered = ascii_lower(calc_text);
            if (!eval_calc(lowered, em, rem, sum)) return false;
            px = sum.px;
            percent = sum.percent;
            return true;
        };
        std::string resolved_list;
        if (litebird::grid_resolved_track_list(value, em, rem, basis, is_grid, simplify_calc, resolved_list,
                                               &ensure_calc_ctx())) {
            return resolved_list;
        }
        return value;
    }

    // A calc() of lengths is a length. With a percentage in it there is
    // nothing more to do without layout, and it is written the short way.
    std::string text = lower;
    if (is_function_value(lower, "calc")) {
        // A calc() of a length is a computed length; resolve it against what
        // the tree knows (fonts, the query container). A number-typed calc
        // stays as written — the plain-number paths handle those.
        litebird::CalcTriple triple;
        if (!litebird::calc_eval_text(lower, ensure_calc_ctx(), triple)) return value;
        if (!triple.dimension) return value;
        // A calc with a percentage in it keeps its calc at computed-value
        // time (percentages resolve against a basis that is not here); a
        // zero one is written as it came in.
        const bool has_percent = lower.find('%') != std::string::npos;
        if (has_percent && triple.percent == 0) return value;
        if (triple.percent != 0) {
            const std::string percent = format_number(triple.percent) + "%";
            if (triple.px == 0) return percent;
            std::string length = px_text(std::fabs(triple.px));
            return "calc(" + percent + (triple.px < 0 ? " - " : " + ") + length + ")";
        }
        // A negative length is clamped to zero where the property does not
        // accept negative lengths, for example a flex-basis.
        text = (name == "flex-basis" && triple.px < 0) ? "0px" : px_text(triple.px);
    }

    std::size_t at = 0;
    double number = 0;
    const bool numeric = parse_css_number(text, at, number);
    const std::string unit = numeric ? text.substr(at) : std::string();
    if (name == "line-height" && numeric) {
        // A bare number is inherited as a number and multiplies the font
        // size of whoever has it; a percentage is of the font size where it
        // was declared, and is inherited as that length.
        if (unit.empty()) return px_text(number * font_size_px(realm, subject));
        if (unit == "%") return px_text(number / 100 * font_size_px(realm, from));
    }
    if (name == "border-spacing") {
        // Two lengths, also when one was written.
        std::vector<std::string> parts = split_components(text);
        if (parts.size() == 1) parts.push_back(parts[0]);
        if (parts.size() != 2) return value;
        double across = 0, down = 0;
        if (!length_px(realm, from, parts[0], across) || !length_px(realm, from, parts[1], down)) return value;
        return px_text(across) + " " + px_text(down);
    }
    double px = 0;
    const bool is_length = length_px(realm, from, text, px);
    if (name.size() > 6 && name.compare(name.size() - 6, 6, "-width") == 0) {
        // A border, an outline or a column rule that is not drawn has no
        // width, whatever was declared for it. One that is drawn has a whole
        // number of pixels, and not less than one.
        const std::string style_name = name.substr(0, name.size() - 5) + "style";
        if (*initial_value(style_name)) {
            std::string style;
            const Found found = declared_in(block, style_name, style);
            if (found == Found::No) return "0px";
            if (found == Found::Yes) {
                style = ascii_lower(style);
                if (style == "none" || style == "hidden") return "0px";
            }
            if (lower == "thin") return "1px";
            if (lower == "medium") return "3px";
            if (lower == "thick") return "5px";
            if (is_length) return px_text(px > 0 && px < 1 ? 1 : std::floor(px));
        }
    }
    return is_length ? px_text(px) : value;
}

std::string computed_longhand(Realm& realm, const Element& element, const Block& block, const std::string& name,
                              int depth) {
    const bool inherited = is_inherited(name);
    // 0: a value of its own; 1: the parent's; 2: the initial one.
    auto kind_of = [&](const std::string& text) {
        const std::string lower = ascii_lower(text);
        if (lower == "inherit") return 1;
        if (lower == "initial") return 2;
        if (lower == "unset" || lower == "revert" || lower == "revert-layer") return inherited ? 1 : 2;
        // The colour of the text, asked of the colour of the text.
        if (lower == "currentcolor" && name == "color") return 1;
        return 0;
    };

    std::string value;
    Found found = declared_in(block, name, value);
    if (found == Found::Unknown) return std::string();
    int kind = found == Found::Yes ? kind_of(value) : (inherited ? 1 : 2);

    const Element* from = &element;
    const Block* from_block = &block;
    Block above;
    std::shared_ptr<Node> keep;
    if (kind == 1) {
        // Up the tree to the first ancestor that says something; a loop, for
        // a tree of any depth.
        kind = 2;
        std::shared_ptr<Node> next;
        const Node* top = tree_root_of(element);
        for (const Element* at = parent_element_of(element, next); at; at = parent_element_of(*at, next)) {
            keep = next;
            compute_block(realm, *at, above, top);
            found = declared_in(above, name, value);
            if (found == Found::Unknown) return std::string();
            if (found == Found::Yes) {
                const int there = kind_of(value);
                if (there == 1) continue;
                if (there == 0) {
                    kind = 0;
                    from = at;
                    from_block = &above;
                }
                break;
            }
            // Not inherited, so this was an explicit `inherit`: the parent
            // has no declaration, which leaves the initial value.
            if (!inherited) break;
        }
    }
    if (kind == 2) {
        value = initial_value(name);
        if (value.empty()) return value;
        from = &element;
        from_block = &block;
    }
    return resolved(realm, element, *from, *from_block, name, value, depth);
}

// What getComputedStyle answers for `name`.
std::string computed_value(Realm& realm, const Element& element, const Block& block, const std::string& name,
                           int depth) {
    if (depth > 4) return std::string();
    if (const Shorthand* shorthand = find_shorthand(name)) {
        if (shorthand->kind == ShorthandKind::Background) {
            // Only the colour of a background is known here.
            const Entry* declared = block.find(name);
            if (declared && split_components(declared->value).size() > 1) return declared->value;
            return computed_longhand(realm, element, block, "background-color", depth + 1);
        }
        Block parts;
        for (const auto& longhand : shorthand->longhands) {
            parts.entries.push_back({longhand, computed_longhand(realm, element, block, longhand, depth + 1), false, true});
        }
        if (shorthand->kind == ShorthandKind::FlexFlow) {
            // The computed value is the two longhands' computed values,
            // whole; el.style.flexFlow collapses the initial ones instead.
            std::vector<std::string> values;
            for (const auto& entry : parts.entries) {
                if (entry.value.empty()) return std::string();
                values.push_back(entry.value);
            }
            return put_together(name, values, /*collapse=*/false);
        }
        return parts.get(name);
    }
    if (const std::vector<std::string>* parts = parts_of(name)) {
        std::vector<std::string> values;
        for (const auto& part : *parts) values.push_back(computed_longhand(realm, element, block, part, depth + 1));
        return put_together(name, values);
    }
    return computed_longhand(realm, element, block, name, depth);
}

// ---------------------------------------------------------------------------
// The script object
// ---------------------------------------------------------------------------

struct StyleState : JsExotic {
    Realm* realm{nullptr};
    std::shared_ptr<Element> element;
    bool computed{false};
    Block block;
    /// Inline: the attribute text `block` was parsed from.
    std::string source;
    bool has_source{false};
    /// Computed: the tree version `block` was computed at.
    uint64_t version{0};

    void load() {
        if (computed) {
            // An element that is not connected has no computed style: every
            // property, the priority, the length and the cssText read as
            // empty, the way a browser answers it (HTML, window.getComputedStyle).
            if (realm->alive && !element->is_connected()) {
                block.clear();
                version = Node::tree_version();
                return;
            }
            if (version == Node::tree_version() && realm->alive) return;
            if (realm->alive) compute_block(*realm, *element, block);
            version = Node::tree_version();
            return;
        }
        const std::string* attribute = element->attribute("style");
        if (!attribute) {
            block.clear();
            has_source = false;
            source.clear();
            return;
        }
        if (has_source && *attribute == source) return;
        block.clear();
        apply_declarations(block, *attribute, true);
        source = *attribute;
        has_source = true;
    }

    void store() {
        source = block.text(!computed);
        has_source = true;
        element->set_attribute("style", source);
    }

    void refuse_write() const {
        if (computed && realm->alive) {
            throw_dom(*realm, "NoModificationAllowedError", "These styles are computed, and therefore read-only.");
        }
    }

    std::string value_of(const std::string& name) {
        load();
        if (computed && realm->alive && !element->is_connected()) return std::string();
        if (!computed || !realm->alive) return block.get(name, true);
        return computed_value(*realm, *element, block, name, 0);
    }

    void set_value(const std::string& name, const std::string& text, bool important) {
        refuse_write();
        load();
        if (trim(text).empty()) {
            const std::string before = block.text(true);
            block.remove(name, true);
            if (block.text(true) != before) store();
            return;
        }
        const std::string before = block.text(true);
        if (!block.set(name, text, important, true)) return;
        if (!has_source || block.text(true) != before) store();
    }

    bool get(JsObject*, const std::string& name, JsValue& out) override {
        std::size_t index = 0;
        if (parse_index(name, index)) {
            load();
            const Entry* entry = block.listed(index);
            if (!entry) return false;
            out = str(entry->name);
            return true;
        }
        const std::string property = css_name_for(name);
        if (property.empty()) return false;
        out = str(value_of(property));
        return true;
    }
    bool has(JsObject*, const std::string& name) override {
        std::size_t index = 0;
        if (parse_index(name, index)) {
            load();
            return index < block.listed_count();
        }
        return !css_name_for(name).empty();
    }
    bool set(JsObject*, const std::string& name, const JsValue& value) override {
        std::size_t index = 0;
        if (parse_index(name, index)) return true;
        const std::string property = css_name_for(name);
        if (property.empty()) return false;
        set_value(property, to_str_null_empty(value), false);
        return true;
    }
    void keys(JsObject*, std::vector<std::string>& out) override {
        load();
        const std::size_t count = block.listed_count();
        for (std::size_t i = 0; i < count; ++i) out.push_back(std::to_string(i));
    }
    void trace(Heap& heap) override { trace_node(heap, element.get()); }
};

StyleState& this_style() {
    return this_state<StyleState>("CSSStyleDeclaration");
}

std::string property_argument(const JsValue& value) {
    std::string name = to_str(value);
    return is_custom_property(name) ? name : canonicalize_property_name(ascii_lower(name));
}

void setup_style_declaration(Iface& I) {
    I.accessor("cssText",
        [](Realm&) {
            StyleState& state = this_style();
            state.load();
            return str(state.block.text(!state.computed));
        },
        [](Realm&, const JsValue& value) {
            StyleState& state = this_style();
            state.refuse_write();
            state.block.clear();
            apply_declarations(state.block, to_str_null_empty(value), !state.computed);
            state.store();
        });
    I.getter("length", [](Realm&) {
        StyleState& state = this_style();
        state.load();
        return num(static_cast<double>(state.block.listed_count()));
    });
    I.getter("parentRule", [](Realm&) {
        this_style();
        return null();
    });
    I.method("item", 1, [](Realm&, Args args) {
        require_args(args, 1, "CSSStyleDeclaration.item");
        StyleState& state = this_style();
        state.load();
        const Entry* entry = state.block.listed(to_ulong(args[0]));
        return str(entry ? entry->name : std::string());
    });
    I.method("getPropertyValue", 1, [](Realm&, Args args) {
        require_args(args, 1, "CSSStyleDeclaration.getPropertyValue");
        StyleState& state = this_style();
        const std::string name = property_argument(args[0]);
        if (!is_css_property(name)) return str("");
        return str(state.value_of(name));
    });
    I.method("getPropertyPriority", 1, [](Realm&, Args args) {
        require_args(args, 1, "CSSStyleDeclaration.getPropertyPriority");
        StyleState& state = this_style();
        state.load();
        return str(state.block.is_important(property_argument(args[0]), !state.computed) ? "important" : "");
    });
    I.method("setProperty", 2, [](Realm&, Args args) {
        require_args(args, 2, "CSSStyleDeclaration.setProperty");
        StyleState& state = this_style();
        state.refuse_write();
        const std::string name = property_argument(args[0]);
        if (!is_css_property(name)) return undefined();
        const std::string priority = js_arg(args, 2).is_nullish() ? std::string() : ascii_lower(to_str(args[2]));
        if (!priority.empty() && priority != "important") return undefined();
        state.set_value(name, to_str_null_empty(args[1]), priority == "important");
        return undefined();
    });
    I.method("removeProperty", 1, [](Realm&, Args args) {
        require_args(args, 1, "CSSStyleDeclaration.removeProperty");
        StyleState& state = this_style();
        state.refuse_write();
        state.load();
        const std::string name = property_argument(args[0]);
        const std::string before = state.block.text(!state.computed);
        const std::string old = state.block.remove(name, !state.computed);
        if (state.block.text(!state.computed) != before) state.store();
        return str(old);
    });
}

JsValue make_style(Realm& realm, Element& element, bool computed) {
    auto state = std::make_shared<StyleState>();
    state->realm = &realm;
    state->element = shared(element);
    state->computed = computed;
    return realm.make("CSSStyleDeclaration", state);
}

} // namespace

JsValue inline_style(Realm& realm, Element& element) {
    return cached(wrap(realm, element), "style", [&]() { return make_style(realm, element, false); });
}

JsValue computed_style(Realm& realm, Element& element) {
    return make_style(realm, element, true);
}

void set_inline_css_text(Realm& realm, Element& element, const std::string& text) {
    JsValue style = inline_style(realm, element);
    StyleState& state = *state_of<StyleState>(style);
    state.block.clear();
    apply_declarations(state.block, text, true);
    state.store();
}

void register_style_interfaces() {
    register_interface({"CSSStyleDeclaration", nullptr, setup_style_declaration, nullptr, 0, nullptr});
}

} // namespace dom
} // namespace litebird
