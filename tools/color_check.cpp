#include "litebird/common/Color.hpp"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

bool parse_space_arg(const std::string& s, litebird::ColorSpace& out) {
    using litebird::ColorSpace;
    if (s == "srgb") { out = ColorSpace::Srgb; return true; }
    if (s == "srgb-linear") { out = ColorSpace::SrgbLinear; return true; }
    if (s == "display-p3") { out = ColorSpace::DisplayP3; return true; }
    if (s == "display-p3-linear") { out = ColorSpace::DisplayP3Linear; return true; }
    if (s == "a98-rgb") { out = ColorSpace::A98Rgb; return true; }
    if (s == "prophoto-rgb") { out = ColorSpace::ProphotoRgb; return true; }
    if (s == "rec2020") { out = ColorSpace::Rec2020; return true; }
    if (s == "xyz-d50") { out = ColorSpace::XyzD50; return true; }
    if (s == "xyz-d65") { out = ColorSpace::XyzD65; return true; }
    if (s == "lab") { out = ColorSpace::Lab; return true; }
    if (s == "lch") { out = ColorSpace::Lch; return true; }
    if (s == "oklab") { out = ColorSpace::Oklab; return true; }
    if (s == "oklch") { out = ColorSpace::Oklch; return true; }
    if (s == "hsl") { out = ColorSpace::Hsl; return true; }
    if (s == "hwb") { out = ColorSpace::Hwb; return true; }
    return false;
}

const char* space_name(litebird::ColorSpace s) {
    using litebird::ColorSpace;
    switch (s) {
        case ColorSpace::Srgb: return "srgb";
        case ColorSpace::SrgbLinear: return "srgb-linear";
        case ColorSpace::DisplayP3: return "display-p3";
        case ColorSpace::DisplayP3Linear: return "display-p3-linear";
        case ColorSpace::A98Rgb: return "a98-rgb";
        case ColorSpace::ProphotoRgb: return "prophoto-rgb";
        case ColorSpace::Rec2020: return "rec2020";
        case ColorSpace::XyzD50: return "xyz-d50";
        case ColorSpace::XyzD65: return "xyz-d65";
        case ColorSpace::Lab: return "lab";
        case ColorSpace::Lch: return "lch";
        case ColorSpace::Oklab: return "oklab";
        case ColorSpace::Oklch: return "oklch";
        case ColorSpace::Hsl: return "hsl";
        case ColorSpace::Hwb: return "hwb";
    }
    return "srgb";
}

void split_tabs(const std::string& line, std::string out[4], int& count) {
    count = 0;
    size_t start = 0;
    while (count < 3) {
        size_t tab = line.find('\t', start);
        if (tab == std::string::npos) break;
        out[count++] = line.substr(start, tab - start);
        start = tab + 1;
    }
    out[count++] = line.substr(start);
}

} // namespace

int main(int argc, char** argv) {
    using namespace litebird;
    if (argc < 2 || std::string_view(argv[1]) != "--batch") {
        std::cerr << "Usage: color_check --batch\n"
                  << "Reads tab-separated lines: <cmd>\\t<currentcolor>\\t<dark>\\t<expr>\n"
                  << "Commands: parse | convert:<space> | fuzz\n";
        return 2;
    }

    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    std::cout << std::setprecision(12);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        std::string fields[4];
        int n = 0;
        split_tabs(line, fields, n);
        if (n < 4) {
            std::cout << "ERR\n";
            continue;
        }
        const std::string& cmd = fields[0];
        const std::string& cc_str = fields[1];
        const bool dark = (fields[2] == "1" || fields[2] == "dark");
        const std::string& expr = fields[3];

        ColorResolveContext ctx;
        ctx.dark_scheme = dark;
        CssColor cc;
        if (!cc_str.empty() && cc_str != "-") {
            if (CssColor::parse(cc_str, cc)) ctx.current_color = &cc;
        }

        if (cmd == "parse") {
            CssColor parsed;
            if (!CssColor::parse(expr, parsed, ctx)) {
                std::cout << "ERR\n";
                continue;
            }
            const Color rgba = parsed.to_srgb8();
            std::cout << "OK\t" << parsed.serialize_computed() << "\t"
                      << static_cast<int>(rgba.r) << "\t"
                      << static_cast<int>(rgba.g) << "\t"
                      << static_cast<int>(rgba.b) << "\t"
                      << static_cast<int>(rgba.a) << "\t"
                      << space_name(parsed.space) << "\t"
                      << parsed.c[0] << "\t" << parsed.c[1] << "\t" << parsed.c[2] << "\t"
                      << parsed.alpha << "\n";
        } else if (cmd.rfind("convert:", 0) == 0) {
            ColorSpace target = ColorSpace::Srgb;
            if (!parse_space_arg(cmd.substr(8), target)) {
                std::cout << "ERR\n";
                continue;
            }
            CssColor parsed;
            if (!CssColor::parse(expr, parsed, ctx)) {
                std::cout << "ERR\n";
                continue;
            }
            const CssColor conv = parsed.convert_to(target);
            std::cout << "OK\t"
                      << conv.c[0] << "\t" << conv.c[1] << "\t" << conv.c[2] << "\t" << conv.alpha << "\t"
                      << (conv.missing[0] ? 1 : 0) << "\t"
                      << (conv.missing[1] ? 1 : 0) << "\t"
                      << (conv.missing[2] ? 1 : 0) << "\t"
                      << (conv.alpha_missing ? 1 : 0) << "\n";
        } else if (cmd == "fuzz") {
            CssColor parsed;
            if (CssColor::parse(expr, parsed, ctx)) {
                size_t sink = parsed.serialize_computed().size();
                const Color srgb8 = parsed.to_srgb8();
                sink += srgb8.r + srgb8.g + srgb8.b + srgb8.a;
                static constexpr ColorSpace kAll[] = {
                    ColorSpace::Srgb, ColorSpace::SrgbLinear, ColorSpace::DisplayP3,
                    ColorSpace::DisplayP3Linear, ColorSpace::A98Rgb, ColorSpace::ProphotoRgb,
                    ColorSpace::Rec2020, ColorSpace::XyzD50, ColorSpace::XyzD65,
                    ColorSpace::Lab, ColorSpace::Lch, ColorSpace::Oklab, ColorSpace::Oklch,
                    ColorSpace::Hsl, ColorSpace::Hwb,
                };
                for (ColorSpace sp : kAll) {
                    const CssColor c = parsed.convert_to(sp);
                    sink += c.serialize_computed().size();
                }
                if (sink == 0) std::cout << "ERR\n";
                else std::cout << "OK\n";
            } else {
                std::cout << "ERR\n";
            }
        } else {
            std::cout << "ERR\n";
        }
    }
    return 0;
}
