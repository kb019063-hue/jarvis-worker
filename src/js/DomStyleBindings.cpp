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

// The colour a name stands for, as 0xRRGGBB. `transparent` and
// `currentcolor` are not here: neither is a colour of its own.
bool named_color_value(const std::string& lower, unsigned& out) {
    static const std::unordered_map<std::string, unsigned> s_values = {
        {"aliceblue", 0xf0f8ff}, {"antiquewhite", 0xfaebd7}, {"aqua", 0x00ffff}, {"aquamarine", 0x7fffd4},
        {"azure", 0xf0ffff}, {"beige", 0xf5f5dc}, {"bisque", 0xffe4c4}, {"black", 0x000000},
        {"blanchedalmond", 0xffebcd}, {"blue", 0x0000ff}, {"blueviolet", 0x8a2be2}, {"brown", 0xa52a2a},
        {"burlywood", 0xdeb887}, {"cadetblue", 0x5f9ea0}, {"chartreuse", 0x7fff00}, {"chocolate", 0xd2691e},
        {"coral", 0xff7f50}, {"cornflowerblue", 0x6495ed}, {"cornsilk", 0xfff8dc}, {"crimson", 0xdc143c},
        {"cyan", 0x00ffff}, {"darkblue", 0x00008b}, {"darkcyan", 0x008b8b}, {"darkgoldenrod", 0xb8860b},
        {"darkgray", 0xa9a9a9}, {"darkgreen", 0x006400}, {"darkgrey", 0xa9a9a9}, {"darkkhaki", 0xbdb76b},
        {"darkmagenta", 0x8b008b}, {"darkolivegreen", 0x556b2f}, {"darkorange", 0xff8c00}, {"darkorchid", 0x9932cc},
        {"darkred", 0x8b0000}, {"darksalmon", 0xe9967a}, {"darkseagreen", 0x8fbc8f}, {"darkslateblue", 0x483d8b},
        {"darkslategray", 0x2f4f4f}, {"darkslategrey", 0x2f4f4f}, {"darkturquoise", 0x00ced1},
        {"darkviolet", 0x9400d3}, {"deeppink", 0xff1493}, {"deepskyblue", 0x00bfff}, {"dimgray", 0x696969},
        {"dimgrey", 0x696969}, {"dodgerblue", 0x1e90ff}, {"firebrick", 0xb22222}, {"floralwhite", 0xfffaf0},
        {"forestgreen", 0x228b22}, {"fuchsia", 0xff00ff}, {"gainsboro", 0xdcdcdc}, {"ghostwhite", 0xf8f8ff},
        {"gold", 0xffd700}, {"goldenrod", 0xdaa520}, {"gray", 0x808080}, {"green", 0x008000},
        {"greenyellow", 0xadff2f}, {"grey", 0x808080}, {"honeydew", 0xf0fff0}, {"hotpink", 0xff69b4},
        {"indianred", 0xcd5c5c}, {"indigo", 0x4b0082}, {"ivory", 0xfffff0}, {"khaki", 0xf0e68c},
        {"lavender", 0xe6e6fa}, {"lavenderblush", 0xfff0f5}, {"lawngreen", 0x7cfc00}, {"lemonchiffon", 0xfffacd},
        {"lightblue", 0xadd8e6}, {"lightcoral", 0xf08080}, {"lightcyan", 0xe0ffff},
        {"lightgoldenrodyellow", 0xfafad2}, {"lightgray", 0xd3d3d3}, {"lightgreen", 0x90ee90},
        {"lightgrey", 0xd3d3d3}, {"lightpink", 0xffb6c1}, {"lightsalmon", 0xffa07a}, {"lightseagreen", 0x20b2aa},
        {"lightskyblue", 0x87cefa}, {"lightslategray", 0x778899}, {"lightslategrey", 0x778899},
        {"lightsteelblue", 0xb0c4de}, {"lightyellow", 0xffffe0}, {"lime", 0x00ff00}, {"limegreen", 0x32cd32},
        {"linen", 0xfaf0e6}, {"magenta", 0xff00ff}, {"maroon", 0x800000}, {"mediumaquamarine", 0x66cdaa},
        {"mediumblue", 0x0000cd}, {"mediumorchid", 0xba55d3}, {"mediumpurple", 0x9370db},
        {"mediumseagreen", 0x3cb371}, {"mediumslateblue", 0x7b68ee}, {"mediumspringgreen", 0x00fa9a},
        {"mediumturquoise", 0x48d1cc}, {"mediumvioletred", 0xc71585}, {"midnightblue", 0x191970},
        {"mintcream", 0xf5fffa}, {"mistyrose", 0xffe4e1}, {"moccasin", 0xffe4b5}, {"navajowhite", 0xffdead},
        {"navy", 0x000080}, {"oldlace", 0xfdf5e6}, {"olive", 0x808000}, {"olivedrab", 0x6b8e23},
        {"orange", 0xffa500}, {"orangered", 0xff4500}, {"orchid", 0xda70d6}, {"palegoldenrod", 0xeee8aa},
        {"palegreen", 0x98fb98}, {"paleturquoise", 0xafeeee}, {"palevioletred", 0xdb7093}, {"papayawhip", 0xffefd5},
        {"peachpuff", 0xffdab9}, {"peru", 0xcd853f}, {"pink", 0xffc0cb}, {"plum", 0xdda0dd},
        {"powderblue", 0xb0e0e6}, {"purple", 0x800080}, {"rebeccapurple", 0x663399}, {"red", 0xff0000},
        {"rosybrown", 0xbc8f8f}, {"royalblue", 0x4169e1}, {"saddlebrown", 0x8b4513}, {"salmon", 0xfa8072},
        {"sandybrown", 0xf4a460}, {"seagreen", 0x2e8b57}, {"seashell", 0xfff5ee}, {"sienna", 0xa0522d},
        {"silver", 0xc0c0c0}, {"skyblue", 0x87ceeb}, {"slateblue", 0x6a5acd}, {"slategray", 0x708090},
        {"slategrey", 0x708090}, {"snow", 0xfffafa}, {"springgreen", 0x00ff7f}, {"steelblue", 0x4682b4},
        {"tan", 0xd2b48c}, {"teal", 0x008080}, {"thistle", 0xd8bfd8}, {"tomato", 0xff6347}, {"turquoise", 0x40e0d0},
        {"violet", 0xee82ee}, {"wheat", 0xf5deb3}, {"white", 0xffffff}, {"whitesmoke", 0xf5f5f5},
        {"yellow", 0xffff00}, {"yellowgreen", 0x9acd32},
    };
    auto found = s_values.find(lower);
    if (found == s_values.end()) return false;
    out = found->second;
    return true;
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
        // Eight bits of alpha are written with two decimals when two are
        // enough to get the same eight bits back, and with three otherwise:
        // #11223380 is 0.5, not 0.502.
        double alpha = 1;
        if (channels[3] < 255) {
            const double hundredths = std::round(channels[3] * 100 / 255.0);
            alpha = std::round(hundredths * 255 / 100.0) == channels[3] ? hundredths / 100
                                                                         : std::round(channels[3] * 1000 / 255.0) / 1000;
        }
        out = rgb_text(channels[0], channels[1], channels[2], alpha);
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

using Parts = std::vector<std::pair<std::string, std::string>>;

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
bool take_apart(const std::string& name, const std::string& value, Parts& out) {
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
        std::string direction = "row", wrap = "nowrap";
        for (const auto& part : parts) {
            const std::string word = ascii_lower(part);
            if (word == "row" || word == "row-reverse" || word == "column" || word == "column-reverse") direction = word;
            else if (word == "nowrap" || word == "wrap" || word == "wrap-reverse") wrap = word;
            else return false;
        }
        out.emplace_back("flex-direction", direction);
        out.emplace_back("flex-wrap", wrap);
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
                if (is_plain_number(part) && grow.empty()) grow = part;
                else if (is_plain_number(part) && shrink.empty()) shrink = part;
                else if (basis.empty()) basis = is_plain_number(part) ? part + "px" : part;
                else return false;
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

// The value of a shorthand, from the computed values of its parts in the
// order parts_of() has them. "" when one of them is not known.
std::string put_together(const std::string& name, const std::vector<std::string>& v) {
    for (const auto& part : v) {
        if (part.empty()) return std::string();
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
                                           [&](const Entry& kept) { return covered_by(declaration.name, kept.name); }),
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

std::string computed_value(Realm& realm, const Element& element, const Block& block, const std::string& name,
                           int depth);

// `value` is what the cascade (or inheritance, or the table of initial
// values) gave `subject` for `name`; it was declared on `from`, whose block
// is `block`. This makes the computed value of it: an absolute colour, an
// absolute length.
std::string resolved(Realm& realm, const Element& subject, const Element& from, const Block& block,
                     const std::string& name, const std::string& value, int depth) {
    if (is_custom_property(name)) return value;
    const std::string lower = ascii_lower(value);
    if (is_color_property(name)) {
        if (lower == "currentcolor") {
            return name == "color" ? std::string("rgb(0, 0, 0)") : computed_value(realm, from, block, "color", depth + 1);
        }
        if (lower == "transparent") return "rgba(0, 0, 0, 0)";
        unsigned rgb = 0;
        if (named_color_value(lower, rgb)) return rgb_text((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 1);
        return value;
    }
    if (name == "font-size") return px_text(font_size_px(realm, from));
    if (name == "font-weight") return format_number(font_weight_of(realm, from));

    // A calc() of lengths is a length. With a percentage in it there is
    // nothing more to do without layout, and it is written the short way.
    std::string text = lower;
    if (is_function_value(lower, "calc")) {
        const bool font_relative = lower.find("em") != std::string::npos;
        const double em = font_relative ? font_size_px(realm, from) : 16;
        const double rem = lower.find("rem") != std::string::npos ? root_font_size(realm, from) : 16;
        CalcValue sum;
        if (!eval_calc(lower, em, rem, sum)) return value;
        if (sum.percent != 0) {
            const std::string percent = format_number(sum.percent) + "%";
            if (sum.px == 0) return percent;
            std::string length = px_text(std::fabs(sum.px));
            return "calc(" + percent + (sum.px < 0 ? " - " : " + ") + length + ")";
        }
        text = px_text(sum.px);
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
    if (const std::vector<std::string>* parts = parts_of(name)) {
        std::vector<std::string> values;
        for (const auto& part : *parts) values.push_back(computed_longhand(realm, element, block, part, depth + 1));
        return put_together(name, values);
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
        return parts.get(name);
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
        if (!computed || !realm->alive) return block.get(name);
        return computed_value(*realm, *element, block, name, 0);
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
