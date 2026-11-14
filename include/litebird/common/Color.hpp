#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace litebird {

struct Color {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};

    constexpr Color() = default;
    constexpr Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        : r(red), g(green), b(blue), a(alpha) {}

    bool is_transparent() const { return a == 0; }

    constexpr uint32_t to_argb() const {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8) |
               static_cast<uint32_t>(b);
    }

    static constexpr Color from_argb(uint32_t val) {
        return Color(
            static_cast<uint8_t>((val >> 16) & 0xFF),
            static_cast<uint8_t>((val >> 8) & 0xFF),
            static_cast<uint8_t>(val & 0xFF),
            static_cast<uint8_t>((val >> 24) & 0xFF)
        );
    }

    // The colour a name stands for, as 0xRRGGBB. The 148 names below are the
    // CSS color keywords checked against jsdom; `transparent` and `currentcolor`
    // are not here because neither is a colour of its own.
    static bool named_color_value(const std::string& lower, unsigned& out) {
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

    static Color transparent() { return Color(0, 0, 0, 0); }
    static Color black()       { return Color(0, 0, 0, 255); }
    static Color white()       { return Color(255, 255, 255, 255); }
    static Color red()         { return Color(255, 0, 0, 255); }
    static Color green()       { return Color(0, 128, 0, 255); }
    static Color blue()        { return Color(0, 0, 255, 255); }
    static Color gray()        { return Color(128, 128, 128, 255); }
    static Color dark_gray()   { return Color(40, 44, 52, 255); }
    static Color light_gray()  { return Color(220, 220, 220, 255); }

    static Color parse(std::string_view str) {
        if (str.empty()) return Color::black();

        // Trim whitespace
        while (!str.empty() && std::isspace(str.front())) str.remove_prefix(1);
        while (!str.empty() && std::isspace(str.back())) str.remove_suffix(1);

        if (str.starts_with("#")) {
            str.remove_prefix(1);
            if (str.length() == 3) {
                // #RGB -> #RRGGBB
                auto hex_val = [](char c) -> uint8_t {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return 0;
                };
                uint8_t r = hex_val(str[0]) * 17;
                uint8_t g = hex_val(str[1]) * 17;
                uint8_t b = hex_val(str[2]) * 17;
                return Color(r, g, b, 255);
            } else if (str.length() >= 6) {
                try {
                    unsigned long val = std::stoul(std::string(str.substr(0, 6)), nullptr, 16);
                    uint8_t r = (val >> 16) & 0xFF;
                    uint8_t g = (val >> 8) & 0xFF;
                    uint8_t b = val & 0xFF;
                    return Color(r, g, b, 255);
                } catch (...) {
                    return Color::black();
                }
            }
        }

        if (str.starts_with("rgb(") && str.ends_with(")")) {
            auto inner = str.substr(4, str.length() - 5);
            int r = 0, g = 0, b = 0;
            char comma1, comma2;
            std::stringstream ss{std::string(inner)};
            if (ss >> r >> comma1 >> g >> comma2 >> b) {
                return Color(
                    static_cast<uint8_t>(std::clamp(r, 0, 255)),
                    static_cast<uint8_t>(std::clamp(g, 0, 255)),
                    static_cast<uint8_t>(std::clamp(b, 0, 255)),
                    255
                );
            }
        }

        std::string lower(str);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

        if (lower == "transparent") return Color(0, 0, 0, 0);
        unsigned named = 0;
        if (named_color_value(lower, named)) {
            return Color(static_cast<uint8_t>((named >> 16) & 0xFF),
                         static_cast<uint8_t>((named >> 8) & 0xFF),
                         static_cast<uint8_t>(named & 0xFF), 255);
        }

        return Color::black();
    }

    std::string to_hex() const {
        std::stringstream ss;
        ss << "#" << std::hex << std::setfill('0')
           << std::setw(2) << (int)r
           << std::setw(2) << (int)g
           << std::setw(2) << (int)b;
        return ss.str();
    }

    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
};

} // namespace litebird
