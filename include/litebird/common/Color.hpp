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
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        static const std::unordered_map<std::string, Color> named = {
            {"black",       Color(0, 0, 0)},
            {"white",       Color(255, 255, 255)},
            {"red",         Color(220, 53, 69)},
            {"green",       Color(40, 167, 69)},
            {"blue",        Color(0, 123, 255)},
            {"yellow",      Color(255, 193, 7)},
            {"cyan",        Color(23, 162, 184)},
            {"magenta",     Color(232, 62, 140)},
            {"orange",      Color(253, 126, 20)},
            {"purple",      Color(111, 66, 193)},
            {"gray",        Color(108, 117, 125)},
            {"grey",        Color(108, 117, 125)},
            {"lightgray",   Color(240, 240, 240)},
            {"lightgrey",   Color(240, 240, 240)},
            {"darkgray",    Color(52, 58, 64)},
            {"darkgrey",    Color(52, 58, 64)},
            {"navy",        Color(0, 0, 128)},
            {"teal",        Color(32, 201, 151)},
            {"transparent", Color(0, 0, 0, 0)},
        };

        auto it = named.find(lower);
        if (it != named.end()) {
            return it->second;
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
