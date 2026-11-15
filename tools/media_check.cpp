#include "litebird/render/JpegDecoder.hpp"
#include "litebird/render/GifDecoder.hpp"
#include "litebird/render/SvgRenderer.hpp"
#include "litebird/html/HtmlParser.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool read_file_bytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    ifs.seekg(0, std::ios::end);
    const std::streamoff sz = ifs.tellg();
    if (sz < 0) return false;
    ifs.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(sz));
    if (sz > 0) {
        ifs.read(reinterpret_cast<char*>(out.data()), sz);
    }
    return ifs.good() || ifs.eof();
}

bool write_ppm(const std::string& path, const litebird::ImageData& img) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;
    ofs << "P6\n" << img.width << " " << img.height << "\n255\n";
    std::vector<uint8_t> rgb(static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 3u);
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        const auto c = litebird::Color::from_argb(img.pixels[i]);
        rgb[i * 3 + 0] = c.r;
        rgb[i * 3 + 1] = c.g;
        rgb[i * 3 + 2] = c.b;
    }
    ofs.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    return ofs.good();
}

bool write_rgba(const std::string& path, const litebird::ImageData& img) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;
    std::vector<uint8_t> rgba(static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 4u);
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        const auto c = litebird::Color::from_argb(img.pixels[i]);
        rgba[i * 4 + 0] = c.r;
        rgba[i * 4 + 1] = c.g;
        rgba[i * 4 + 2] = c.b;
        rgba[i * 4 + 3] = c.a;
    }
    ofs.write(reinterpret_cast<const char*>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
    return ofs.good();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: media_check <--decode-jpeg|--decode-gif|--render-svg|--fuzz-jpeg|--fuzz-gif|--fuzz-svg> <in> [out]\n";
        return 2;
    }
    const std::string mode = argv[1];

    if (mode == "--fuzz-batch") {
        // Usage: media_check --fuzz-batch <jpeg|gif|svg> <file1> [file2 ...]
        if (argc < 4) return 2;
        const std::string kind = argv[2];
        for (int i = 3; i < argc; ++i) {
            std::vector<uint8_t> b;
            if (!read_file_bytes(argv[i], b)) continue;
            if (kind == "jpeg") {
                (void)litebird::JpegDecoder::decode_detailed(b.data(), b.size());
            } else if (kind == "gif") {
                (void)litebird::GifDecoder::decode_frames(b.data(), b.size(), 16);
            } else if (kind == "svg") {
                std::string markup(reinterpret_cast<const char*>(b.data()), b.size());
                auto doc = litebird::HtmlParser::parse(markup);
                auto svgs = doc->get_elements_by_tag_name("svg");
                if (!svgs.empty()) {
                    (void)litebird::SvgRenderer::render(svgs[0], 64.0f, 64.0f);
                }
            } else {
                return 2;
            }
        }
        return 0;
    }

    const std::string in_path = argv[2];
    std::vector<uint8_t> bytes;
    if (!read_file_bytes(in_path, bytes)) {
        std::cerr << "Failed to read " << in_path << "\n";
        return 2;
    }

    if (mode == "--decode-jpeg") {
        if (argc < 4) return 2;
        auto res = litebird::JpegDecoder::decode_detailed(bytes.data(), bytes.size());
        if (!res.ok()) {
            std::cerr << "JPEG error: " << res.error << "\n";
            return 1;
        }
        if (!write_ppm(argv[3], *res.image)) {
            std::cerr << "Failed to write PPM\n";
            return 2;
        }
        std::cout << res.width << " " << res.height << " " << res.components << " "
                  << (res.progressive ? "progressive" : "sequential") << "\n";
        return 0;
    }

    if (mode == "--decode-gif") {
        if (argc < 4) return 2;
        auto res = litebird::GifDecoder::decode_frames(bytes.data(), bytes.size());
        if (!res.ok()) {
            std::cerr << "GIF error: " << res.error << "\n";
            return 1;
        }
        const std::string prefix = argv[3];
        std::cout << res.width << " " << res.height << " " << res.frames.size() << " " << res.loop_count << "\n";
        for (size_t i = 0; i < res.frames.size(); ++i) {
            const std::string out_file = prefix + "_" + std::to_string(i) + ".rgba";
            if (!write_rgba(out_file, *res.frames[i].image)) return 2;
            std::cout << res.frames[i].delay_ms << " " << static_cast<int>(res.frames[i].disposal) << "\n";
        }
        return 0;
    }

    if (mode == "--render-svg") {
        if (argc < 4) return 2;
        const float w = (argc >= 6) ? std::strtof(argv[4], nullptr) : -1.0f;
        const float h = (argc >= 6) ? std::strtof(argv[5], nullptr) : -1.0f;
        std::string markup(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        auto doc = litebird::HtmlParser::parse(markup);
        auto svgs = doc->get_elements_by_tag_name("svg");
        if (svgs.empty()) {
            std::cerr << "No <svg> element found\n";
            return 1;
        }
        auto img = litebird::SvgRenderer::render(svgs[0], w, h);
        if (!img || !img->is_valid()) {
            std::cerr << "SVG render returned null\n";
            return 1;
        }
        if (!write_rgba(argv[3], *img)) return 2;
        std::cout << img->width << " " << img->height << "\n";
        return 0;
    }

    if (mode == "--fuzz-jpeg") {
        (void)litebird::JpegDecoder::decode_detailed(bytes.data(), bytes.size());
        return 0;
    }
    if (mode == "--fuzz-gif") {
        (void)litebird::GifDecoder::decode_frames(bytes.data(), bytes.size(), 16);
        return 0;
    }
    if (mode == "--fuzz-svg") {
        std::string markup(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        auto doc = litebird::HtmlParser::parse(markup);
        auto svgs = doc->get_elements_by_tag_name("svg");
        if (!svgs.empty()) {
            (void)litebird::SvgRenderer::render(svgs[0], 64.0f, 64.0f);
        }
        return 0;
    }

    std::cerr << "Unknown mode: " << mode << "\n";
    return 2;
}
