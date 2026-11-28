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
#include "litebird/css/DocumentStyles.hpp"
#include "litebird/css/GridGrammar.hpp"
#include "litebird/text/Bidi.hpp"
#include "litebird/js/Promise.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
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

        // Keyword properties that had no rule at all. With no rule,
        // `canonicalize_css_value` treats the declaration as free text: it is
        // stored and `getPropertyValue` hands it back, so
        // `setProperty("table-layout", "totallybogusvalue")` looked accepted and
        // a browser would have dropped the declaration. The set of values is
        // CSS's; a value outside it is not a declaration this engine should
        // keep, because nothing downstream can use it.
        rules["table-layout"] = {ValueKind::Keyword, {"auto", "fixed"}};
        rules["border-collapse"] = {ValueKind::Keyword, {"separate", "collapse"}};
        rules["empty-cells"] = {ValueKind::Keyword, {"show", "hide"}};
        rules["caption-side"] = {ValueKind::Keyword, {"top", "bottom", "block-start", "block-end", "inline-start",
                                                      "inline-end"}};
        rules["writing-mode"] = {ValueKind::Keyword, {"horizontal-tb", "vertical-rl", "vertical-lr", "sideways-rl",
                                                      "sideways-lr", "lr", "lr-tb", "rl", "rl-tb", "tb", "tb-rl"}};
        rules["direction"] = {ValueKind::Keyword, {"ltr", "rtl"}};
        rules["unicode-bidi"] = {ValueKind::Keyword, {"normal", "embed", "isolate", "bidi-override",
                                                      "isolate-override", "plaintext"}};
        rules["mix-blend-mode"] = {ValueKind::Keyword, {"normal", "multiply", "screen", "overlay", "darken",
                                                        "lighten", "color-dodge", "color-burn", "hard-light",
                                                        "soft-light", "difference", "exclusion", "hue",
                                                        "saturation", "color", "luminosity", "plus-lighter"}};
        rules["isolation"] = {ValueKind::Keyword, {"auto", "isolate"}};
        rules["object-fit"] = {ValueKind::Keyword, {"fill", "contain", "cover", "none", "scale-down"}};
        rules["white-space"] = {ValueKind::Keyword, {"normal", "pre", "nowrap", "pre-wrap", "pre-line",
                                                     "break-spaces"}};
        rules["word-break"] = {ValueKind::Keyword, {"normal", "break-all", "keep-all", "break-word"}};
        rules["overflow-wrap"] = {ValueKind::Keyword, {"normal", "break-word", "anywhere"}};
        rules["hyphens"] = {ValueKind::Keyword, {"none", "manual", "auto"}};
        rules["vertical-align"] = {ValueKind::Keyword, {"baseline", "sub", "super", "text-top", "text-bottom",
                                                       "middle", "top", "bottom"}};
        rules["pointer-events"] = {ValueKind::Keyword, {"auto", "none", "visiblePainted", "visibleFill",
                                                        "visibleStroke", "visible", "painted", "fill", "stroke",
                                                        "all", "bounding-box"}};
        rules["user-select"] = {ValueKind::Keyword, {"auto", "text", "none", "contain", "all"}};
        rules["resize"] = {ValueKind::Keyword, {"none", "both", "horizontal", "vertical", "block", "inline"}};
        rules["list-style-type"] = {ValueKind::Keyword, {"none", "disc", "circle", "square", "decimal",
                                                         "decimal-leading-zero", "lower-roman", "upper-roman",
                                                         "lower-alpha", "upper-alpha", "lower-latin",
                                                         "upper-latin", "lower-greek", "georgian", "armenian",
                                                         "disclosure-open", "disclosure-closed"}};
        rules["background-repeat"] = {ValueKind::Keyword, {"repeat", "repeat-x", "repeat-y", "no-repeat", "space",
                                                           "round"}};
        rules["background-attachment"] = {ValueKind::Keyword, {"scroll", "fixed", "local"}};
        rules["background-clip"] = {ValueKind::Keyword, {"border-box", "padding-box", "content-box", "text"}};
        rules["background-origin"] = {ValueKind::Keyword, {"border-box", "padding-box", "content-box"}};
        rules["appearance"] = {ValueKind::Keyword, {"none", "auto", "button", "textfield", "menulist", "checkbox",
                                                    "radio", "searchfield", "textarea", "listbox"}};
        // `font-weight: 700` is as valid as `bold`, so the rule is a keyword
        // *or* a number: `Number` is what the switch below falls through to
        // when no keyword matches, and the keywords are tried first.
        rules["font-weight"] = {ValueKind::Number, {"normal", "bold", "bolder", "lighter"}};
        rules["touch-action"] = {ValueKind::Keyword, {"auto", "none", "manipulation", "pan-x", "pan-y",
                                                      "pan-left", "pan-right", "pan-up", "pan-down",
                                                      "pinch-zoom"}};
        rules["scroll-behavior"] = {ValueKind::Keyword, {"auto", "smooth"}};
        rules["image-rendering"] = {ValueKind::Keyword, {"auto", "smooth", "high-quality", "crisp-edges",
                                                         "pixelated"}};
        rules["justify-content"] = {ValueKind::Keyword, {"normal", "flex-start", "flex-end", "center",
                                                         "space-between", "space-around", "space-evenly",
                                                         "stretch", "start", "end", "left", "right"}};
        rules["align-items"] = {ValueKind::Keyword, {"normal", "stretch", "flex-start", "flex-end", "center",
                                                     "baseline", "start", "end", "self-start", "self-end"}};
        rules["align-self"] = {ValueKind::Keyword, {"auto", "normal", "stretch", "flex-start", "flex-end", "center",
                                                    "baseline", "start", "end", "self-start", "self-end"}};
        rules["align-content"] = {ValueKind::Keyword, {"normal", "flex-start", "flex-end", "center",
                                                       "space-between", "space-around", "space-evenly", "stretch",
                                                       "start", "end", "baseline"}};
        rules["justify-items"] = {ValueKind::Keyword, {"normal", "stretch", "start", "end", "center", "left",
                                                       "right", "baseline", "self-start", "self-end"}};
        // `will-change` has no rule here: its grammar is a comma-separated
        // list of custom idents (Phase 244), checked in normalize_value.
        rules["cursor"] = {ValueKind::Keyword, {"auto", "default", "none", "context-menu", "help", "pointer",
                                                "progress", "wait", "cell", "crosshair", "text", "vertical-text",
                                                "alias", "copy", "move", "no-drop", "not-allowed", "grab",
                                                "grabbing", "e-resize", "n-resize", "ne-resize", "nw-resize",
                                                "s-resize", "se-resize", "sw-resize", "w-resize", "ew-resize",
                                                "ns-resize", "nesw-resize", "nwse-resize", "col-resize",
                                                "row-resize", "all-scroll", "zoom-in", "zoom-out"}};
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


// Splits a value on the commas that are not inside parentheses or quotes.
std::vector<std::string> split_top_level_commas(const std::string& value) {
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;
    char quote = 0;
    for (const char c : value) {
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
            if (depth > 0) --depth;
            current += c;
        } else if (c == ',' && depth == 0) {
            parts.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    parts.push_back(trim(current));
    return parts;
}

// Defined further down with the shorthand machinery it serves.
std::vector<std::string> split_components(const std::string& value);

// A number, or a percentage. A calc()/var() is taken on trust.
bool is_number_or_percentage(const std::string& text) {
    const std::string lower = ascii_lower(trim(text));
    if (is_function_value(lower, "var") || is_function_value(lower, "calc") || is_function_value(lower, "min") ||
        is_function_value(lower, "max") || is_function_value(lower, "clamp") || is_function_value(lower, "env")) {
        return true;
    }
    std::size_t at = 0;
    double number = 0;
    if (!parse_css_number(lower, at, number)) return false;
    const std::string unit = lower.substr(at);
    return unit.empty() || unit == "%";
}

// An angle: a number with deg/rad/grad/turn, or a unitless zero. A
// calc()/var() is taken on trust.
bool is_angle_value(const std::string& text) {
    const std::string lower = ascii_lower(trim(text));
    if (is_function_value(lower, "var") || is_function_value(lower, "calc") || is_function_value(lower, "min") ||
        is_function_value(lower, "max") || is_function_value(lower, "clamp") || is_function_value(lower, "env")) {
        return true;
    }
    std::size_t at = 0;
    double number = 0;
    if (!parse_css_number(lower, at, number)) return false;
    const std::string unit = lower.substr(at);
    if (unit.empty()) return number == 0;
    return unit == "deg" || unit == "rad" || unit == "grad" || unit == "turn";
}

// A length or a percentage (calc()/var() included via normalize_length).
bool is_length_percentage(const std::string& text) {
    std::string normalized;
    return normalize_length(text, normalized);
}

// A length that is not a percentage.
bool is_length_token(const std::string& text) {
    std::string normalized;
    if (!normalize_length(text, normalized)) return false;
    return normalized.empty() || normalized.back() != '%';
}

bool balanced_parens(const std::string& text) {
    int depth = 0;
    char quote = 0;
    for (const char c : text) {
        if (quote) {
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
            if (depth < 0) return false;
        }
    }
    return depth == 0 && !quote;
}

// A position token: a length-percentage, or one of the box keywords.
bool is_position_token(const std::string& lower) {
    return is_length_percentage(lower) || one_of(lower, {"left", "center", "right", "top", "bottom"});
}

// A shape radius: a length-percentage, or a closest/farthest keyword.
bool is_shape_radius(const std::string& lower) {
    return is_length_percentage(lower) || one_of(lower, {"closest-side", "farthest-side"});
}

// If `token` is a function call, gives its lowercase name and its raw
// arguments; false when the token is not one.
bool split_function_token(const std::string& raw_token, std::string& name, std::string& args) {
    const std::string token = trim(raw_token);
    if (token.size() < 4 || token.back() != ')') return false;
    const std::size_t open = token.find('(');
    if (open == std::string::npos || open == 0) return false;
    if (!balanced_parens(token)) return false;
    name = ascii_lower(trim(token.substr(0, open)));
    args = token.substr(open + 1, token.size() - open - 2);
    return !name.empty();
}

bool is_image_function(const std::string& name) {
    return one_of(name, {"url", "linear-gradient", "radial-gradient", "conic-gradient",
                         "repeating-linear-gradient", "repeating-radial-gradient", "repeating-conic-gradient",
                         "image-set", "-webkit-image-set", "image"});
}

std::string join_strings(const std::vector<std::string>& parts, const char* separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += separator;
        out += parts[i];
    }
    return out;
}

// --- contain -------------------------------------------------------------
// contain: none | strict | content | [ size || inline-size || layout ||
// style || paint ] — the standalone keywords may not be mixed, and none of
// the list keywords may appear twice.
bool normalize_contain(const std::string& value, std::string& out) {
    const std::vector<std::string> parts = split_components(ascii_lower(trim(value)));
    if (parts.empty()) return false;
    if (parts.size() == 1 && one_of(parts[0], {"none", "strict", "content"})) {
        out = parts[0];
        return true;
    }
    std::unordered_set<std::string> seen;
    for (const auto& part : parts) {
        if (!one_of(part, {"size", "inline-size", "layout", "style", "paint"})) return false;
        if (!seen.insert(part).second) return false;
    }
    out = join_strings(parts, " ");
    return true;
}

// --- filter --------------------------------------------------------------
// filter: none | <filter-function>+ — whitespace-separated, arguments per
// function. A drop-shadow may carry one color token anywhere in it.
bool normalize_drop_shadow(const std::string& args) {
    const std::vector<std::string> parts = split_components(args);
    if (parts.size() < 2 || parts.size() > 4) return false;
    int lengths = 0, colors = 0;
    for (const auto& token : parts) {
        std::string normalized;
        if (normalize_length(token, normalized)) {
            ++lengths;
            if (lengths > 3) return false;
            if (lengths == 3 && (normalized.back() == '%' || is_negative_length(normalized))) return false;
            continue;
        }
        std::string ignored;
        if (normalize_color(token, ignored)) {
            ++colors;
            continue;
        }
        return false;
    }
    return lengths >= 2 && colors <= 1;
}

bool normalize_filter_function(const std::string& token) {
    std::string name, args;
    if (!split_function_token(token, name, args)) return false;
    const std::vector<std::string> a = split_components(args);
    if (name == "url") {
        return is_function_value(ascii_lower(std::string("url(") + trim(args) + ")"), "url") && !trim(args).empty();
    }
    if (name == "blur") {
        if (a.size() != 1 || !is_length_token(a[0])) return false;
        std::string normalized;
        return normalize_length(a[0], normalized) && !is_negative_length(normalized);
    }
    if (name == "hue-rotate") {
        return a.size() == 1 && is_angle_value(a[0]);
    }
    if (one_of(name, {"brightness", "contrast", "grayscale", "invert", "opacity", "saturate", "sepia"})) {
        return a.size() == 1 && is_number_or_percentage(a[0]);
    }
    if (name == "drop-shadow") {
        return normalize_drop_shadow(args);
    }
    return false;
}

bool normalize_filter(const std::string& value, std::string& out) {
    if (ascii_lower(trim(value)) == "none") {
        out = "none";
        return true;
    }
    const std::vector<std::string> tokens = split_components(value);
    if (tokens.empty()) return false;
    for (const auto& token : tokens) {
        if (!normalize_filter_function(token)) return false;
    }
    out = canonicalize_css_value("filter", join_strings(tokens, " "));
    return true;
}

// --- mask ----------------------------------------------------------------
// mask: none | <mask-layer># — layers separated by top-level commas; each
// layer carries a <mask-reference> (an image function, a url, or `none`)
// and zero or more components (position/size lengths, repeat and mode
// keywords, the `/` separator).
bool normalize_mask_layer(const std::string& layer) {
    const std::vector<std::string> tokens = split_components(layer);
    if (tokens.empty()) return false;
    bool saw_reference = false;
    for (const auto& raw : tokens) {
        const std::string token = ascii_lower(raw);
        if (token == "/") continue;
        std::string name, args;
        if (split_function_token(raw, name, args)) {
            if (!is_image_function(name)) return false;
            if (trim(args).empty()) return false;
            saw_reference = true;
            continue;
        }
        if (one_of(token, {"none", "luminance", "alpha", "match-source", "add", "subtract", "intersect", "exclude",
                           "repeat", "repeat-x", "repeat-y", "no-repeat", "space", "round", "scroll", "fixed",
                           "local", "border-box", "padding-box", "content-box", "margin-box", "fill-box", "stroke-box",
                           "view-box", "no-clip", "border-area", "text", "center", "left", "right", "top", "bottom",
                           "x", "y", "auto", "cover", "contain"})) {
            if (token == "none") saw_reference = true;
            continue;
        }
        if (is_length_percentage(raw)) continue;
        return false;
    }
    return saw_reference;
}

bool normalize_mask(const std::string& value, std::string& out) {
    if (ascii_lower(trim(value)) == "none") {
        out = "none";
        return true;
    }
    const std::vector<std::string> layers = split_top_level_commas(value);
    if (layers.empty()) return false;
    for (const auto& layer : layers) {
        if (!normalize_mask_layer(layer)) return false;
    }
    out = canonicalize_css_value("mask", join_strings(layers, ", "));
    return true;
}

// --- clip-path -----------------------------------------------------------
// clip-path: none | url(...) | [ <basic-shape> || <geometry-box> ] — at
// most one url or shape, at most one geometry box.
bool normalize_polygon(const std::string& args) {
    std::vector<std::string> parts = split_top_level_commas(args);
    if (parts.empty()) return false;
    if (parts.size() > 1 && one_of(ascii_lower(parts[0]), {"nonzero", "evenodd"})) {
        parts.erase(parts.begin());
    }
    if (parts.empty()) return false;
    for (const auto& point : parts) {
        const std::vector<std::string> pair = split_components(point);
        if (pair.size() != 2) return false;
        if (!is_length_percentage(pair[0]) || !is_length_percentage(pair[1])) return false;
    }
    return true;
}

// inset(10px 20px round 4px): 1-4 lengths/percentages, an optional `round`
// followed by 1-4 shape radii. xywh(): exactly 4, then the same optional tail.
bool normalize_inset_xywh(const std::string& args, bool exactly_four) {
    const std::vector<std::string> tokens = split_components(args);
    if (tokens.empty()) return false;
    std::size_t i = 0;
    int lengths = 0;
    while (i < tokens.size() && tokens[i] != "round") {
        const std::string t = ascii_lower(tokens[i]);
        if (!is_length_percentage(t)) return false;
        ++lengths;
        ++i;
    }
    if (lengths == 0) return false;
    if (!exactly_four && lengths > 4) return false;
    if (exactly_four && lengths != 4) return false;
    if (i == tokens.size()) return true;
    ++i;  // past `round`
    int radii = 0;
    while (i < tokens.size()) {
        const std::string t = ascii_lower(tokens[i]);
        if (!is_shape_radius(t)) return false;
        ++radii;
        ++i;
    }
    return radii >= 1 && radii <= 4;
}

bool normalize_circle_ellipse(const std::string& args, bool two_radii) {
    const std::vector<std::string> tokens = split_components(args);
    if (tokens.empty()) return true;  // `circle()` defaults
    std::size_t i = 0;
    std::size_t radii = 0;
    const std::size_t max_radii = two_radii ? 2 : 1;
    while (i < tokens.size() && ascii_lower(tokens[i]) != "at" && radii < max_radii) {
        const std::string t = ascii_lower(tokens[i]);
        if (!is_shape_radius(t)) return false;
        ++radii;
        ++i;
    }
    if (two_radii && (radii == 1)) return false;  // ellipse takes both radii or none
    if (i < tokens.size()) {
        if (ascii_lower(tokens[i]) != "at") return false;
        ++i;
        // A position is one or two tokens: `at center`, `at left top`.
        const std::size_t remaining = tokens.size() - i;
        if (remaining < 1 || remaining > 2) return false;
        for (; i < tokens.size(); ++i) {
            if (!is_position_token(ascii_lower(tokens[i]))) return false;
        }
        return true;
    }
    return true;
}

bool normalize_shape_function(const std::string& name, const std::string& args) {
    const std::string trimmed = trim(args);
    if (name == "path") {
        if (trimmed.size() < 2) return false;
        const char q = trimmed.front();
        return (q == '\'' || q == '"') && trimmed.back() == q;
    }
    if (name == "polygon") return normalize_polygon(trimmed);
    if (name == "inset") return normalize_inset_xywh(trimmed, false);
    if (name == "xywh") return normalize_inset_xywh(trimmed, true);
    if (name == "circle") return normalize_circle_ellipse(trimmed, false);
    if (name == "ellipse") return normalize_circle_ellipse(trimmed, true);
    if (name == "shape") {
        // The most expressive shape function (css-shapes-2). Checked for
        // structure — balanced parentheses, a small safe charset — rather
        // than modelled, so a valid `shape()` is never refused.
        if (!balanced_parens(trimmed)) return false;
        for (const char c : trimmed) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                            c == ' ' || c == '\t' || c == '-' || c == '+' || c == '.' || c == '%' || c == ',' ||
                            c == '(' || c == ')' || c == '\'' || c == '"' || c == '/' || c == '_';
            if (!ok) return false;
        }
        return true;
    }
    return false;
}

bool normalize_clip_path(const std::string& value, std::string& out) {
    if (ascii_lower(trim(value)) == "none") {
        out = "none";
        return true;
    }
    const std::vector<std::string> tokens = split_components(value);
    if (tokens.empty()) return false;
    bool saw_url = false, saw_shape = false, saw_box = false;
    for (const auto& raw : tokens) {
        const std::string token = ascii_lower(trim(raw));
        std::string name, args;
        if (split_function_token(raw, name, args)) {
            if (name == "url") {
                if (tokens.size() != 1 || trim(args).empty()) return false;
                saw_url = true;
                continue;
            }
            if (saw_shape || !one_of(name, {"inset", "circle", "ellipse", "polygon", "path", "xywh", "shape"})) {
                return false;
            }
            if (!normalize_shape_function(name, args)) return false;
            saw_shape = true;
            continue;
        }
        if (one_of(token, {"margin-box", "border-box", "padding-box", "content-box", "fill-box", "stroke-box",
                           "view-box"})) {
            if (saw_box) return false;
            saw_box = true;
            continue;
        }
        return false;
    }
    if (saw_url && (saw_shape || saw_box)) return false;
    out = canonicalize_css_value("clip-path", join_strings(tokens, " "));
    return true;
}

// --- will-change ---------------------------------------------------------
// will-change: auto | <custom-ident># — any custom ident, or the two
// non-property features. `auto` must stand alone; no ident may be a
// CSS-wide keyword.
bool is_custom_ident(const std::string& lower) {
    if (lower.empty()) return false;
    for (std::size_t i = 0; i < lower.size(); ++i) {
        const char c = lower[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
        if (i == 0 && (c >= '0' && c <= '9')) return false;
    }
    if (lower[0] == '-' && lower.size() == 1) return false;
    return true;
}

bool normalize_will_change(const std::string& value, std::string& out) {
    if (ascii_lower(trim(value)) == "auto") {
        out = "auto";
        return true;
    }
    const std::vector<std::string> parts = split_top_level_commas(value);
    if (parts.empty()) return false;
    std::vector<std::string> idents;
    for (const auto& raw : parts) {
        const std::string token = ascii_lower(trim(raw));
        if (token.empty() || token == "auto") return false;
        if (is_global_keyword(token)) return false;
        if (!is_custom_ident(token)) return false;
        idents.push_back(token);
    }
    out = join_strings(idents, ", ");
    return true;
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
    if (is_global_keyword(lower)) {
        out = lower;
        return true;
    }
    if (is_function_value(lower, "var")) {
        out = value;
        return true;
    }
    if (property == "contain") return normalize_contain(value, out);
    if (property == "filter") return normalize_filter(value, out);
    if (property == "mask") return normalize_mask(value, out);
    if (property == "clip-path") return normalize_clip_path(value, out);
    if (property == "will-change") return normalize_will_change(value, out);
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

// One rule of a sheet: a style rule, or a `CSSMediaRule` and the rules its
// block holds. A script's CSSOM reads and writes the same object the cascade
// reads: `rule.style` is a CSSStyleDeclaration over `declarations`, and
// `selectorText` reparsed into `selector`.
struct SheetRule;
using SheetRulePtr = std::shared_ptr<SheetRule>;

struct SheetRule {
    enum class Kind { Style, Media, Keyframes, Keyframe, FontFace };

    Kind kind{Kind::Style};
    /// A style rule: what `selectorText` reads, and the rule the cascade
    /// matches.
    SelectorList selector;
    std::string selector_text;
    /// A style rule: its declaration block.
    std::vector<RawDeclaration> declarations;
    /// A `CSSMediaRule`: its query, as written, and the rules inside it.
    /// The cascade applies those only when the query matches the window.
    std::string media_text;
    /// A `CSSKeyframesRule`: the name it declares. A `CSSKeyframeRule`: the
    /// selectors it was written with, `0%` or `50%, 100%`.
    std::string keyframes_name;
    std::string key_text;
    /// A `CSSKeyframesRule`: its frames, as `children`. Nothing else has any.
    std::vector<SheetRulePtr> children;
    /// The rule's identity: `cssRules[0] === cssRules.item(0)` holds because
    /// the object is the one made for this number, wherever the rule now is
    /// in the list. Children are numbered from the sheet's one counter, so an
    /// id names a rule anywhere in the sheet.
    uint64_t id{0};
    /// True when the rule came from the element's text. A rule a script
    /// inserted stays when that text changes; this one goes with it.
    bool from_text{false};
    bool font_face_parsed{false};

    /// The answer `media_text` was last given, and the window it was given
    /// for: the cascade asks about every element, and the text is parsed once
    /// per window rather than once per element. -1 is "not asked yet".
    mutable int cached_match{-1};
    mutable float cached_width{0.0f};
    mutable float cached_height{0.0f};

    bool is_font_face() const { return kind == Kind::FontFace; }
    bool is_media() const { return kind == Kind::Media; }
    bool is_keyframes() const { return kind == Kind::Keyframes; }
    bool is_keyframe() const { return kind == Kind::Keyframe; }
    /// Whether the cascade has anything to say about the rule: a style rule,
    /// or the block of a `@media` rule that is looked into. A `@keyframes`
    /// rule and its frames are for the animation engine and the CSSOM, not
    /// for matching selectors against an element.
    bool is_style_or_media() const { return kind == Kind::Style || kind == Kind::Media; }

    void forget_media_answer() {
        cached_match = -1;
        cached_width = 0.0f;
        cached_height = 0.0f;
    }
};

struct ParsedSheet {
    std::vector<SheetRulePtr> rules;
    /// Bumped by every CSSOM write, so a style object knows to read again.
    uint64_t generation{0};
    /// The number the next rule of this sheet gets.
    uint64_t next_rule_id{1};

    /// Gives every rule an id, after the text was parsed — the rules of an
    /// `@media` block too, so that `media.cssRules[0]` has one of its own.
    void number_rules() {
        const std::function<void(std::vector<SheetRulePtr>&)> number = [&](std::vector<SheetRulePtr>& list) {
            for (auto& rule : list) {
                if (!rule) continue;
                if (rule->id == 0) {
                    rule->id = next_rule_id++;
                    rule->from_text = true;
                }
                number(rule->children);
            }
        };
        number(rules);
    }
};

uint64_t sheet_generation(const std::shared_ptr<ParsedSheet>& sheet) {
    return sheet ? sheet->generation : 0;
}

/// Bumped by every CSSOM write in the realm. A computed style is worked out
/// again when it changes, because `insertRule` changes what the cascade says
/// without changing the tree.
uint64_t s_style_generation = 0;

uint64_t style_generation() { return s_style_generation; }

/// Defined after the CSSOM serializers it needs; it writes an adopted sheet
/// back to CSS so the layout's cascade reads the edit (Phase 457). `realm` is
/// allowed to be null for a write outside a realm.
void refresh_adopted_record(Realm& realm, const std::shared_ptr<ParsedSheet>& sheet);

void touch_sheet(const std::shared_ptr<ParsedSheet>& sheet) {
    if (sheet) ++sheet->generation;
    ++s_style_generation;
    // An adopted sheet's text is what the layout reads, so a write to it has to
    // be written back. Nothing happens for a sheet no document adopted.
    if (sheet) refresh_adopted_record(current_realm(), sheet);
}

/// The frames of a `@keyframes` block: `0%, 50% { … }` each, until the brace
/// that ends the block. `at` is on the `{` that opened it.
void parse_keyframe_blocks(const std::string& css, std::size_t& at, std::vector<SheetRulePtr>& out);

void parse_rules(const std::string& css, std::size_t& at, std::vector<SheetRulePtr>& out, bool nested) {
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
            const bool is_media = lower.compare(0, 6, "@media") == 0;
            const bool is_keyframes = lower.compare(0, 10, "@keyframes") == 0 ||
                                      lower.compare(0, 18, "@-webkit-keyframes") == 0;
            const bool applies = lower.compare(0, 9, "@supports") == 0 || lower.compare(0, 6, "@layer") == 0;
            if (lower == "@font-face") {
                // A font-face is a declaration block, not a selector or a
                // grouping rule. Quoted braces do not close its block.
                std::string body;
                int depth = 1;
                char quote = 0;
                while (at < css.size() && depth > 0) {
                    char c = css[at++];
                    if (quote) {
                        body += c;
                        if (c == '\\' && at < css.size()) body += css[at++];
                        else if (c == quote) quote = 0;
                    } else if (c == '/' && at < css.size() && css[at] == '*') {
                        auto end = css.find("*/", at + 1);
                        if (end == std::string::npos) { at = css.size(); break; }
                        body += ' ';
                        at = end + 2;
                    } else if (c == '"' || c == '\'') {
                        quote = c; body += c;
                    } else if (c == '{') {
                        ++depth; body += c;
                    } else if (c == '}') {
                        if (--depth) body += c;
                    } else body += c;
                }
                auto rule = std::make_shared<SheetRule>();
                rule->kind = SheetRule::Kind::FontFace;
                rule->declarations = parse_declarations(body);
                out.push_back(std::move(rule));
            } else if (is_keyframes) {
                // A `@keyframes` block is a rule of the sheet, as it is in the
                // language: `cssRules` lists it, `rule.name` reads the name, and
                // its frames are a `CSSRuleList` of their own. The cascade never
                // matches an element against it.
                auto rule = std::make_shared<SheetRule>();
                rule->kind = SheetRule::Kind::Keyframes;
                const std::size_t keyword_end = prelude.find_first_of(" \t\r\n\f");
                std::string name =
                    keyword_end == std::string::npos ? std::string() : trim(prelude.substr(keyword_end));
                if (name.size() >= 2 && (name.front() == '"' || name.front() == '\'') &&
                    name.back() == name.front()) {
                    name = name.substr(1, name.size() - 2);
                }
                rule->keyframes_name = name;
                parse_keyframe_blocks(css, at, rule->children);
                if (!name.empty()) out.push_back(std::move(rule));
            } else if (is_media) {
                // A `@media` block is a rule of the sheet now, whether its
                // query matches or not: `cssRules` lists it, and the cascade
                // asks the query. `@supports` and `@layer` hold rules
                // unconditionally as far as this sheet is concerned, so their
                // rules are the sheet's own, as they always were.
                auto rule = std::make_shared<SheetRule>();
                rule->kind = SheetRule::Kind::Media;
                rule->media_text = trim(prelude.substr(6));
                parse_rules(css, at, rule->children, true);
                out.push_back(std::move(rule));
            } else if (applies) {
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
        auto rule = std::make_shared<SheetRule>();
        if (!SelectorList::parse(prelude, rule->selector)) continue;
        rule->selector_text = prelude;
        rule->declarations = parse_declarations(body);
        out.push_back(std::move(rule));
    }
}

void parse_keyframe_blocks(const std::string& css, std::size_t& at, std::vector<SheetRulePtr>& out) {
    while (at < css.size()) {
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
            if (c == '}') {
                ++at;
                return;
            }
            if (c == ';') {
                ++at;
                prelude.clear();
                continue;
            }
            prelude += c;
            ++at;
        }
        if (!opened) return;
        std::string body;
        int depth = 1;
        while (at < css.size()) {
            const char c = css[at++];
            if (c == '{') ++depth;
            else if (c == '}' && --depth == 0) break;
            body += c;
        }
        auto frame = std::make_shared<SheetRule>();
        frame->kind = SheetRule::Kind::Keyframe;
        frame->key_text = trim(prelude);
        frame->declarations = parse_declarations(body);
        if (!frame->key_text.empty()) out.push_back(std::move(frame));
    }
}

// ---------------------------------------------------------------------------
// The CSS Object Model
// ---------------------------------------------------------------------------
//
// A sheet object is made once for a <style> element and lives as long as the
// element can be read: `style.sheet` is the same object every time, and it is
// still there once the element has left the tree, as browsers have it.
//
// The rules of a live sheet and the rules the cascade applies are one list:
// a sheet the cascade reads is the same ParsedSheet an `insertRule` writes, so
// a rule a script adds styles the page on the next `getComputedStyle`.

struct SheetState : JsExotic {
    Realm* realm{nullptr};
    /// Null for a sheet no element owns (`new CSSStyleSheet()`).
    std::shared_ptr<Element> owner;
    std::shared_ptr<ParsedSheet> sheet;
    bool own_sheet{false};
    /// The value `disabled` was given, as written.
    bool disabled{false};
    /// Whether the cascade reads this sheet: a <style> element in a tree, and
    /// not `disabled`.
    bool applied{false};
    std::string href;

    /// `disabled` is the cascade's, not this object's: it is stored where
    /// `compute_block` looks, by the sheet.
    void set_applied(bool value);
};

struct RuleState : JsExotic {
    std::shared_ptr<ParsedSheet> sheet;
    /// The rule itself. A rule that leaves its sheet keeps it: it is the rule
    /// it was, and what it held is still readable, with the tree no longer
    /// applying it.
    SheetRulePtr rule;
    /// The `@media` rule it is inside, for `parentRule`. Null at the top of
    /// the sheet.
    SheetRulePtr parent;
};

/// A list of rules: the sheet's own when `parent` is null, and otherwise the
/// `cssRules` of a `@media` rule — the sheet's when it is still in the sheet,
/// and the rule's own after it has left it.
struct RuleListState : JsExotic {
    std::shared_ptr<ParsedSheet> sheet;
    SheetRulePtr parent;

    bool get(JsObject*, const std::string& name, JsValue& out) override;
    bool has(JsObject*, const std::string& name) override;
    void keys(JsObject*, std::vector<std::string>& out) override;
};

/// `media.mediaText`, `media.length` and `media.item(i)` over a `@media`
/// rule's query: the alternatives are its comma-separated parts, each
/// trimmed, as a browser writes them back.
struct MediaListState : JsExotic {
    std::shared_ptr<ParsedSheet> sheet;
    SheetRulePtr rule;

    bool get(JsObject*, const std::string& name, JsValue& out) override;
    bool has(JsObject*, const std::string& name) override;
    void keys(JsObject*, std::vector<std::string>& out) override;
};

// The rule objects of a realm, one per rule, so that a rule object is the same
// object every time it is read.
struct RuleObjectEntry {
    std::weak_ptr<ParsedSheet> sheet;
    JsValue object;
};

struct RuleObjectsPart : RealmPart {
    std::map<std::pair<const ParsedSheet*, const SheetRule*>, RuleObjectEntry> objects;
    std::map<const ParsedSheet*, RuleObjectEntry> sheets;
    void trace(Heap& heap) override {
        for (auto& entry : objects) heap.mark_value(entry.second.object);
        for (auto& entry : sheets) heap.mark_value(entry.second.object);
    }
};

std::string serialize_selector_list(const SelectorList& list);
std::string serialize_declarations(const std::vector<RawDeclaration>& declarations);
std::string rule_block_text(Realm& realm, RuleState& state);

/// Parses `text` as one rule for `insertRule`: a style rule, or an `@media`
/// rule and the rules in its block. False when it is not one.
bool parse_inserted_rule(const std::string& text, SheetRulePtr& out);

/// Gives every rule of `list` its own number from the sheet's counter, and
/// leaves the ones that already have one alone.
void number_inserted_rules(ParsedSheet& sheet, std::vector<SheetRulePtr>& list);

std::string rule_css_text(Realm& realm, RuleState& state);

/// The declarations a rule currently has, or null for a rule that has none.
std::vector<RawDeclaration>* rule_declarations(RuleState& state) {
    if (!state.rule) return nullptr;
    return &state.rule->declarations;
}

const std::vector<RawDeclaration>* rule_declarations(const RuleState& state) {
    return rule_declarations(const_cast<RuleState&>(state));
}

std::string rule_selector_text(const RuleState& state) {
    if (!state.rule) return std::string();
    return state.rule->selector_text;
}

JsValue make_sheet(Realm& realm, std::shared_ptr<ParsedSheet> sheet, std::shared_ptr<Element> owner,
                   bool own_sheet, bool applied, std::string href);
JsValue sheet_object(Realm& realm, Element& element);
JsValue make_rule_list(Realm& realm, std::shared_ptr<ParsedSheet> sheet, SheetRulePtr parent = nullptr);
JsValue make_rule(Realm& realm, std::shared_ptr<ParsedSheet> sheet, SheetRulePtr parent, const SheetRulePtr& rule);
JsValue make_media_list(Realm& realm, std::shared_ptr<ParsedSheet> sheet, SheetRulePtr rule);
JsValue make_rule_style(Realm& realm, std::shared_ptr<RuleState> rule);

// One sheet object of a realm, made for a <style> element. `version` is the
// tree version its `applied` was worked out at.
struct SheetEntry {
    JsValue object;
    std::shared_ptr<Element> owner;
    std::shared_ptr<ParsedSheet> sheet;
    /// The element's text the sheet's rules were read from.
    std::string source;
    bool applied{false};
    uint64_t version{0};
};

struct StylePart : RealmPart {
    std::unordered_map<std::string, std::shared_ptr<ParsedSheet>> sheets;
    /// The sheet object of each <style> element, so that `sheet` is stable.
    std::map<const Element*, SheetEntry> sheet_objects;
    /// A sheet a script has set `disabled` on, by the sheet it is: a sheet's
    /// text can be replaced by another (an entry made again), and `disabled`
    /// is what was written on the object, not on the element.
    std::map<const ParsedSheet*, bool> disabled_sheets;
    /// An adopted sheet's record parsed into this cascade's own model, by the
    /// record. Kept and rebuilt only when the record's generation moves, so a
    /// `getComputedStyle` on a thousand elements does not parse the same sheet
    /// a thousand times.
    std::map<const Document::AdoptedStyleSheet*, std::shared_ptr<ParsedSheet>> adopted_parsed;

    bool is_disabled(const ParsedSheet* sheet) const {
        auto found = disabled_sheets.find(sheet);
        return found != disabled_sheets.end() && found->second;
    }
    void set_disabled(const ParsedSheet* sheet, bool disabled) {
        if (disabled) disabled_sheets[sheet] = true;
        else disabled_sheets.erase(sheet);
    }
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
    sheet->number_rules();
    if (part.sheets.size() >= 64) part.sheets.clear();
    part.sheets[css] = sheet;
    return sheet;
}

/// Whether the cascade reads an element's sheet: it is in a document, and its
/// `media` is not print.
bool sheet_is_applied(Element& element) {
    if (!element.is_connected()) return false;
    return ascii_lower(element.get_attribute("media")).find("print") == std::string::npos;
}

// The sheet one element has at this state of the tree: the one it had before
// while its text has not changed, and a new one when it has. A sheet a script
// has written to is not thrown away with its text: an `insertRule` survives
// the next `getComputedStyle`.
/// The text of the sheet an element owns. A `<style>` owns its text; a
/// `<link rel="stylesheet">` owns whatever its href points at, which is why a
/// `<link>` used to contribute nothing here -- it has no text content, so the
/// sheet a page actually links was empty and every rule in it was missing from
/// what `getComputedStyle` reported, while the layout painted them. The load
/// goes through DocumentStyles so that the two cascades cannot disagree.
std::string sheet_text_of(Element& element) {
    if (element.tag_name() != "link") return element.text_content();
    std::shared_ptr<Document> doc = element.owner_document();
    if (!doc) return std::string();
    std::string css;
    if (!DocumentStyles::load_linked_stylesheet(element, *doc, &doc->stylesheet_cache(), css)) {
        return std::string();
    }
    return css;
}

std::shared_ptr<ParsedSheet> sheet_of(Realm& realm, Element& element) {
    StylePart& part = realm.part<StylePart>();
    SheetEntry& entry = part.sheet_objects[&element];
    if (!entry.sheet) {
        entry.sheet = std::make_shared<ParsedSheet>();
        entry.owner = shared(element);
        std::size_t at = 0;
        parse_rules(sheet_text_of(element), at, entry.sheet->rules, false);
        entry.sheet->number_rules();
        entry.applied = sheet_is_applied(element);
        entry.version = Node::tree_version();
        entry.source = sheet_text_of(element);
        return entry.sheet;
    }
    // The sheet is one object for as long as the element has one: its rules
    // are the element's text (for a `<link>`, the text it points at), read
    // again when that changes, and an `insertRule` is a rule of the sheet like
    // any other.
    const std::string text = sheet_text_of(element);
    if (text != entry.source) {
        std::vector<SheetRulePtr> parsed;
        std::size_t at = 0;
        parse_rules(text, at, parsed, false);
        std::vector<SheetRulePtr> kept;
        for (auto& rule : entry.sheet->rules) {
            if (!rule || rule->from_text) continue; // it came from the text that has gone
            kept.push_back(std::move(rule));
        }
        entry.sheet->rules = std::move(parsed);
        entry.sheet->number_rules();
        // What a script inserted stays, after the rules of the new text, as a
        // rule inserted at the end would be.
        for (auto& rule : kept) entry.sheet->rules.push_back(std::move(rule));
        entry.source = text;
        touch_sheet(entry.sheet);
    }
    return entry.sheet;
}

JsValue sheet_object(Realm& realm, Element& element) {
    StylePart& part = realm.part<StylePart>();
    // Read again first: the element's text is the sheet's rules, and the
    // object handed back is the one made for it.
    std::shared_ptr<ParsedSheet> sheet = sheet_of(realm, element);
    auto found = part.sheet_objects.find(&element);
    if (found != part.sheet_objects.end() && found->second.object.is_object()) {
        if (found->second.version != Node::tree_version()) {
            found->second.applied = sheet_is_applied(element);
            found->second.version = Node::tree_version();
        }
        return found->second.object;
    }
    const bool applied = sheet_is_applied(element);
    JsValue object = make_sheet(realm, sheet, shared(element), false, applied, std::string());
    SheetEntry& entry = part.sheet_objects[&element];
    entry.owner = shared(element);
    entry.sheet = sheet;
    entry.applied = applied;
    entry.version = Node::tree_version();
    entry.object = object;
    return object;
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
        } else if (is_html_element(node, "link")) {
            // A linked stylesheet takes part in the cascade like an inline one.
            // Leaving it out was the defect: a page whose only CSS is a
            // `<link>` had a layout painted from rules that script was never
            // told about, so `getComputedStyle` and the painted box disagreed.
            auto& element = static_cast<Element&>(node);
            if (DocumentStyles::is_stylesheet_rel(element.get_attribute("rel"))) {
                const std::string media = ascii_lower(element.get_attribute("media"));
                if (media.find("print") == std::string::npos) part.style_elements.push_back(shared(element));
            }
        }
        return true;
    });
    // Parsed once for each state of the tree, not once for each element asked about.
    part.style_sheets.clear();
    for (const auto& style : part.style_elements) part.style_sheets.push_back(sheet_of(realm, *style));
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
    if (element.tag_name() == "dialog") return element.has_attribute("open") ? "block" : "none";
    static const std::unordered_map<std::string, const char*> s_table = [] {
        std::unordered_map<std::string, const char*> table;
        for (const char* tag : {"html", "body", "div", "p", "ul", "ol", "dl", "dt", "dd", "h1", "h2", "h3", "h4",
                                "h5", "h6", "section", "article", "aside", "header", "footer", "nav", "main",
                                "form", "fieldset", "legend", "pre", "blockquote", "figure", "figcaption", "hr",
                                "address", "details", "summary", "center", "dir", "menu", "hgroup", "optgroup",
                                "option", "search", "listing", "xmp", "plaintext", "frameset", "frame"}) {
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
bdo { unicode-bidi: bidi-override }
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

// ---------------------------------------------------------------------------
// Media queries, as the cascade asks them
// ---------------------------------------------------------------------------

/// The parts of a media query list: its comma-separated alternatives, each
/// trimmed, with the empty ones dropped. A query written with a function
/// (`(width > 100px)` has none here) is split on the top-level commas only.
std::vector<std::string> media_alternatives(const std::string& query) {
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    char quote = 0;
    for (const char c : query) {
        if (quote) {
            current += c;
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            current += c;
            continue;
        }
        if (c == '(') ++depth;
        else if (c == ')' && depth > 0) --depth;
        if (c == ',' && depth == 0) {
            const std::string part = trim(current);
            if (!part.empty()) out.push_back(part);
            current.clear();
            continue;
        }
        current += c;
    }
    const std::string part = trim(current);
    if (!part.empty()) out.push_back(part);
    return out;
}

/// The terms of one alternative, split on the top-level ` and `.
std::vector<std::string> media_terms(const std::string& alternative) {
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    std::size_t i = 0;
    while (i < alternative.size()) {
        const char c = alternative[i];
        if (c == '(') {
            ++depth;
            current += c;
            ++i;
            continue;
        }
        if (c == ')') {
            if (depth > 0) --depth;
            current += c;
            ++i;
            continue;
        }
        if (depth == 0 && is_ascii_space(c)) {
            std::size_t word_start = i;
            while (word_start < alternative.size() && is_ascii_space(alternative[word_start])) ++word_start;
            std::size_t word_end = word_start;
            while (word_end < alternative.size() && !is_ascii_space(alternative[word_end]) &&
                   alternative[word_end] != '(') {
                ++word_end;
            }
            if (ascii_lower(alternative.substr(word_start, word_end - word_start)) == "and") {
                const std::string part = trim(current);
                if (!part.empty()) out.push_back(part);
                current.clear();
                i = word_end;
                continue;
            }
            current += alternative.substr(i, word_end - i);
            i = word_end;
            continue;
        }
        current += c;
        ++i;
    }
    const std::string part = trim(current);
    if (!part.empty()) out.push_back(part);
    return out;
}

/// A length as the window counts it: `px`, `em`/`rem` at 16px, and a bare
/// number, which a media query takes for px. False when it is not a length.
bool media_length(const std::string& text, double& out) {
    const std::string value = trim(text);
    if (value.empty()) return false;
    char* end = nullptr;
    const double number = std::strtod(value.c_str(), &end);
    if (end == value.c_str()) return false;
    std::string unit = ascii_lower(trim(std::string(end)));
    if (unit.empty() || unit == "px") out = number;
    else if (unit == "em" || unit == "rem") out = number * 16.0;
    else if (unit == "pt") out = number * (96.0 / 72.0);
    else if (unit == "in") out = number * 96.0;
    else return false;
    return true;
}

/// One parenthesised feature: `(min-width: 600px)`, `(orientation: landscape)`,
/// `(hover)`. False when this engine does not know it, which the standard
/// says is the same answer as a feature that does not match.
bool media_feature_matches(const std::string& feature, float viewport_width, float viewport_height) {
    std::string name = feature;
    std::string value;
    const auto colon = feature.find(':');
    if (colon != std::string::npos) {
        name = feature.substr(0, colon);
        value = trim(feature.substr(colon + 1));
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
            value = value.substr(1, value.size() - 2);
        }
    }
    name = ascii_lower(trim(name));
    const std::string lower_value = ascii_lower(value);

    if (name == "min-width" || name == "max-width" || name == "width") {
        double wanted = 0;
        if (!media_length(value, wanted)) return false;
        const double actual = viewport_width;
        if (name == "width") return std::abs(actual - wanted) < 1e-6;
        return name == "min-width" ? actual >= wanted - 1e-6 : actual <= wanted + 1e-6;
    }
    if (name == "min-height" || name == "max-height" || name == "height") {
        double wanted = 0;
        if (!media_length(value, wanted)) return false;
        const double actual = viewport_height;
        if (name == "height") return std::abs(actual - wanted) < 1e-6;
        return name == "min-height" ? actual >= wanted - 1e-6 : actual <= wanted + 1e-6;
    }
    if (name == "orientation") {
        const bool landscape = viewport_width >= viewport_height;
        if (lower_value == "landscape") return landscape;
        if (lower_value == "portrait") return !landscape;
        return false;
    }
    if (name == "resolution" || name == "min-resolution" || name == "max-resolution") {
        // The window is a 96 dpi screen, which is 1dppx.
        double wanted = 0;
        const std::string trimmed = trim(lower_value);
        char* end = nullptr;
        const double number = std::strtod(trimmed.c_str(), &end);
        if (end == trimmed.c_str()) return false;
        const std::string unit = trim(std::string(end));
        if (unit == "dpi") wanted = number / 96.0;
        else if (unit == "dpcm") wanted = number * (2.54 / 96.0);
        else if (unit == "dppx" || unit == "x") wanted = number;
        else return false;
        if (name == "resolution") return std::abs(wanted - 1.0) < 1e-6;
        return name == "min-resolution" ? 1.0 >= wanted - 1e-6 : 1.0 <= wanted + 1e-6;
    }
    if (name == "prefers-color-scheme") return lower_value == "light";
    if (name == "prefers-reduced-motion") return lower_value == "no-preference";
    if (name == "prefers-contrast") return lower_value == "no-preference";
    if (name == "hover" || name == "any-hover") return lower_value == "hover" || value.empty();
    if (name == "pointer" || name == "any-pointer") {
        return lower_value == "fine" || (value.empty() && name == "pointer");
    }
    if (name == "scripting") return lower_value == "enabled" || (value.empty());
    if (name == "color") {
        double wanted = 0;
        if (value.empty()) return true; // `(color)`: a colour screen, and it is one
        if (!media_length(value, wanted)) {
            char* end = nullptr;
            wanted = std::strtod(value.c_str(), &end);
            if (end == value.c_str()) return false;
        }
        return wanted >= 8;
    }
    return false;
}

/// Whether a media query list matches this window. The features are the ones
/// this engine can answer for: the media type, width and height with `min-`
/// and `max-`, orientation, resolution (a 96 dpi screen, so `1dppx`), the
/// user preferences the window already answers `matchMedia` with, and
/// `(color)`. An unknown feature does not match, as the standard has it, and
/// an empty query matches everything.
bool media_query_matches(const std::string& query, float viewport_width, float viewport_height) {
    const std::vector<std::string> alternatives = media_alternatives(query);
    if (alternatives.empty()) return true;
    for (const auto& alternative : alternatives) {
        std::vector<std::string> terms = media_terms(alternative);
        if (terms.empty()) continue;
        bool negated = false;
        const std::string first = ascii_lower(terms.front());
        if (first.compare(0, 4, "not ") == 0) {
            negated = true;
            terms.front() = trim(terms.front().substr(4));
        } else if (first.compare(0, 5, "only ") == 0) {
            terms.front() = trim(terms.front().substr(5));
        }
        bool matches = true;
        for (const auto& term : terms) {
            const std::string text = trim(term);
            if (text.empty()) continue;
            if (text.front() == '(') {
                std::string inside = trim(text.substr(1, text.back() == ')' ? text.size() - 2 : text.size() - 1));
                if (inside.empty()) {
                    matches = false;
                    break;
                }
                if (inside.front() == '(') {
                    // `((min-width: 600px))` is the same question.
                    inside = trim(inside.substr(1, inside.back() == ')' ? inside.size() - 2 : inside.size() - 1));
                }
                if (!media_feature_matches(inside, viewport_width, viewport_height)) {
                    matches = false;
                    break;
                }
                continue;
            }
            // A media type.
            const std::string type = ascii_lower(text);
            if (type != "all" && type != "screen") {
                matches = false;
                break;
            }
        }
        if (negated) matches = !matches;
        if (matches) return true;
    }
    return false;
}

/// Whether a `@media` rule's query holds for this window, read once per
/// window: a sheet's block is asked about by every element the cascade
/// computes, and its text does not change between them.
bool media_rule_matches(const SheetRule& rule, float viewport_width, float viewport_height) {
    if (rule.cached_match < 0 || rule.cached_width != viewport_width || rule.cached_height != viewport_height) {
        rule.cached_match = media_query_matches(rule.media_text, viewport_width, viewport_height) ? 1 : 0;
        rule.cached_width = viewport_width;
        rule.cached_height = viewport_height;
    }
    return rule.cached_match == 1;
}

// What the cascade gives one element: the declarations that apply to it, the
// last word on each property. In order of weight: the element's defaults,
// the rules of the document's style elements, the style attribute, and then
// the `!important` ones of the same three; within the rules by specificity,
// then by position.
void compute_block(Realm& realm, const Element& element, Block& block, const Node* root = nullptr) {
    block.clear();
    float viewport_width = 1024.0f;
    float viewport_height = 768.0f;
    if (auto document = element.owner_document()) {
        viewport_width = document->viewport_width();
        viewport_height = document->viewport_height();
    }
    if (element.has_attribute("hidden") && !element.is_foreign()) block.set("display", "none", false);
    else block.set("display", default_display(element), false);

    // The `dir` attribute is a presentational hint, set before the matched
    // rules are stored: `store` replaces a same-name entry in place, so any
    // author declaration of `direction` — `inherit` included — wins over it,
    // and with no declaration the hint answers instead of the inherited
    // value. `dir` is an HTML enumerated attribute: ASCII case-insensitive.
    // `auto` is the element-level first strong character (HTML §3.2.5) —
    // one answer over the element's whole text, not one per paragraph.
    if (!element.is_foreign()) {
        const std::string dir = element.get_attribute("dir");
        if (ascii_lower(dir) == "ltr") block.set("direction", "ltr", false);
        else if (ascii_lower(dir) == "rtl") block.set("direction", "rtl", false);
        else if (ascii_lower(dir) == "auto") {
            const int level = bidi_paragraph_utf8(element.text_content(), -1).base_level;
            block.set("direction", level == 1 ? "rtl" : "ltr", false);
        }
    }

    std::vector<Matched> matched;
    // The rules of a sheet, and for a `@media` block whose query matches the
    // window, the rules inside it: the block is not flattened into the sheet,
    // so the cascade is where the query is asked.
    std::function<void(const std::vector<SheetRulePtr>&, int)> walk =
        [&](const std::vector<SheetRulePtr>& rules, int layer) {
            for (const auto& rule : rules) {
                if (!rule) continue;
                if (rule->is_media()) {
                    if (media_rule_matches(*rule, viewport_width, viewport_height)) walk(rule->children, layer);
                    continue;
                }
                // A `@keyframes` rule and its frames declare no selector: they
                // are the animation engine's and the CSSOM's, and matching an
                // element against them would be matching it against nothing.
                if (!rule->is_style_or_media()) continue;
                const long specificity = rule->selector.matching_specificity(element);
                if (specificity < 0) continue;
                for (const auto& declaration : rule->declarations) {
                    matched.push_back({declaration.important ? layer + 3 : layer, specificity, &declaration});
                }
            }
        };
    auto collect = [&](const ParsedSheet& sheet, int layer) { walk(sheet.rules, layer); };
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
        for (std::size_t i = 0; i < part.style_sheets.size(); ++i) {
            // `sheet.disabled = true` takes a sheet out of the cascade, and
            // leaves it in `document.styleSheets` and in `cssRules`.
            const Element* owner = i < part.style_elements.size() ? part.style_elements[i].get() : nullptr;
            if (owner) {
                auto found = part.sheet_objects.find(owner);
                if (found != part.sheet_objects.end() && found->second.object.is_object() &&
                    found->second.version != Node::tree_version()) {
                    found->second.applied = sheet_is_applied(*part.style_elements[i]);
                    found->second.version = Node::tree_version();
                }
                if (part.is_disabled(part.style_sheets[i].get())) continue;
            }
            collect(*part.style_sheets[i], 1);
        }
    }

    // What a script adopted (`document.adoptedStyleSheets`), after the
    // document's own sheets, which is the order CSSOM gives them: a later
    // adopted rule beats an earlier one at equal specificity, and both beat
    // the document's. The text is the sheet written back to CSS by the CSSOM
    // side (Phase 457), and it is parsed here exactly as a `<style>`'s is.
    // `@media` rules inside it keep their queries: `walk` reads the query of a
    // media rule and the rules it holds.
    if (root->is_document()) {
        if (const auto* found = dynamic_cast<const Document*>(root)) {
            for (const auto& adopted : found->adopted_sheets()) {
                if (!adopted || adopted->text.empty()) continue;
                StylePart& style_part = realm.part<StylePart>();
                auto& cached_sheet = style_part.adopted_parsed[adopted.get()];
                if (!cached_sheet || cached_sheet->generation != adopted->generation) {
                    cached_sheet = std::make_shared<ParsedSheet>();
                    std::size_t at = 0;
                    parse_rules(adopted->text, at, cached_sheet->rules, false);
                    cached_sheet->number_rules();
                    cached_sheet->generation = adopted->generation;
                }
                collect(*cached_sheet, 2);
            }
        }
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

// The value of a math function whose comparison needs the basis first:
// `min(50%, 30px)` at a 20px parent is `min(10px, 30px)`. Every percentage is
// replaced by its length before the expression is evaluated, which is what
// makes the comparison possible; -1 when the text is not such an expression.
double resolve_math_with_basis(const std::string& lower, double basis, double rem) {
    litebird::CalcNode resolved;
    if (!litebird::calc_parse(lower, resolved)) return -1.0;
    std::function<bool(litebird::CalcNode&)> substitute = [&](litebird::CalcNode& node) -> bool {
        if (node.kind == litebird::CalcNode::Dimension && node.unit == "%") {
            node.kind = litebird::CalcNode::Dimension;
            node.unit = "px";
            node.number = node.number / 100.0 * basis;
            return true;
        }
        for (auto& child : node.children) {
            if (!substitute(child)) return false;
        }
        return true;
    };
    if (!substitute(resolved)) return -1.0;
    litebird::CalcTriple triple;
    litebird::CalcContext ctx;
    ctx.rem = rem;
    if (!litebird::calc_eval(resolved, ctx, triple) || !triple.dimension) return -1.0;
    return triple.px;
}

// A font size that is said in terms of the parent's (or, for rem, the root's).
double relative_font_size(const std::string& lower, double parent, double root) {
    // A `calc()` here, and a `min()`/`max()`/`clamp()` too: `CalcParser` knows
    // only sums and products, so the three comparisons used to fall through to
    // "the parent's size" -- `font-size: max(50%, 30px)` read back as 20px
    // while the cascade gave the element 30px -- and the line box was 40px.
    // The typed evaluator in litebird/css/Calc.hpp is the one the cascade
    // uses, so the two cannot disagree again.
    static const char* const kMathFns[] = {"calc", "min", "max", "clamp"};
    for (const char* name : kMathFns) {
        if (!is_function_value(lower, name)) continue;
        litebird::CalcTriple triple;
        litebird::CalcContext ctx;
        ctx.em = parent;
        ctx.rem = root;
        if (litebird::calc_eval_text(lower, ctx, triple) && triple.dimension) {
            return triple.px + triple.percent / 100 * parent;
        }
        {
            // A length-percentage this evaluator can see but not make
            // absolute: `min(50%, 30px)` needs the two compared, and only a
            // basis compares them. The parent's size is that basis for a
            // font size, so the comparison happens here.
            const double result = resolve_math_with_basis(lower, parent, root);
            if (result >= 0.0) return result;
        }
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
        // A percentage in a `line-height` is of the element's **own** font
        // size (CSS 2.1 §10.8.1), which this has — so the value is a length,
        // not a calculation waiting for a containing block it never needed.
        // The cascade computes the same length; before this, the layout used
        // 14px for `line-height: calc(100% + 4px)` at 10px while
        // getComputedStyle went on quoting the declaration.
        if (name == "line-height" && triple.percent != 0) {
            const double length = triple.px + triple.percent / 100.0 * font_size_px(realm, subject);
            if (length >= 0.0) return px_text(length);
            return value;  // a negative line-height is not a length anybody can use
        }
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
    if (name == "line-height") {
        // `calc()`, `min()`, `max()` and `clamp()` in a `line-height` are
        // lengths too, and a percentage in one is of this element's own font
        // size -- the basis the three comparisons need. The branch above
        // reads a single number and cannot see a function; `resolved()`
        // leaves a percentage in a `calc()` as text on purpose, because for
        // every property but these two the basis is the containing block.
        // `line-height: max(100%, 20px)` at 10px laid out 20px while this
        // answered the declaration, and the DOM corpus cannot see the
        // difference because jsdom lays nothing out.
        static const char* const kMathFns[] = {"calc", "min", "max", "clamp"};
        for (const char* fn : kMathFns) {
            if (!is_function_value(text, fn)) continue;
            const double length = resolve_math_with_basis(text, font_size_px(realm, from), root_font_size(realm, from));
            if (length >= 0.0) return px_text(length);
            return value;  // an expression nothing here can evaluate keeps its text
        }
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

// The used content-box size, from the layout box the element already has.
//
// CSSOM's resolved value for `width` and `height` is *not* the computed value:
// if the property applies and `display` is neither `none` nor `contents`, the
// resolved value is the **used** value -- for these two, the content-box size
// in pixels (CSSOM "resolved values"). The engine answered the computed
// value, so `width: 100px` with 10px of padding and a 5px border read back
// `"100px"` where a browser says `"130px"`, and `width: auto` read back
// `"auto"` where a browser says the block's used width.
//
// The layout box is the border box, and the border and padding widths are kept
// beside it (Element::layout_border, Element::layout_padding).
//
// An element with no box is answered by its computed value, which is what the
// spec says for `display: none` and what a browser does for a `display:
// contents` element: every `getComputedStyle` value for one reads as it is
// declared. Non-replaced inline boxes keep the computed value too (CSS 2.1
// §10.3.1 leaves their width to the content), which is what a browser
// reports.
bool used_content_size(const Element& element, const Block& block, bool horizontal, float& out) {
    if (!element.is_connected()) return false;
    if (element.is_foreign()) return false;                 // no CSS box model in SVG
    // A used size needs a layout, and asking for one is what a browser does
    // here (the forced synchronous layout every style read costs). It is
    // version-guarded, so it is a comparison when nothing has changed.
    if (auto document = element.owner_document()) document->ensure_layout();
    if (!element.has_layout_box()) return false;
    // The cascade's own answer for `display`, not the inline declaration: a
    // `display: none` from a style sheet is as good as one from `style=`.
    const std::string display = ascii_lower(block.get("display", true));
    if (display == "none" || display == "contents") return false;
    // A non-replaced inline box takes its width from its content, so CSS 2.1
    // §10.3.1 leaves the declaration computable and browsers report it. An
    // inline *replaced* element (an `img`, an `inline-block`) is atomic and
    // has a used width of its own; the engine's own layout box says which is
    // which: a non-replaced inline element's box is assembled from fragments.
    if (display == "inline" && element.layout_box_from_fragments()) return false;
    const Rect border_box = element.bounding_box();
    // What the used `width` measures depends on `box-sizing`, and the answer is
    // the box `width` controls: the content box under `content-box` (the
    // default), the border box under `border-box`. So
    // `width: 100px; padding: 10px; border: 5px solid` reads `"100px"` either
    // way -- the box is 130 wide under `content-box` and 100 under
    // `border-box`, and in both the content box is what the declaration sized.
    if (ascii_lower(block.get("box-sizing", true)) == "border-box") {
        out = horizontal ? border_box.width : border_box.height;
        return out >= 0.0f;
    }
    const EdgeSizes border = element.layout_border();
    const EdgeSizes padding = element.layout_padding();
    out = horizontal ? border_box.width - border.left - border.right - padding.left - padding.right
                     : border_box.height - border.top - border.bottom - padding.top - padding.bottom;
    return out >= 0.0f;
}

// What getComputedStyle answers for `name`.
std::string computed_value(Realm& realm, const Element& element, const Block& block, const std::string& name,
                           int depth) {
    if (depth > 4) return std::string();
    // Only `width` and `height` are resolved to a used value here. The other
    // CSS 2.1 legacy properties CSSOM also names (`top`, `right`, `bottom`,
    // `left`, `margin-*`, `padding-*`) still answer their computed value; the
    // widths above are the ones a page reaches for first, and the ones the
    // DOM corpus could not ask.
    if (depth == 0 && (name == "width" || name == "height")) {
        const std::string computed = computed_longhand(realm, element, block, name, depth + 1);
        // A value that is still a `calc()` after the cascade is left as text,
        // and for two different reasons. One is that a percentage in it needs
        // a layout basis this code does not have -- the gap the computed value
        // has always had, and one that stays. The other is a `calc()` nested
        // past what the resolver reduces: the layout does not re-resolve such
        // a declaration, so its box is still the *previous* declaration's, and
        // reporting that box as this declaration's used value would be a lie.
        // Neither is a used value this code can stand behind, so both keep
        // the computed text.
        if (ascii_lower(computed).find("calc(") != std::string::npos) return computed;
        float used = 0.0f;
        if (used_content_size(element, block, name == "width", used)) return px_text(used);
        return computed;
    }
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

// Font-face descriptors are a different grammar from element properties.
// Keep them as individual entries, without expanding CSS shorthands.
bool font_face_descriptor(const std::string& name) {
    static const std::unordered_set<std::string> names = {
        "font-family", "src", "font-style", "font-weight", "font-stretch",
        "font-display", "unicode-range", "font-feature-settings", "font-variation-settings",
        "font-named-instance", "ascent-override", "descent-override", "line-gap-override",
        "size-adjust"};
    return names.count(name) != 0;
}

std::string font_face_property(const std::string& property) {
    std::string name;
    for (char c : property) {
        if (c >= 'A' && c <= 'Z') { name += '-'; name += static_cast<char>(c - 'A' + 'a'); }
        else name += c;
    }
    return font_face_descriptor(name) ? name : std::string();
}

// Normalize the common descriptor forms. The CSSOM's font-face grammar is
// independent of the renderer's font loader. Advanced CSS escapes and newer
// descriptor forms are not yet fully parsed here.
bool font_face_value(const std::string& name, const std::string& text, std::string& out) {
    std::string value = trim(text);
    if (value.empty()) return false;
    std::string lower = ascii_lower(value);
    if (name == "font-display") {
        if (lower != "auto" && lower != "block" && lower != "swap" && lower != "fallback" && lower != "optional") return false;
        out = lower; return true;
    }
    if (name == "font-family") {
        if (value.find(',') != std::string::npos || lower == "inherit" || lower == "initial" || lower == "unset") return false;
        if (value.size() >= 2 && (value.front() == '\'' || value.front() == '"') && value.back() == value.front()) {
            std::string unquoted = value.substr(1, value.size() - 2);
            // Identifiers made solely of simple words need no quotes.
            if (!unquoted.empty() && unquoted.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_ ") == std::string::npos)
                value = unquoted;
        }
        out = value; return true;
    }
    if (name == "src") {
        // Serialize local(), url() and their format()/tech() hints without
        // folding case inside their string arguments.
        std::string result;
        std::size_t i = 0;
        bool need_source = true;
        while (i < value.size()) {
            while (i < value.size() && is_ascii_space(value[i])) ++i;
            if (i == value.size()) break;
            if (value[i] == ',') {
                if (need_source) return false;
                result += ", "; ++i; need_source = true; continue;
            }
            std::size_t start = i;
            while (i < value.size() && ((value[i] >= 'a' && value[i] <= 'z') || (value[i] >= 'A' && value[i] <= 'Z') || value[i] == '-')) ++i;
            std::string fn = ascii_lower(value.substr(start, i - start));
            if (i == value.size() || value[i++] != '(') return false;
            bool source = fn == "url" || fn == "local";
            if ((need_source && !source) || (!need_source && fn != "format" && fn != "tech")) return false;
            std::size_t arg_start = i;
            char quote = 0;
            while (i < value.size()) {
                char c = value[i];
                if (quote) {
                    if (c == '\\' && i + 1 < value.size()) { i += 2; continue; }
                    if (c == quote) quote = 0;
                } else if (c == '\'' || c == '"') quote = c;
                else if (c == ')') break;
                ++i;
            }
            if (i == value.size() || quote) return false;
            std::string arg = trim(value.substr(arg_start, i - arg_start)); ++i;
            if (arg.empty()) return false;
            if (!need_source && !result.empty()) result += ' ';
            if (fn != "tech") {
                if (arg.size() >= 2 && (arg.front() == '\'' || arg.front() == '"') && arg.back() == arg.front()) arg = arg.substr(1, arg.size() - 2);
                std::string escaped;
                for (char c : arg) { if (c == '"') escaped += '\\'; escaped += c; }
                result += fn + "(\"" + escaped + "\")";
            } else result += fn + "(" + arg + ")";
            need_source = false;
        }
        if (need_source) return false;
        out = result; return true;
    }
    if (name == "font-weight") {
        if (lower == "normal" || lower == "bold") { out = lower; return true; }
        auto parts = split_components(value);
        if (parts.empty() || parts.size() > 2) return false;
        double previous = 0;
        for (const auto& part : parts) {
            std::size_t at = 0; double number = 0;
            if (!parse_css_number(part, at, number) || at != part.size() || number < 1 || number > 1000 || number < previous) return false;
            previous = number;
        }
        out = value; return true;
    }
    if (name == "font-style") {
        if (lower != "normal" && lower != "italic" && lower != "oblique" && lower.rfind("oblique ", 0) != 0) return false;
        out = lower; return true;
    }
    if (is_global_keyword(lower) || lower.find("var(") != std::string::npos) return false;
    out = value; return true;
}

std::string font_face_text(const Block& block) {
    std::string out;
    for (const Entry& entry : block.entries) {
        if (!out.empty()) out += ' ';
        out += entry.name + ": " + entry.value + (entry.important ? " !important;" : ";");
    }
    return out;
}

void font_face_put(Block& block, const std::string& name, const std::string& value, bool important) {
    if (Entry* old = block.find(name)) { old->value = value; old->important = important; }
    else block.entries.push_back({name, value, important, false});
}

struct StyleState : JsExotic {
    Realm* realm{nullptr};
    std::shared_ptr<Element> element;
    bool computed{false};
    /// A style rule's declaration block: the declarations live in the rule,
    /// which `block` is built from and written back to.
    std::shared_ptr<RuleState> rule;
    Block block;
    /// Inline: the attribute text `block` was parsed from.
    std::string source;
    bool has_source{false};
    /// Computed: the tree version `block` was computed at. A rule: the
    /// sheet's `generation` it was built at.
    uint64_t version{0};
    /// Computed: the style generation it was computed at.
    uint64_t style_version{0};

    bool font_face() const { return rule && rule->rule && rule->rule->is_font_face(); }

    std::string block_text() const { return font_face() ? font_face_text(block) : block.text(!computed); }

    void load() {
        if (rule) {
            std::vector<RawDeclaration>* declarations = rule_declarations(*rule);
            if (version == sheet_generation(rule->sheet) && has_source) return;
            block.clear();
            if (!declarations) return;
            for (const auto& declaration : *declarations) {
                if (font_face()) {
                    if (rule->rule->font_face_parsed) {
                        font_face_put(block, declaration.name, declaration.value, declaration.important);
                    } else {
                        std::string value;
                        if (declaration.important || !font_face_descriptor(declaration.name) ||
                            !font_face_value(declaration.name, declaration.value, value)) continue;
                        font_face_put(block, declaration.name, value, false);
                    }
                } else {
                    if (!is_css_property(declaration.name)) continue;
                    block.set(declaration.name, declaration.value, declaration.important, true);
                }
            }
            source = block_text();
            if (font_face() && !rule->rule->font_face_parsed) {
                *declarations = parse_declarations(source);
                rule->rule->font_face_parsed = true;
            }
            has_source = true;
            version = sheet_generation(rule->sheet);
            return;
        }
        if (computed) {
            // An element that is not connected has no computed style: every
            // property, the priority, the length and the cssText read as
            // empty, the way a browser answers it (HTML, window.getComputedStyle).
            if (realm->alive && !element->is_connected()) {
                block.clear();
                version = Node::tree_version();
                return;
            }
            if (version == Node::tree_version() && style_version == style_generation() && realm->alive) return;
            if (realm->alive) compute_block(*realm, *element, block);
            version = Node::tree_version();
            style_version = style_generation();
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
        if (rule) {
            source = block_text();
            if (font_face()) rule->rule->font_face_parsed = true;
            has_source = true;
            std::vector<RawDeclaration>* declarations = rule_declarations(*rule);
            if (!declarations) return;
            declarations->clear();
            for (const auto& declaration : parse_declarations(source)) {
                if (!font_face() && !is_css_property(declaration.name)) continue;
                declarations->push_back(declaration);
            }
            touch_sheet(rule->sheet);
            version = sheet_generation(rule->sheet);
            return;
        }
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
        if (font_face()) { const Entry* entry = block.find(name); return entry ? entry->value : std::string(); }
        if (!computed || !realm->alive) return block.get(name, true);
        return computed_value(*realm, *element, block, name, 0);
    }

    void set_value(const std::string& name, const std::string& text, bool important) {
        refuse_write();
        load();
        if (font_face()) {
            std::string before = block_text();
            if (trim(text).empty()) {
                block.entries.erase(std::remove_if(block.entries.begin(), block.entries.end(),
                    [&](const Entry& e) { return e.name == name; }), block.entries.end());
            } else {
                std::string value;
                if (font_face_descriptor(name)) {
                    if (!font_face_value(name, text, value)) return;
                } else if (!is_css_property(name) || !normalize_value(name, text, value, true)) return;
                font_face_put(block, name, value, important);
            }
            if (block_text() != before) store();
            return;
        }
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
        std::string property = css_name_for(name);
        if (property.empty() && font_face()) property = font_face_property(name);
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
        return !css_name_for(name).empty() || (font_face() && !font_face_property(name).empty());
    }
    JsExotic::SetResult set(JsObject*, const std::string& name, const JsValue& value) override {
        std::size_t index = 0;
        if (parse_index(name, index)) return JsExotic::SetResult::Refused;
        std::string property = css_name_for(name);
        if (property.empty() && font_face()) property = font_face_property(name);
        if (property.empty()) return JsExotic::SetResult::NotHandled;
        set_value(property, to_str_null_empty(value), false);
        return JsExotic::SetResult::Handled;
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
            return str(state.block_text());
        },
        [](Realm&, const JsValue& value) {
            StyleState& state = this_style();
            state.refuse_write();
            state.block.clear();
            if (state.font_face()) {
                for (const auto& declaration : parse_declarations(to_str_null_empty(value))) {
                    std::string normalized;
                    if (!declaration.important && font_face_descriptor(declaration.name) &&
                        font_face_value(declaration.name, declaration.value, normalized))
                        font_face_put(state.block, declaration.name, normalized, false);
                }
            } else apply_declarations(state.block, to_str_null_empty(value), !state.computed);
            state.store();
        });
    I.getter("length", [](Realm&) {
        StyleState& state = this_style();
        state.load();
        return num(static_cast<double>(state.block.listed_count()));
    });
    I.getter("parentRule", [](Realm& realm) {
        StyleState& state = this_style();
        if (state.font_face()) return make_rule(realm, state.rule->sheet, state.rule->parent, state.rule->rule);
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
        if (!is_css_property(name) && !(state.font_face() && font_face_descriptor(name))) return str("");
        return str(state.value_of(name));
    });
    I.method("getPropertyPriority", 1, [](Realm&, Args args) {
        require_args(args, 1, "CSSStyleDeclaration.getPropertyPriority");
        StyleState& state = this_style();
        state.load();
        const std::string name = property_argument(args[0]);
        if (state.font_face()) { const Entry* e = state.block.find(name); return str(e && e->important ? "important" : ""); }
        return str(state.block.is_important(name, !state.computed) ? "important" : "");
    });
    I.method("setProperty", 2, [](Realm&, Args args) {
        require_args(args, 2, "CSSStyleDeclaration.setProperty");
        StyleState& state = this_style();
        state.refuse_write();
        const std::string name = property_argument(args[0]);
        if (!is_css_property(name) && !(state.font_face() && font_face_descriptor(name))) return undefined();
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
        const std::string before = state.block_text();
        std::string old;
        if (state.font_face()) {
            if (const Entry* entry = state.block.find(name)) old = entry->value;
            state.block.entries.erase(std::remove_if(state.block.entries.begin(), state.block.entries.end(),
                [&](const Entry& e) { return e.name == name; }), state.block.entries.end());
        } else old = state.block.remove(name, !state.computed);
        if (state.block_text() != before) state.store();
        return str(old);
    });
}

// ---------------------------------------------------------------------------
// FontFaceSet
// ---------------------------------------------------------------------------
//
// `document.fonts` exists for one thing in practice: `ready`, a promise that
// is resolved once the fonts in use have loaded. Every face here is a face
// the engine found already, so there is never anything to wait for: `ready`
// is a promise that is already resolved.
//
// The faces themselves (`FontFace`, `@font-face`, `load()`) are not
// implemented: the set is empty, and asking it to load one adds it and
// leaves it `unloaded`.

struct FontFaceSetState : JsExotic {
    std::vector<JsValue> faces;
    JsValue ready;

    void trace(Heap& heap) override {
        heap.mark_value(ready);
        for (const auto& face : faces) heap.mark_value(face);
    }
};

void setup_font_face_set(Iface& I) {
    I.getter("status", [](Realm&) {
        this_state<FontFaceSetState>("FontFaceSet");
        return str("loaded");
    });
    I.getter("ready", [](Realm&) {
        // Resolved with the set itself, as the standard has it: what a page
        // uses the promise for is to know when the fonts are there, and the
        // promise then hands it the set.
        FontFaceSetState& state = this_state<FontFaceSetState>("FontFaceSet");
        if (!state.ready.is_object()) state.ready = Promise::make_resolved(JsValue::current_this());
        return state.ready;
    });
    I.getter("size", [](Realm&) {
        FontFaceSetState& state = this_state<FontFaceSetState>("FontFaceSet");
        return num(static_cast<double>(state.faces.size()));
    });
    I.method("add", 1, [](Realm&, Args args) {
        FontFaceSetState& state = this_state<FontFaceSetState>("FontFaceSet");
        require_args(args, 1, "FontFaceSet.add");
        state.faces.push_back(args[0]);
        return JsValue::current_this();
    });
    I.method("delete", 1, [](Realm&, Args args) {
        FontFaceSetState& state = this_state<FontFaceSetState>("FontFaceSet");
        require_args(args, 1, "FontFaceSet.delete");
        const auto before = state.faces.size();
        state.faces.erase(std::remove_if(state.faces.begin(), state.faces.end(),
                                         [&](const JsValue& face) { return strict_equals(face, args[0]); }),
                          state.faces.end());
        return boolean(state.faces.size() != before);
    });
    I.method("has", 1, [](Realm&, Args args) {
        FontFaceSetState& state = this_state<FontFaceSetState>("FontFaceSet");
        require_args(args, 1, "FontFaceSet.has");
        return boolean(std::any_of(state.faces.begin(), state.faces.end(),
                                   [&](const JsValue& face) { return strict_equals(face, args[0]); }));
    });
    I.method("clear", 0, [](Realm&, Args) {
        FontFaceSetState& state = this_state<FontFaceSetState>("FontFaceSet");
        state.faces.clear();
        return undefined();
    });
    I.method("forEach", 1, [](Realm& realm, Args args) {
        FontFaceSetState& state = this_state<FontFaceSetState>("FontFaceSet");
        require_args(args, 1, "FontFaceSet.forEach");
        const JsValue callback = args[0];
        if (!callback.is_function()) js_throw_type_error("FontFaceSet.forEach: the callback is not a function");
        const JsValue self = JsValue::current_this();
        for (const auto& face : state.faces) {
            call_reporting(realm, callback, self, {face, face, self});
        }
        return undefined();
    });
}

// ---------------------------------------------------------------------------
// The CSS Object Model: the interfaces over the rules above
// ---------------------------------------------------------------------------

// A style rule's own declaration block. The declarations live in the rule;
// `StyleState` writes back to them through `rule`.
JsValue make_rule_style(Realm& realm, std::shared_ptr<RuleState> rule) {
    auto state = std::make_shared<StyleState>();
    state->realm = &realm;
    state->rule = std::move(rule);
    return realm.make("CSSStyleDeclaration", state);
}

std::string serialize_selector_list(const SelectorList& list) {
    (void)list;
    return std::string();
}

// The block of a rule, as a browser writes it: through the CSSStyleDeclaration
// that `rule.style` is, so that a shorthand is one declaration and a length is
// a computed one — `margin: 0 auto` comes back as `margin: 0px auto;`.
std::string rule_block_text(Realm& realm, RuleState& state) {
    JsValue style = make_rule_style(realm, std::make_shared<RuleState>(state));
    StyleState* style_state = state_of<StyleState>(style);
    if (!style_state || !rule_declarations(state)) {
        const std::vector<RawDeclaration>* declarations = rule_declarations(state);
        return declarations ? serialize_declarations(*declarations) : std::string();
    }
    style_state->load();
    return style_state->block_text();
}

/// A rule as `cssText` writes it: a style rule with its block, and a `@media`
/// rule with its query and the rules inside it.
std::string rule_css_text(Realm& realm, RuleState& state) {
    if (!state.rule) return std::string();
    if (state.rule->is_font_face()) {
        const std::string block = rule_block_text(realm, state);
        return block.empty() ? "@font-face { }" : "@font-face { " + block + " }";
    }
    if (state.rule->is_keyframes()) {
        std::string inner;
        for (const auto& frame : state.rule->children) {
            if (!frame) continue;
            RuleState frame_state;
            frame_state.sheet = state.sheet;
            frame_state.rule = frame;
            frame_state.parent = state.rule;
            if (!inner.empty()) inner += ' ';
            inner += rule_css_text(realm, frame_state);
        }
        return "@keyframes " + state.rule->keyframes_name + " { " + inner + " }";
    }
    if (state.rule->is_keyframe()) {
        std::string block;
        if (rule_declarations(static_cast<const RuleState&>(state))) block = rule_block_text(realm, state);
        return state.rule->key_text + " { " + block + " }";
    }
    if (!state.rule->is_media()) {
        std::string block;
        if (rule_declarations(static_cast<const RuleState&>(state))) block = rule_block_text(realm, state);
        return rule_selector_text(state) + " { " + block + " }";
    }
    std::string inner;
    for (const auto& child : state.rule->children) {
        if (!child) continue;
        RuleState child_state;
        child_state.sheet = state.sheet;
        child_state.rule = child;
        child_state.parent = state.rule;
        if (!inner.empty()) inner += ' ';
        inner += rule_css_text(realm, child_state);
    }
    return "@media " + state.rule->media_text + " { " + inner + " }";
}

std::string serialize_declarations(const std::vector<RawDeclaration>& declarations) {
    std::string out;
    for (const auto& declaration : declarations) {
        if (!out.empty()) out += ' ';
        out += declaration.name + ": " + declaration.value + (declaration.important ? " !important;" : ";");
    }
    return out;
}

// `insertRule(text)`: "selector { declarations }" or "@media <query> { … }",
// and nothing after the closing brace but whitespace.
bool parse_inserted_rule(const std::string& text, SheetRulePtr& out) {
    const std::string trimmed = trim(text);
    // A rule ends with its block: "a {" is not one.
    if (trimmed.empty() || trimmed.back() != '}') return false;
    // The at-rule a sheet holds is `@media`; the rest are not rules it can
    // put a block in yet (a `@supports` block is flattened when the text is
    // read, and `insertRule` does not take that as one rule).
    if (trimmed.front() == '@') {
        const std::string lower = ascii_lower(trimmed);
        const bool keyframes = lower.compare(0, 10, "@keyframes") == 0 ||
                               lower.compare(0, 18, "@-webkit-keyframes") == 0;
        if (!keyframes && lower.compare(0, 6, "@media") != 0 &&
            lower.compare(0, 10, "@font-face") != 0) return false;
    }
    // One block, and nothing after it: "a { b { c {} } }" is not a style rule.
    int depth = 0;
    std::size_t closed_at = std::string::npos;
    char quote = 0;
    for (std::size_t i = 0; i < trimmed.size(); ++i) {
        const char c = trimmed[i];
        if (quote) {
            if (c == '\\' && i + 1 < trimmed.size()) { ++i; continue; }
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) { closed_at = i; break; }
    }
    if (closed_at == std::string::npos) return false;
    for (std::size_t i = closed_at + 1; i < trimmed.size(); ++i) {
        if (!is_ascii_space(trimmed[i])) return false;
    }
    std::size_t at = 0;
    std::vector<SheetRulePtr> rules;
    parse_rules(text, at, rules, false);
    while (at < text.size() && is_ascii_space(text[at])) ++at;
    if (rules.size() != 1 || at != text.size()) return false;
    const SheetRulePtr& rule = rules.front();
    if (!rule) return false;
    // A style rule needs a selector; an `@media` rule needs a query.
    if (rule->is_keyframes()) {
        // A `@keyframes` rule with no name declares nothing, and one with no
        // frames is still the rule a script asked for.
        if (rule->keyframes_name.empty()) return false;
    } else if (rule->is_media()) {
        if (rule->media_text.empty()) return false;
    } else if (!rule->is_font_face() && (rule->selector.empty() || rule->selector_text.empty())) {
        return false;
    }
    out = std::move(rules.front());
    return true;
}

void number_inserted_rules(ParsedSheet& sheet, std::vector<SheetRulePtr>& list) {
    for (auto& rule : list) {
        if (!rule) continue;
        if (rule->id == 0) {
            rule->id = sheet.next_rule_id++;
            rule->from_text = false;
        }
        number_inserted_rules(sheet, rule->children);
    }
}

void SheetState::set_applied(bool value) {
    applied = value;
    if (realm) realm->part<StylePart>().set_disabled(sheet.get(), !value);
}

JsValue make_sheet(Realm& realm, std::shared_ptr<ParsedSheet> sheet, std::shared_ptr<Element> owner,
                   bool own_sheet, bool applied, std::string href) {
    // `rule.parentStyleSheet`: the sheet object of that sheet — the one a
    // <style> element owns when there is one, and otherwise the one made for
    // it here, which is the same object every time it is asked for.
    auto& by_sheet = realm.part<RuleObjectsPart>().sheets;
    auto found = by_sheet.find(sheet.get());
    if (found != by_sheet.end() && found->second.sheet.lock() == sheet) return found->second.object;
    auto state = std::make_shared<SheetState>();
    state->realm = &realm;
    state->sheet = std::move(sheet);
    state->owner = std::move(owner);
    state->own_sheet = own_sheet;
    state->applied = applied;
    state->href = std::move(href);
    JsValue object = realm.make("CSSStyleSheet", state);
    by_sheet[state->sheet.get()] = RuleObjectEntry{state->sheet, object};
    return object;
}

JsValue make_rule_list(Realm& realm, std::shared_ptr<ParsedSheet> sheet, SheetRulePtr parent) {
    auto state = std::make_shared<RuleListState>();
    state->sheet = std::move(sheet);
    state->parent = std::move(parent);
    return realm.make("CSSRuleList", state);
}

JsValue make_rule(Realm& realm, std::shared_ptr<ParsedSheet> sheet, SheetRulePtr parent, const SheetRulePtr& rule) {
    RuleState state;
    state.sheet = sheet;
    state.rule = rule;
    state.parent = std::move(parent);
    // One object per rule: `cssRules[0] === cssRules.item(0)`. The entry is
    // keyed by the sheet and rule object, not the sheet-local numeric id:
    // different sheets (and replaceSync) can reuse an id while an old rule
    // and its style remain reachable.
    auto& objects = realm.part<RuleObjectsPart>().objects;
    const auto key = std::make_pair(sheet.get(), rule.get());
    auto found = objects.find(key);
    if (found != objects.end() && found->second.sheet.lock() == sheet) {
        const JsValue& cached = found->second.object;
        RuleState* cached_state = state_of<RuleState>(cached);
        if (cached_state && cached_state->rule == rule) return cached;
    }
    auto shared_state = std::make_shared<RuleState>(state);
    const char* interface_name = rule->is_media()            ? "CSSMediaRule"
                                 : rule->is_keyframes()      ? "CSSKeyframesRule"
                                 : rule->is_keyframe()       ? "CSSKeyframeRule"
                                 : rule->is_font_face()      ? "CSSFontFaceRule"
                                                             : "CSSStyleRule";
    JsValue object = realm.make(interface_name, shared_state);
    objects[key] = RuleObjectEntry{sheet, object};
    return object;
}

JsValue make_media_list(Realm& realm, std::shared_ptr<ParsedSheet> sheet, SheetRulePtr rule) {
    auto state = std::make_shared<MediaListState>();
    state->sheet = std::move(sheet);
    state->rule = std::move(rule);
    return realm.make("MediaList", state);
}

/// The rules of a list: the sheet's own, or the ones inside a `@media` rule —
/// still in the sheet, or, once the rule has left it, in the rule, which is
/// still the rule it was.
std::vector<SheetRulePtr>* rule_list_of(RuleListState& state) {
    if (state.parent) return &state.parent->children;
    if (!state.sheet) return nullptr;
    return &state.sheet->rules;
}

// `cssRules[0]`: the indices are the list's, and live.
bool RuleListState::get(JsObject*, const std::string& name, JsValue& out) {
    std::size_t index = 0;
    if (!parse_index(name, index)) return false;
    std::vector<SheetRulePtr>* rules = rule_list_of(*this);
    if (!rules || index >= rules->size() || !(*rules)[index]) return false;
    out = make_rule(current_realm(), sheet, parent, (*rules)[index]);
    return true;
}

bool RuleListState::has(JsObject*, const std::string& name) {
    std::size_t index = 0;
    if (!parse_index(name, index)) return false;
    std::vector<SheetRulePtr>* rules = rule_list_of(*this);
    return rules && index < rules->size();
}

void RuleListState::keys(JsObject*, std::vector<std::string>& out) {
    std::vector<SheetRulePtr>* rules = rule_list_of(*this);
    if (!rules) return;
    for (std::size_t i = 0; i < rules->size(); ++i) out.push_back(std::to_string(i));
}

// --- `document.adoptedStyleSheets` ------------------------------------------
//
// The sheets a script built and the document adopted (CSSOM §"Adopted
// style sheets"). The list belongs to the document, not to the realm: the
// cascade is built from the document, in `DocumentStyles::build_resolver`,
// which knows nothing of a realm. What is kept there is the sheet as *CSS
// text* and a generation, because the CSSOM's model of a sheet and the
// layout's are different types and text is the one form every CSSOM write can
// be turned back into.

/// The `StyleSheetList` object `document.adoptedStyleSheets` answers. It reads
/// the document's list live, so a sheet that left it is gone from a list object
/// a script held on to.
struct AdoptedSheetListState : JsExotic {
    std::weak_ptr<Document> document;

    bool get(JsObject*, const std::string& name, JsValue& out) override;
    bool has(JsObject*, const std::string& name) override;
    void keys(JsObject*, std::vector<std::string>& out) override;
};

/// The sheet record of one CSSOM sheet object, made when it is adopted.
/// `SheetState` holds it so `insertRule` and `replaceSync` can write the text
/// back as they change the rules, which is what makes an edit reach the layout.
std::unordered_map<const ParsedSheet*, std::shared_ptr<Document::AdoptedStyleSheet>> s_adopted_records;

/// A whole sheet written back to CSS, which is what the layout's cascade is
/// given. Each rule is serialized by the same writer `rule.cssText` uses, so
/// `cssRules` and the layout cannot disagree about what a rule says.
std::string sheet_css_text(Realm& realm, const std::shared_ptr<ParsedSheet>& sheet) {
    if (!sheet) return std::string();
    std::string out;
    for (const auto& rule : sheet->rules) {
        if (!rule) continue;
        RuleState state;
        state.sheet = sheet;
        state.rule = rule;
        std::string text = rule_css_text(realm, state);
        if (text.empty()) continue;
        out += text;
        out += '\n';
    }
    return out;
}

/// Called by every CSSOM write, through `touch_sheet`: an adopted sheet's
/// record is brought up to date, so the next layout pass reads the rules the
/// script just wrote. A sheet no document adopted has no record and costs
/// nothing.
void refresh_adopted_record(Realm& realm, const std::shared_ptr<ParsedSheet>& sheet) {
    if (!sheet) return;
    auto found = s_adopted_records.find(sheet.get());
    if (found == s_adopted_records.end() || !found->second) return;
    found->second->text = sheet_css_text(realm, sheet);
    ++found->second->generation;
}

/// The live sheet whose adoption record is `record`, so a list index can be
/// answered with the sheet object it is, not a copy of it.
std::unordered_map<const Document::AdoptedStyleSheet*, std::weak_ptr<ParsedSheet>> s_adopted_sheets;

bool AdoptedSheetListState::get(JsObject*, const std::string& name, JsValue& out) {
    std::size_t index = 0;
    if (!parse_index(name, index)) return false;
    auto owner = this->document.lock();
    if (!owner) return false;
    const auto& sheets = owner->adopted_sheets();
    if (index >= sheets.size() || !sheets[index]) return false;
    auto live = s_adopted_sheets.find(sheets[index].get());
    if (live == s_adopted_sheets.end()) return false;
    auto sheet = live->second.lock();
    if (!sheet) return false;
    out = make_sheet(current_realm(), sheet, nullptr, true, true, std::string());
    return true;
}

bool AdoptedSheetListState::has(JsObject*, const std::string& name) {
    std::size_t index = 0;
    if (!parse_index(name, index)) return false;
    auto owner = this->document.lock();
    return owner && index < owner->adopted_sheets().size();
}

void AdoptedSheetListState::keys(JsObject*, std::vector<std::string>& out) {
    auto owner = this->document.lock();
    if (!owner) return;
    for (std::size_t i = 0; i < owner->adopted_sheets().size(); ++i) out.push_back(std::to_string(i));
}

/// A media query list as the parts a browser counts: `media.length` is the
/// number of alternatives, and each is trimmed as it is stored.
std::vector<std::string> media_list_items(const SheetRulePtr& rule) {
    if (!rule) return {};
    return media_alternatives(rule->media_text);
}

bool MediaListState::get(JsObject*, const std::string& name, JsValue& out) {
    std::size_t index = 0;
    if (!parse_index(name, index)) return false;
    const std::vector<std::string> items = media_list_items(rule);
    if (index >= items.size()) return false;
    out = str(items[index]);
    return true;
}

bool MediaListState::has(JsObject*, const std::string& name) {
    std::size_t index = 0;
    if (!parse_index(name, index)) return false;
    return index < media_list_items(rule).size();
}

void MediaListState::keys(JsObject*, std::vector<std::string>& out) {
    const std::vector<std::string> items = media_list_items(rule);
    for (std::size_t i = 0; i < items.size(); ++i) out.push_back(std::to_string(i));
}

void setup_rule(Iface& I) {
    // CSSRule's type constants. They are on `CSSRule` and on every subclass,
    // as WebIDL inheritance puts them (CSSRule.STYLE_RULE and CSSStyleRule.STYLE_RULE).
    static const struct { const char* name; double value; } kTypes[] = {
        {"STYLE_RULE", 1},         {"CHARSET_RULE", 2},      {"IMPORT_RULE", 3},
        {"MEDIA_RULE", 4},         {"FONT_FACE_RULE", 5},    {"PAGE_RULE", 6},
        {"KEYFRAMES_RULE", 7},     {"KEYFRAME_RULE", 8},     {"MARGIN_RULE", 9},
        {"NAMESPACE_RULE", 10},    {"COUNTER_STYLE_RULE", 11}, {"SUPPORTS_RULE", 12},
        {"FONT_FEATURE_VALUES_RULE", 14},
    };
    for (const auto& type : kTypes) I.constant(type.name, type.value);
    I.getter("type", [](Realm&) {
        this_state<RuleState>("CSSRule");
        return num(0);
    });
    I.getter("cssText", [](Realm& realm) {
        RuleState& state = this_state<RuleState>("CSSRule");
        return str(rule_css_text(realm, state));
    });
    I.getter("parentRule", [](Realm& realm) {
        RuleState& state = this_state<RuleState>("CSSRule");
        if (!state.parent) return null();
        // The rule it is inside. A `@media` rule inside a `@media` rule is not
        // what this engine writes, so the parent of a parent is not asked for.
        return make_rule(realm, state.sheet, nullptr, state.parent);
    });
    I.getter("parentStyleSheet", [](Realm& realm) {
        RuleState& state = this_state<RuleState>("CSSRule");
        if (!state.sheet) return null();
        return make_sheet(realm, state.sheet, nullptr, false, false, std::string());
    });
}

void setup_style_rule(Iface& I) {
    I.getter("type", [](Realm&) {
        this_state<RuleState>("CSSStyleRule");
        return num(1); // CSSRule.STYLE_RULE
    });
    I.accessor("selectorText",
        [](Realm&) {
            RuleState& state = this_state<RuleState>("CSSStyleRule");
            return str(rule_selector_text(state));
        },
        [](Realm& realm, const JsValue& value) {
            RuleState& state = this_state<RuleState>("CSSStyleRule");
            if (!state.rule) return;
            const std::string text = trim(to_str(value));
            SelectorList parsed;
            if (!SelectorList::parse(text, parsed)) {
                throw_dom(realm, "SyntaxError",
                          "Failed to set the 'selectorText' property on 'CSSStyleRule': '" + text +
                              "' is not a valid selector list.");
            }
            state.rule->selector = std::move(parsed);
            state.rule->selector_text = text;
            touch_sheet(state.sheet);
        });
    I.getter("style", [](Realm& realm) {
        RuleState& state = this_state<RuleState>("CSSStyleRule");
        return make_rule_style(realm, std::make_shared<RuleState>(state));
    });
    I.getter("cssText", [](Realm& realm) {
        RuleState& state = this_state<RuleState>("CSSStyleRule");
        const std::string block = rule_block_text(realm, state);
        return str(rule_selector_text(state) + " { " + block + " }");
    });
}

void setup_font_face_rule(Iface& I) {
    I.getter("type", [](Realm&) {
        this_state<RuleState>("CSSFontFaceRule");
        return num(5);
    });
    I.getter("style", [](Realm& realm) {
        RuleState& state = this_state<RuleState>("CSSFontFaceRule");
        return cached(JsValue::current_this(), "fontFaceStyle", [&]() {
            return make_rule_style(realm, std::make_shared<RuleState>(state));
        });
    });
}

void setup_rule_list(Iface& I) {
    I.getter("length", [](Realm&) {
        RuleListState& state = this_state<RuleListState>("CSSRuleList");
        std::vector<SheetRulePtr>* rules = rule_list_of(state);
        return num(rules ? static_cast<double>(rules->size()) : 0.0);
    });
    I.method("item", 1, [](Realm& realm, Args args) {
        require_args(args, 1, "CSSRuleList.item");
        RuleListState& state = this_state<RuleListState>("CSSRuleList");
        std::vector<SheetRulePtr>* rules = rule_list_of(state);
        if (!rules) return null();
        const std::size_t index = static_cast<std::size_t>(to_ulong(args[0]));
        if (index >= rules->size() || !(*rules)[index]) return null();
        return make_rule(realm, state.sheet, state.parent, (*rules)[index]);
    });
}

/// `StyleSheetList`, the interface `document.adoptedStyleSheets` answers:
/// `length` and `item(i)`, with the indices answering the same sheet objects.
void setup_style_sheet_list(Iface& I) {
    I.getter("length", [](Realm&) {
        AdoptedSheetListState& state = this_state<AdoptedSheetListState>("StyleSheetList");
        auto owner = state.document.lock();
        return num(owner ? static_cast<double>(owner->adopted_sheets().size()) : 0.0);
    });
    I.method("item", 1, [](Realm& realm, Args args) {
        require_args(args, 1, "StyleSheetList.item");
        AdoptedSheetListState& state = this_state<AdoptedSheetListState>("StyleSheetList");
        auto owner = state.document.lock();
        if (!owner) return null();
        const std::size_t index = static_cast<std::size_t>(to_ulong(args[0]));
        const auto& sheets = owner->adopted_sheets();
        if (index >= sheets.size() || !sheets[index]) return null();
        auto live = s_adopted_sheets.find(sheets[index].get());
        if (live == s_adopted_sheets.end()) return null();
        auto sheet = live->second.lock();
        if (!sheet) return null();
        return make_sheet(realm, sheet, nullptr, true, true, std::string());
    });
}

// A `@media` rule: `type` 4, its query as a `MediaList`, and the rules of its
// block as a `CSSRuleList` a script can add to and take from.
void setup_media_rule(Iface& I) {
    I.getter("type", [](Realm&) {
        this_state<RuleState>("CSSMediaRule");
        return num(4); // CSSRule.MEDIA_RULE
    });
    I.getter("media", [](Realm& realm) {
        RuleState& state = this_state<RuleState>("CSSMediaRule");
        return make_media_list(realm, state.sheet, state.rule);
    });
    I.getter("cssRules", [](Realm& realm) {
        RuleState& state = this_state<RuleState>("CSSMediaRule");
        return make_rule_list(realm, state.sheet, state.rule);
    });
    I.alias("rules", "cssRules");
    I.method("insertRule", 1, [](Realm& realm, Args args) {
        require_args(args, 1, "CSSMediaRule.insertRule");
        RuleState& state = this_state<RuleState>("CSSMediaRule");
        if (!state.rule) return num(0);
        const std::string text = trim(to_str(args[0]));
        std::vector<SheetRulePtr>& rules = state.rule->children;
        std::size_t index = 0;
        if (args.size() > 1 && !args[1].is_undefined()) index = to_ulong(args[1]);
        if (index > rules.size()) {
            throw_dom(realm, "IndexSizeError",
                      "Failed to execute 'insertRule' on 'CSSMediaRule': the index (" + std::to_string(index) +
                          ") is larger than the number of rules (" + std::to_string(rules.size()) + ").");
        }
        SheetRulePtr rule;
        if (!parse_inserted_rule(text, rule)) {
            throw_dom(realm, "SyntaxError",
                      "Failed to execute 'insertRule' on 'CSSMediaRule': the rule '" + text +
                          "' is not a valid style rule.");
        }
        if (state.sheet) {
            std::vector<SheetRulePtr> one{rule};
            number_inserted_rules(*state.sheet, one);
        }
        rules.insert(rules.begin() + static_cast<std::ptrdiff_t>(index), rule);
        touch_sheet(state.sheet);
        return num(static_cast<double>(index));
    });
    I.method("deleteRule", 1, [](Realm& realm, Args args) {
        require_args(args, 1, "CSSMediaRule.deleteRule");
        RuleState& state = this_state<RuleState>("CSSMediaRule");
        if (!state.rule) return undefined();
        std::vector<SheetRulePtr>& rules = state.rule->children;
        const std::size_t index = static_cast<std::size_t>(to_ulong(args[0]));
        if (index >= rules.size()) {
            throw_dom(realm, "IndexSizeError",
                      "Failed to execute 'deleteRule' on 'CSSMediaRule': the index (" + std::to_string(index) +
                          ") is larger than the number of rules (" + std::to_string(rules.size()) + ").");
        }
        rules.erase(rules.begin() + static_cast<std::ptrdiff_t>(index));
        touch_sheet(state.sheet);
        return undefined();
    });
    I.getter("cssText", [](Realm& realm) {
        RuleState& state = this_state<RuleState>("CSSMediaRule");
        return str(rule_css_text(realm, state));
    });
}

// `CSSKeyframesRule`: the name a `@keyframes` block declares and the frames
// inside it. The frames are a `CSSRuleList` like any other block's rules, so
// `cssRules[0] === cssRules.item(0)` and `insertRule`/`deleteRule` work on it;
// `appendRule`, `findRule` and `deleteRule` address a frame by its selector,
// as the API has them.
void setup_keyframes_rule(Iface& I) {
    I.getter("type", [](Realm&) {
        this_state<RuleState>("CSSKeyframesRule");
        return num(7); // CSSRule.KEYFRAMES_RULE
    });
    I.accessor("name",
        [](Realm&) {
            RuleState& state = this_state<RuleState>("CSSKeyframesRule");
            if (!state.rule) return str("");
            return str(state.rule->keyframes_name);
        },
        [](Realm& realm, const JsValue& value) {
            RuleState& state = this_state<RuleState>("CSSKeyframesRule");
            if (!state.rule) return;
            state.rule->keyframes_name = trim(to_str(value));
            touch_sheet(state.sheet);
            (void)realm;
        });
    I.getter("cssRules", [](Realm& realm) {
        RuleState& state = this_state<RuleState>("CSSKeyframesRule");
        return make_rule_list(realm, state.sheet, state.rule);
    });
    I.alias("rules", "cssRules");
    I.method("appendRule", 1, [](Realm& realm, Args args) {
        require_args(args, 1, "CSSKeyframesRule.appendRule");
        RuleState& state = this_state<RuleState>("CSSKeyframesRule");
        if (!state.rule) return undefined();
        const std::string text = trim(to_str(args[0]));
        std::vector<SheetRulePtr> frames;
        std::size_t at = 0;
        parse_keyframe_blocks(text, at, frames);
        while (at < text.size() && is_ascii_space(text[at])) ++at;
        if (frames.size() != 1 || at != text.size()) {
            throw_dom(realm, "SyntaxError",
                      "Failed to execute 'appendRule' on 'CSSKeyframesRule': the rule '" + text +
                          "' is not a valid keyframe rule.");
        }
        if (state.sheet) {
            std::vector<SheetRulePtr> one{frames.front()};
            number_inserted_rules(*state.sheet, one);
        }
        state.rule->children.push_back(std::move(frames.front()));
        touch_sheet(state.sheet);
        return undefined();
    });
    I.method("deleteRule", 1, [](Realm&, Args args) {
        require_args(args, 1, "CSSKeyframesRule.deleteRule");
        RuleState& state = this_state<RuleState>("CSSKeyframesRule");
        if (!state.rule) return undefined();
        const std::string select = trim(to_str(args[0]));
        auto& frames = state.rule->children;
        for (auto it = frames.begin(); it != frames.end(); ++it) {
            if (*it && (*it)->key_text == select) {
                frames.erase(it);
                touch_sheet(state.sheet);
                return undefined();
            }
        }
        return undefined();       // No frame by that name: the call does nothing.
    });
    I.method("findRule", 1, [](Realm& realm, Args args) {
        require_args(args, 1, "CSSKeyframesRule.findRule");
        RuleState& state = this_state<RuleState>("CSSKeyframesRule");
        if (!state.rule) return null();
        const std::string select = trim(to_str(args[0]));
        for (const auto& frame : state.rule->children) {
            if (frame && frame->key_text == select) return make_rule(realm, state.sheet, state.rule, frame);
        }
        return null();
    });
    I.getter("cssText", [](Realm& realm) {
        RuleState& state = this_state<RuleState>("CSSKeyframesRule");
        return str(rule_css_text(realm, state));
    });
}

// `CSSKeyframeRule`: one frame of a `@keyframes` block, `0% { … }`, with the
// `style` a rule of any kind has.
void setup_keyframe_rule(Iface& I) {
    I.getter("type", [](Realm&) {
        this_state<RuleState>("CSSKeyframeRule");
        return num(8); // CSSRule.KEYFRAME_RULE
    });
    I.accessor("keyText",
        [](Realm&) {
            RuleState& state = this_state<RuleState>("CSSKeyframeRule");
            if (!state.rule) return str("");
            return str(state.rule->key_text);
        },
        [](Realm&, const JsValue& value) {
            RuleState& state = this_state<RuleState>("CSSKeyframeRule");
            if (!state.rule) return;
            state.rule->key_text = trim(to_str(value));
            touch_sheet(state.sheet);
        });
    I.getter("style", [](Realm& realm) {
        RuleState& state = this_state<RuleState>("CSSKeyframeRule");
        return make_rule_style(realm, std::make_shared<RuleState>(state));
    });
    I.getter("cssText", [](Realm& realm) {
        RuleState& state = this_state<RuleState>("CSSKeyframeRule");
        return str(rule_css_text(realm, state));
    });
}

// `MediaList`: the query a `@media` rule holds, as `mediaText` and as the
// alternatives `length` and `item(i)` count.
void setup_media_list(Iface& I) {
    I.accessor("mediaText",
        [](Realm&) {
            MediaListState& state = this_state<MediaListState>("MediaList");
            if (!state.rule) return str("");
            return str(state.rule->media_text);
        },
        [](Realm&, const JsValue& value) {
            MediaListState& state = this_state<MediaListState>("MediaList");
            if (!state.rule) return;
            state.rule->media_text = trim(to_str(value));
            state.rule->forget_media_answer();
            touch_sheet(state.sheet);
        });
    I.getter("length", [](Realm&) {
        MediaListState& state = this_state<MediaListState>("MediaList");
        return num(static_cast<double>(media_list_items(state.rule).size()));
    });
    I.method("item", 1, [](Realm&, Args args) {
        require_args(args, 1, "MediaList.item");
        MediaListState& state = this_state<MediaListState>("MediaList");
        const std::vector<std::string> items = media_list_items(state.rule);
        const std::size_t index = static_cast<std::size_t>(to_ulong(args[0]));
        if (index >= items.size()) return null();
        return str(items[index]);
    });
    I.method("appendMedium", 1, [](Realm& realm, Args args) {
        require_args(args, 1, "MediaList.appendMedium");
        MediaListState& state = this_state<MediaListState>("MediaList");
        if (!state.rule) return undefined();
        const std::string medium = trim(to_str(args[0]));
        if (medium.empty()) return undefined();
        std::vector<std::string> items = media_list_items(state.rule);
        const std::string lower = ascii_lower(medium);
        for (const auto& item : items) {
            if (ascii_lower(item) == lower) return undefined();
        }
        if (items.empty()) {
            state.rule->media_text = medium;
        } else {
            state.rule->media_text = state.rule->media_text + ", " + medium;
        }
        state.rule->forget_media_answer();
        touch_sheet(state.sheet);
        (void)realm;
        return undefined();
    });
    I.method("deleteMedium", 1, [](Realm& realm, Args args) {
        require_args(args, 1, "MediaList.deleteMedium");
        MediaListState& state = this_state<MediaListState>("MediaList");
        if (!state.rule) return undefined();
        const std::string medium = trim(to_str(args[0]));
        std::vector<std::string> items = media_list_items(state.rule);
        const std::string lower = ascii_lower(medium);
        std::vector<std::string> kept;
        bool found = false;
        for (const auto& item : items) {
            if (ascii_lower(item) == lower) {
                found = true;
                continue;
            }
            kept.push_back(item);
        }
        if (!found) {
            throw_dom(realm, "NotFoundError",
                      "Failed to execute 'deleteMedium' on 'MediaList': '" + medium +
                          "' is not in the list of media queries.");
        }
        std::string text;
        for (std::size_t i = 0; i < kept.size(); ++i) {
            if (i) text += ", ";
            text += kept[i];
        }
        state.rule->media_text = text;
        state.rule->forget_media_answer();
        touch_sheet(state.sheet);
        return undefined();
    });
}

void setup_style_sheet(Iface& I) {
    I.getter("cssRules", [](Realm& realm) {
        SheetState& state = this_state<SheetState>("CSSStyleSheet");
        return make_rule_list(realm, state.sheet);
    });
    I.alias("rules", "cssRules");
    I.getter("ownerNode", [](Realm& realm) {
        SheetState& state = this_state<SheetState>("CSSStyleSheet");
        if (!state.owner) return null();
        return wrap(realm, state.owner->shared_from_this());
    });
    I.getter("ownerRule", [](Realm&) {
        this_state<SheetState>("CSSStyleSheet");
        return null();
    });
    I.getter("href", [](Realm&) {
        SheetState& state = this_state<SheetState>("CSSStyleSheet");
        return state.href.empty() ? null() : str(state.href);
    });
    I.getter("type", [](Realm&) {
        this_state<SheetState>("CSSStyleSheet");
        return str("text/css");
    });
    // A stylesheet has no `disabled` attribute to reflect: the accessor's job
    // is to give back what was written, so the value is kept.
    I.accessor("disabled",
        [](Realm&) {
            SheetState& state = this_state<SheetState>("CSSStyleSheet");
            return boolean(state.disabled);
        },
        [](Realm&, const JsValue& value) {
            SheetState& state = this_state<SheetState>("CSSStyleSheet");
            state.disabled = value.is_truthy();
            state.set_applied(!state.disabled);
            touch_sheet(state.sheet);
        });
    I.getter("title", [](Realm&) {
        this_state<SheetState>("CSSStyleSheet");
        return str("");
    });
    I.method("insertRule", 1, [](Realm& realm, Args args) {
        require_args(args, 1, "CSSStyleSheet.insertRule");
        SheetState& state = this_state<SheetState>("CSSStyleSheet");
        const std::string text = trim(to_str(args[0]));
        std::size_t index = state.sheet->rules.size();
        if (args.size() > 1 && !args[1].is_undefined()) index = to_ulong(args[1]);
        if (index > state.sheet->rules.size()) {
            throw_dom(realm, "IndexSizeError",
                      "Failed to execute 'insertRule' on 'CSSStyleSheet': the index (" + std::to_string(index) +
                          ") is larger than the number of rules (" +
                          std::to_string(state.sheet->rules.size()) + ").");
        }
        SheetRulePtr rule;
        if (!parse_inserted_rule(text, rule)) {
            throw_dom(realm, "SyntaxError",
                      "Failed to execute 'insertRule' on 'CSSStyleSheet': the rule '" + text +
                          "' is not a valid style rule.");
        }
        std::vector<SheetRulePtr> one{rule};
        number_inserted_rules(*state.sheet, one);
        state.sheet->rules.insert(state.sheet->rules.begin() + static_cast<std::ptrdiff_t>(index), std::move(rule));
        touch_sheet(state.sheet);
        return num(static_cast<double>(index));
    });
    // `replace`/`replaceSync`: the sheet's text becomes exactly this. CSSOM
    // has `replace` wait for any `@import` in it; nothing here loads a
    // stylesheet, so the promise is already settled when it is made.
    I.method("replaceSync", 1, [](Realm& realm, Args args) {
        require_args(args, 1, "CSSStyleSheet.replaceSync");
        SheetState& state = this_state<SheetState>("CSSStyleSheet");
        auto parsed = std::make_shared<ParsedSheet>();
        std::size_t at = 0;
        parse_rules(to_str(args[0]), at, parsed->rules, false);
        parsed->number_rules();
        state.sheet->rules = std::move(parsed->rules);
        state.sheet->next_rule_id = parsed->next_rule_id;
        touch_sheet(state.sheet);
        return JsValue::make_undefined();
    });
    I.method("deleteRule", 1, [](Realm& realm, Args args) {
        require_args(args, 1, "CSSStyleSheet.deleteRule");
        SheetState& state = this_state<SheetState>("CSSStyleSheet");
        const std::size_t index = static_cast<std::size_t>(to_ulong(args[0]));
        if (index >= state.sheet->rules.size()) {
            throw_dom(realm, "IndexSizeError",
                      "Failed to execute 'deleteRule' on 'CSSStyleSheet': the index (" + std::to_string(index) +
                          ") is larger than the number of rules (" +
                          std::to_string(state.sheet->rules.size()) + ").");
        }
        // The object of the rule, if a script has one, holds the rule itself:
        // a removed rule is the rule it was, out of the tree's reach.
        state.sheet->rules.erase(state.sheet->rules.begin() + static_cast<std::ptrdiff_t>(index));
        touch_sheet(state.sheet);
        return undefined();
    });
    I.alias("removeRule", "deleteRule");
}

JsValue make_style(Realm& realm, Element& element, bool computed) {
    auto state = std::make_shared<StyleState>();
    state->realm = &realm;
    state->element = shared(element);
    state->computed = computed;
    return realm.make("CSSStyleDeclaration", state);
}

} // namespace

JsValue style_sheet(Realm& realm, Element& element) {
    // `sheet` is null while the element is not in a document. The sheet object
    // itself is kept: an element put back gets the same one, and a script that
    // held it goes on reading it.
    if (!element.is_connected()) return null();
    return sheet_object(realm, element);
}

/// `document.adoptedStyleSheets`: a live `StyleSheetList` over the document's
/// adopted records. Handed out once per document, as a browser does, so
/// `document.adoptedStyleSheets === document.adoptedStyleSheets`.
JsValue document_adopted_style_sheets(Realm& realm, Document& document) {
    auto state = std::make_shared<AdoptedSheetListState>();
    state->document = std::const_pointer_cast<Document>(
        std::static_pointer_cast<const Document>(document.shared_from_this()));
    return realm.make("StyleSheetList", state);
}

/// `document.adoptedStyleSheets = [...]`, CSSOM's "set an adopted style sheet
/// list". Every value must be a `CSSStyleSheet` the script constructed: an
/// element-owned sheet belongs to its element, and a browser refuses one with
/// `NotAllowedError` rather than taking it away from the element. The list is
/// copied in order, and a sheet that was already adopted keeps the record it
/// has, so the text written back by a later `insertRule` still lands in the
/// right place.
void set_document_adopted_style_sheets(Realm& realm, Document& document, const JsValue& value) {
    std::vector<std::shared_ptr<Document::AdoptedStyleSheet>> next;
    if (!value.is_nullish()) {
        if (!value.is_object_like()) {
            throw_dom(realm, "TypeError", "The provided value is not of type 'CSSStyleSheet[]'.");
        }
        const JsValue length_value = value.get_property("length");
        const double length = length_value.to_number();
        for (double i = 0; i < length; i += 1.0) {
            const JsValue item = value.get_property(std::to_string(static_cast<long long>(i)));
            auto* state = state_of<SheetState>(item);
            if (!state) {
                throw_dom(realm, "TypeError",
                          "Failed to set the 'adoptedStyleSheets' property: the value is not of type "
                          "'CSSStyleSheet'.");
            }
            if (!state->own_sheet || state->owner) {
                throw_dom(realm, "NotAllowedError",
                          "Failed to set the 'adoptedStyleSheets' property: a sheet owned by an "
                          "element cannot be adopted.");
            }
            auto& record = s_adopted_records[state->sheet.get()];
            if (!record) {
                record = std::make_shared<Document::AdoptedStyleSheet>();
                // Written now, so a list adopted without the text having ever
                // been read still carries the rules it has.
                record->text = sheet_css_text(realm, state->sheet);
            }
            s_adopted_sheets[record.get()] = state->sheet;
            next.push_back(record);
        }
    }
    document.set_adopted_sheets(std::move(next));
    // The cascade reads the list, so a change to it is a style change: the
    // next `getComputedStyle` and the next layout both see it.
    ++s_style_generation;
}

JsValue document_style_sheets(Realm& realm, Document& document) {
    std::vector<JsValue> out;
    for_each_descendant(document, [&](Node& node) {
        if (!node.is_element()) return true;
        Element& element = static_cast<Element&>(node);
        if (!is_html_element(element, "style")) return true;
        if (ascii_lower(element.get_attribute("media")).find("print") != std::string::npos) return true;
        out.push_back(sheet_object(realm, element));
        return true;
    });
    return JsValue::make_array(out);
}

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

/// `new CSSStyleSheet()`: a sheet no element owns, which a script builds up
/// with `insertRule`. The cascade does not read it (there is no element to read
/// it from), and `replace`/`replaceSync` give it text.
JsValue construct_style_sheet(Realm& realm, const InterfaceDef&, Args args, const JsValue* derived) {
    auto state = std::make_shared<SheetState>();
    state->realm = &realm;
    state->sheet = std::make_shared<ParsedSheet>();
    state->own_sheet = true;
    state->applied = false;
    // The argument is CSSOM's `options` dictionary (`baseURL`, and in newer
    // drafts `media`/`disabled`), and an object is the only thing accepted.
    if (!args.empty() && !args[0].is_undefined() && !args[0].is_object_like()) {
        js_throw_type_error("Failed to construct 'CSSStyleSheet': The provided value is not of type 'CSSStyleSheetInit'.");
    }
    JsValue object = realm.make("CSSStyleSheet", state, derived);
    realm.part<RuleObjectsPart>().sheets[state->sheet.get()] = RuleObjectEntry{state->sheet, object};
    return object;
}

void register_style_interfaces() {
    register_interface({"CSSStyleDeclaration", nullptr, setup_style_declaration, nullptr, 0, nullptr});
    register_interface({"CSSRule", nullptr, setup_rule, nullptr, 0, nullptr});
    register_interface({"CSSStyleRule", "CSSRule", setup_style_rule, nullptr, 0, nullptr});
    register_interface({"CSSFontFaceRule", "CSSRule", setup_font_face_rule, nullptr, 0, nullptr});
    register_interface({"CSSMediaRule", "CSSRule", setup_media_rule, nullptr, 0, nullptr});
    register_interface({"CSSKeyframesRule", "CSSRule", setup_keyframes_rule, nullptr, 0, nullptr});
    register_interface({"CSSKeyframeRule", "CSSRule", setup_keyframe_rule, nullptr, 0, nullptr});
    register_interface({"CSSRuleList", nullptr, setup_rule_list, nullptr, 0, nullptr});
    register_interface({"MediaList", nullptr, setup_media_list, nullptr, 0, nullptr});
    register_interface({"CSSStyleSheet", nullptr, setup_style_sheet, construct_style_sheet, 0, nullptr});
    register_interface({"StyleSheetList", nullptr, setup_style_sheet_list, nullptr, 0, nullptr});
    register_interface({"FontFaceSet", nullptr, setup_font_face_set, nullptr, 0, nullptr});
}

JsValue document_fonts(Realm& realm) {
    auto state = std::make_shared<FontFaceSetState>();
    return realm.make("FontFaceSet", state);
}

} // namespace dom
} // namespace litebird
