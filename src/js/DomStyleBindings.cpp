// CSSStyleDeclaration: `element.style` and what getComputedStyle returns.
//
// `element.style` is a view of the style attribute. It reads the attribute
// again whenever the attribute has changed, and every write goes back into
// the attribute as the whole list, so the two can never disagree. A value is
// checked against what its property accepts before it is stored: assigning
// "banana" to `width` changes nothing, as in a browser.
//
// The computed style has its own small cascade. It is not the layout engine's
// (css/StyleResolver.hpp), which resolves into the typed values layout needs;
// a script wants the declared text back. It reads the <style> elements of the
// document, matches their selectors with the script selector engine, and
// applies the style attribute on top.

#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/Selector.hpp"
#include "litebird/dom/TreeWalk.hpp"

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
        "align-content", "align-items", "align-self", "all", "animation", "animation-delay", "animation-direction",
        "animation-duration", "animation-fill-mode", "animation-iteration-count", "animation-name",
        "animation-play-state", "animation-timing-function", "appearance", "aspect-ratio", "backdrop-filter",
        "backface-visibility", "background", "background-attachment", "background-blend-mode", "background-clip",
        "background-color", "background-image", "background-origin", "background-position", "background-repeat",
        "background-size", "block-size", "border", "border-bottom", "border-bottom-color",
        "border-bottom-left-radius", "border-bottom-right-radius", "border-bottom-style", "border-bottom-width",
        "border-collapse", "border-color", "border-image", "border-left", "border-left-color", "border-left-style",
        "border-left-width", "border-radius", "border-right", "border-right-color", "border-right-style",
        "border-right-width", "border-spacing", "border-style", "border-top", "border-top-color",
        "border-top-left-radius", "border-top-right-radius", "border-top-style", "border-top-width", "border-width",
        "bottom", "box-shadow", "box-sizing", "break-after", "break-before", "break-inside", "caption-side",
        "caret-color", "clear", "clip", "clip-path", "color", "color-scheme", "column-count", "column-fill",
        "column-gap", "column-rule", "column-rule-color", "column-rule-style", "column-rule-width", "column-span",
        "column-width", "columns", "contain", "content", "counter-increment", "counter-reset", "cursor", "direction",
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
        "-moz-appearance", "-moz-user-select", "-ms-transform",
    };
    return s_names;
}

bool is_custom_property(const std::string& name) {
    return name.size() > 2 && name[0] == '-' && name[1] == '-';
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
    out = lower.substr(0, at) + unit;
    if (!out.empty() && out[0] == '+') out.erase(0, 1);
    return true;
}

const std::unordered_set<std::string>& named_colors() {
    static const std::unordered_set<std::string> s_names = {
        "aliceblue", "antiquewhite", "aqua", "aquamarine", "azure", "beige", "bisque", "black", "blanchedalmond",
        "blue", "blueviolet", "brown", "burlywood", "cadetblue", "chartreuse", "chocolate", "coral",
        "cornflowerblue", "cornsilk", "crimson", "cyan", "darkblue", "darkcyan", "darkgoldenrod", "darkgray",
        "darkgreen", "darkgrey", "darkkhaki", "darkmagenta", "darkolivegreen", "darkorange", "darkorchid",
        "darkred", "darksalmon", "darkseagreen", "darkslateblue", "darkslategray", "darkslategrey",
        "darkturquoise", "darkviolet", "deeppink", "deepskyblue", "dimgray", "dimgrey", "dodgerblue", "firebrick",
        "floralwhite", "forestgreen", "fuchsia", "gainsboro", "ghostwhite", "gold", "goldenrod", "gray", "green",
        "greenyellow", "grey", "honeydew", "hotpink", "indianred", "indigo", "ivory", "khaki", "lavender",
        "lavenderblush", "lawngreen", "lemonchiffon", "lightblue", "lightcoral", "lightcyan",
        "lightgoldenrodyellow", "lightgray", "lightgreen", "lightgrey", "lightpink", "lightsalmon",
        "lightseagreen", "lightskyblue", "lightslategray", "lightslategrey", "lightsteelblue", "lightyellow",
        "lime", "limegreen", "linen", "magenta", "maroon", "mediumaquamarine", "mediumblue", "mediumorchid",
        "mediumpurple", "mediumseagreen", "mediumslateblue", "mediumspringgreen", "mediumturquoise",
        "mediumvioletred", "midnightblue", "mintcream", "mistyrose", "moccasin", "navajowhite", "navy", "oldlace",
        "olive", "olivedrab", "orange", "orangered", "orchid", "palegoldenrod", "palegreen", "paleturquoise",
        "palevioletred", "papayawhip", "peachpuff", "peru", "pink", "plum", "powderblue", "purple",
        "rebeccapurple", "red", "rosybrown", "royalblue", "saddlebrown", "salmon", "sandybrown", "seagreen",
        "seashell", "sienna", "silver", "skyblue", "slateblue", "slategray", "slategrey", "snow", "springgreen",
        "steelblue", "tan", "teal", "thistle", "tomato", "turquoise", "violet", "wheat", "white", "whitesmoke",
        "yellow", "yellowgreen", "transparent", "currentcolor",
    };
    return s_names;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string rgb_text(double r, double g, double b, double alpha) {
    auto channel = [](double value) {
        return format_number(std::round(std::min(255.0, std::max(0.0, value))));
    };
    if (alpha >= 1) return "rgb(" + channel(r) + ", " + channel(g) + ", " + channel(b) + ")";
    const double rounded = std::round(std::max(0.0, alpha) * 1000) / 1000;
    return "rgba(" + channel(r) + ", " + channel(g) + ", " + channel(b) + ", " + format_number(rounded) + ")";
}

// Splits the arguments of a function on commas, whitespace and "/".
std::vector<std::string> function_arguments(const std::string& inside) {
    std::vector<std::string> out;
    std::string current;
    for (char c : inside) {
        if (c == ',' || c == '/' || is_ascii_space(c)) {
            if (!current.empty()) out.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

bool parse_channel(const std::string& text, double scale, double& out) {
    std::size_t at = 0;
    double number = 0;
    if (!parse_css_number(text, at, number)) return false;
    const std::string unit = text.substr(at);
    if (unit == "%") out = number / 100 * scale;
    else if (unit.empty()) out = number;
    else return false;
    return true;
}

double hue_to_channel(double p, double q, double t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0 / 6) return p + (q - p) * 6 * t;
    if (t < 1.0 / 2) return q;
    if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}

// A colour, written the way a browser gives it back.
bool normalize_color(const std::string& text, std::string& out) {
    const std::string lower = ascii_lower(text);
    if (named_colors().count(lower) || is_global_keyword(lower)) {
        out = lower;
        return true;
    }
    if (is_function_value(lower, "var")) {
        out = text;
        return true;
    }
    if (!lower.empty() && lower[0] == '#') {
        const std::string digits = lower.substr(1);
        for (char c : digits) {
            if (hex_digit(c) < 0) return false;
        }
        double channels[4] = {0, 0, 0, 255};
        if (digits.size() == 3 || digits.size() == 4) {
            for (std::size_t i = 0; i < digits.size(); ++i) channels[i] = hex_digit(digits[i]) * 17;
        } else if (digits.size() == 6 || digits.size() == 8) {
            for (std::size_t i = 0; i < digits.size() / 2; ++i) {
                channels[i] = hex_digit(digits[2 * i]) * 16 + hex_digit(digits[2 * i + 1]);
            }
        } else {
            return false;
        }
        out = rgb_text(channels[0], channels[1], channels[2], channels[3] / 255);
        return true;
    }
    const bool is_rgb = is_function_value(lower, "rgb") || is_function_value(lower, "rgba");
    const bool is_hsl = is_function_value(lower, "hsl") || is_function_value(lower, "hsla");
    if (!is_rgb && !is_hsl) return false;
    const auto open = lower.find('(');
    const auto arguments = function_arguments(lower.substr(open + 1, lower.size() - open - 2));
    if (arguments.size() != 3 && arguments.size() != 4) return false;
    double alpha = 1;
    if (arguments.size() == 4 && !parse_channel(arguments[3], 1, alpha)) return false;
    if (is_rgb) {
        double r = 0, g = 0, b = 0;
        if (!parse_channel(arguments[0], 255, r) || !parse_channel(arguments[1], 255, g) || !parse_channel(arguments[2], 255, b)) {
            return false;
        }
        out = rgb_text(r, g, b, alpha);
        return true;
    }
    std::size_t at = 0;
    double hue = 0, saturation = 0, lightness = 0;
    if (!parse_css_number(arguments[0], at, hue)) return false;
    if (!parse_channel(arguments[1], 1, saturation) || !parse_channel(arguments[2], 1, lightness)) return false;
    if (arguments[1].back() != '%' || arguments[2].back() != '%') return false;
    hue = std::fmod(std::fmod(hue, 360) + 360, 360) / 360;
    double r = lightness, g = lightness, b = lightness;
    if (saturation != 0) {
        const double q = lightness < 0.5 ? lightness * (1 + saturation) : lightness + saturation - lightness * saturation;
        const double p = 2 * lightness - q;
        r = hue_to_channel(p, q, hue + 1.0 / 3);
        g = hue_to_channel(p, q, hue);
        b = hue_to_channel(p, q, hue - 1.0 / 3);
    }
    out = rgb_text(r * 255, g * 255, b * 255, alpha);
    return true;
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
    return normalize_length(text, out) && out.back() != '%';
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
                                 "outline-offset"}) {
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
        rules["text-align"] = {ValueKind::Keyword,
                               {"left", "right", "center", "justify", "start", "end", "match-parent", "-webkit-center"}};
        rules["font-style"] = {ValueKind::Keyword, {"normal", "italic", "oblique"}};
        rules["text-transform"] = {ValueKind::Keyword, {"none", "capitalize", "uppercase", "lowercase", "full-width"}};
        rules["opacity"] = {ValueKind::Number, {}};
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

// Checks `text` against what `property` accepts and gives it back the way
// it is stored. False when the declaration must be dropped.
bool normalize_value(const std::string& property, const std::string& text, std::string& out) {
    const std::string value = trim(text);
    if (value.empty()) return false;
    if (is_custom_property(property)) {
        out = value;
        return true;
    }
    const std::string lower = ascii_lower(value);
    auto found = property_rules().find(property);
    if (found == property_rules().end()) {
        out = value;
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
            out = value;
            return true;
        case ValueKind::Length:
        case ValueKind::LengthOrAuto:
        case ValueKind::LengthOrNone:
        case ValueKind::FontSize:
            return normalize_length(value, out);
        case ValueKind::LineHeight: {
            std::size_t at = 0;
            double number = 0;
            if (parse_css_number(lower, at, number) && at == lower.size()) {
                out = lower;
                return true;
            }
            return normalize_length(value, out);
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
            if (!parse_css_number(lower, at, number)) return false;
            const std::string unit = lower.substr(at);
            if (!unit.empty() && !(unit == "%" && property == "opacity")) return false;
            out = lower;
            return true;
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

// ---------------------------------------------------------------------------
// Shorthands
// ---------------------------------------------------------------------------

enum class ShorthandKind { FourSides, Side, Border, Background };

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

    // The value of a property, a shorthand's put together from its parts.
    std::string get(const std::string& name) const {
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
            case ShorthandKind::Background:
                break;
        }
        return std::string();
    }

    bool is_important(const std::string& name) const {
        if (const Shorthand* shorthand = find_shorthand(name)) {
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
    bool set(const std::string& name, const std::string& text, bool important) {
        if (const Shorthand* shorthand = find_shorthand(name)) {
            std::vector<std::string> values;
            std::string raw;
            if (!expand(*shorthand, text, values, raw)) return false;
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
            for (std::size_t i = 0; i < shorthand->longhands.size(); ++i) {
                store(shorthand->longhands[i], values[i], important, true);
            }
            return true;
        }
        std::string value;
        if (!normalize_value(name, text, value)) return false;
        bool covered = false;
        for (const auto& shorthand : shorthands()) {
            if (covers(shorthand, name) && find(shorthand.name)) {
                covered = true;
                if (shorthand.kind == ShorthandKind::Background) find(shorthand.name)->value = value;
            }
        }
        store(name, value, important, covered);
        return true;
    }

    // Removes a property; returns the value it had.
    std::string remove(const std::string& name) {
        const std::string old = get(name);
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

    std::string text() const {
        std::string out;
        auto write = [&](const std::string& name, const std::string& value, bool important) {
            if (!out.empty()) out += ' ';
            out += name + ": " + value + (important ? " !important;" : ";");
        };
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
            if (!is_custom_property(declaration.name)) declaration.name = ascii_lower(declaration.name);
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

void apply_declarations(Block& block, const std::string& text) {
    for (const auto& declaration : parse_declarations(text)) {
        if (!is_css_property(declaration.name)) continue;
        block.set(declaration.name, declaration.value, declaration.important);
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
    /// The <style> elements of the window's document, at `version`.
    std::vector<std::shared_ptr<Element>> style_elements;
    uint64_t version{0};
    const Node* root{nullptr};
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
    part.version = Node::tree_version();
    part.root = &root;
    return part.style_elements;
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
        "border-spacing", "caption-side", "tab-size", "writing-mode", "text-rendering",
    };
    return is_custom_property(property) || s_inherited.count(property) != 0;
}

const char* initial_value(const std::string& property) {
    if (property == "display") return "inline";
    if (property == "visibility") return "visible";
    if (property == "pointer-events") return "auto";
    return "";
}

// What the cascade gives one element: the user-agent default, the rules of
// the document's style elements in order, then the style attribute.
void compute_block(Realm& realm, const Element& element, Block& block) {
    block.clear();
    if (element.has_attribute("hidden") && !element.is_foreign()) block.set("display", "none", false);
    else block.set("display", default_display(element), false);

    auto apply = [&](const RawDeclaration& declaration) {
        if (!is_css_property(declaration.name)) return;
        // A declaration that is not important never replaces one that is.
        if (!declaration.important && block.is_important(declaration.name) && !block.get(declaration.name).empty()) return;
        block.set(declaration.name, declaration.value, declaration.important);
    };

    // The tree the element is in decides which style elements apply.
    const Node* root = &element;
    std::shared_ptr<Node> hold;
    while (auto parent = root->parent()) {
        hold = parent;
        root = hold.get();
    }
    if (root->is_document() || root->is_document_fragment()) {
        for (const auto& style : style_elements_of(realm, *root)) {
            auto sheet = sheet_for(realm, style->text_content());
            for (const auto& rule : sheet->rules) {
                if (!rule.selector.matches(element)) continue;
                for (const auto& declaration : rule.declarations) apply(declaration);
            }
        }
    }
    if (const std::string* inline_text = element.attribute("style")) {
        for (const auto& declaration : parse_declarations(*inline_text)) apply(declaration);
    }
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
        apply_declarations(block, *attribute);
        source = *attribute;
        has_source = true;
    }

    void store() {
        source = block.text();
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
        std::string value = block.get(name);
        if (!computed || !value.empty()) return value;
        // Not declared for this element: inherited from above, or the initial value.
        if (is_inherited(name) && realm->alive) {
            Block inherited;
            for (auto at = element->parent(); at && at->is_element(); at = at->parent()) {
                compute_block(*realm, static_cast<Element&>(*at), inherited);
                value = inherited.get(name);
                if (!value.empty()) return value;
            }
        }
        return initial_value(name);
    }

    void set_value(const std::string& name, const std::string& text, bool important) {
        refuse_write();
        load();
        if (trim(text).empty()) {
            if (block.find(name) || find_shorthand(name)) {
                const std::string before = block.text();
                block.remove(name);
                if (block.text() != before) store();
            }
            return;
        }
        const std::string before = block.text();
        if (!block.set(name, text, important)) return;
        if (!has_source || block.text() != before) store();
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
    return is_custom_property(name) ? name : ascii_lower(name);
}

void setup_style_declaration(Iface& I) {
    I.accessor("cssText",
        [](Realm&) {
            StyleState& state = this_style();
            state.load();
            return str(state.block.text());
        },
        [](Realm&, const JsValue& value) {
            StyleState& state = this_style();
            state.refuse_write();
            state.block.clear();
            apply_declarations(state.block, to_str_null_empty(value));
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
        return str(state.block.is_important(property_argument(args[0])) ? "important" : "");
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
        const std::string before = state.block.text();
        const std::string old = state.block.remove(name);
        if (state.block.text() != before) state.store();
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
    apply_declarations(state.block, text);
    state.store();
}

void register_style_interfaces() {
    register_interface({"CSSStyleDeclaration", nullptr, setup_style_declaration, nullptr, 0, nullptr});
}

} // namespace dom
} // namespace litebird
