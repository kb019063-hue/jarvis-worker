// Test-only adapter: no second implementation of shaping lives here.
#include "litebird/render/FontRenderer.hpp"
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc < 3 || (std::string(argv[2]) != "ltr" && std::string(argv[2]) != "rtl"))
            throw std::runtime_error("usage: shaping-probe FONT ltr|rtl [hex codepoints...]");
        litebird::TrueTypeFont font;
        if (!font.load_file(argv[1])) throw std::runtime_error("cannot load font");
        std::vector<uint32_t> text;
        for (int i = 3; i < argc; ++i) {
            size_t end;
            auto cp = std::stoul(argv[i], &end, 16);
            if (argv[i][end] || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
                throw std::runtime_error("invalid Unicode scalar");
            text.push_back(cp);
        }
        auto glyphs = font.shape_glyphs(text, true, std::string(argv[2]) == "rtl");
        std::cout << "[";
        bool first = true;
        for (const auto& g : glyphs) {
            if (!first) std::cout << ",";
            first = false;
            std::cout << "[" << g.glyph << "," << g.x_advance << "," << g.y_advance
                      << "," << g.x_offset << "," << g.y_offset << "," << g.cluster << "]";
        }
        std::cout << "]\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 2;
    }
}
