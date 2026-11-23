#include "litebird/render/JpegDecoder.hpp"
#include "litebird/render/GifDecoder.hpp"
#include "litebird/render/WebPDecoder.hpp"
#include "litebird/render/Vp8Decoder.hpp"
#include "litebird/render/AvifContainer.hpp"
#include "litebird/render/ImageDecoder.hpp"
#include "litebird/render/SvgRenderer.hpp"
#include "litebird/html/HtmlParser.hpp"

#include <cstdio>
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
        std::cerr << "Usage: media_check <--decode-jpeg|--decode-gif|--decode-webp|--decode-webp-anim|--decode-vp8|"
                     "--probe-avif|--decode-avif|--render-svg|"
                     "--fuzz-jpeg|--fuzz-gif|--fuzz-webp|--fuzz-vp8|--fuzz-avif|--fuzz-svg> <in> [out]\n";
        return 2;
    }
    const std::string mode = argv[1];

    if (mode == "--fuzz-batch") {
        // Usage: media_check --fuzz-batch <jpeg|gif|webp|vp8|avif|svg> <file1> [file2 ...]
        if (argc < 4) return 2;
        const std::string kind = argv[2];
        for (int i = 3; i < argc; ++i) {
            std::vector<uint8_t> b;
            if (!read_file_bytes(argv[i], b)) continue;
            if (kind == "jpeg") {
                (void)litebird::JpegDecoder::decode_detailed(b.data(), b.size());
            } else if (kind == "gif") {
                (void)litebird::GifDecoder::decode_frames(b.data(), b.size(), 16);
            } else if (kind == "webp") {
                (void)litebird::WebPDecoder::decode_detailed(b.data(), b.size());
                (void)litebird::WebPDecoder::decode_frames(b.data(), b.size(), 16);
            } else if (kind == "vp8") {
                litebird::Vp8Decoder::Frame frame;
                std::string error;
                if (litebird::Vp8Decoder::decode(b.data(), b.size(), frame, error) ==
                    litebird::Vp8Decoder::Status::Ok) {
                    std::vector<uint32_t> pixels;
                    litebird::Vp8Decoder::to_argb(frame, nullptr, 0, pixels);
                }
            } else if (kind == "avif") {
                (void)litebird::AvifContainer::parse(b.data(), b.size());
                (void)litebird::ImageDecoder::decode_bytes(b.data(), b.size(), "image/avif");
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

    if (mode == "--decode-webp") {
        if (argc < 4) return 2;
        auto res = litebird::WebPDecoder::decode_detailed(bytes.data(), bytes.size());
        if (!res.ok()) {
            std::cerr << "WebP error (" << litebird::WebPDecoder::status_string(res.status) << "): " << res.error << "\n";
            return 1;
        }
        if (!write_ppm(argv[3], *res.image)) {
            std::cerr << "Failed to write PPM\n";
            return 2;
        }
        // PPM carries no alpha, and a lossless WebP keeps the colour of fully
        // transparent pixels, so the caller may ask for RGBA as well.
        if (argc >= 5 && !write_rgba(argv[4], *res.image)) {
            std::cerr << "Failed to write RGBA\n";
            return 2;
        }
        std::cout << res.image->width << " " << res.image->height
                  << (res.lossy ? " lossy\n" : " lossless\n");
        return 0;
    }

    if (mode == "--decode-webp-anim") {
        // Every frame of an animation, composited on the canvas, one `.rgba`
        // per frame: `<prefix>_<n>.rgba`. The first line is the canvas size,
        // the frame count, the loop count, the background colour and whether
        // the file is an animation at all or a still image asked for its
        // frames; then one line per frame with its geometry and its methods.
        if (argc < 4) return 2;
        const size_t max_frames = argc >= 5 ? static_cast<size_t>(std::strtoul(argv[4], nullptr, 10))
                                           : litebird::WebPDecoder::kMaxFrames;
        auto anim = litebird::WebPDecoder::decode_frames(bytes.data(), bytes.size(), max_frames);
        if (!anim.ok()) {
            std::cerr << "WebP animation error: " << anim.error << "\n";
            return 1;
        }
        const std::string prefix = argv[3];
        char background[16];
        std::snprintf(background, sizeof(background), "%08x", anim.background);
        std::cout << anim.canvas_width << " " << anim.canvas_height << " " << anim.frames.size() << " "
                  << anim.loop_count << " " << background << " " << (anim.animated ? "animated" : "still")
                  << "\n";
        for (size_t i = 0; i < anim.frames.size(); ++i) {
            const auto& frame = anim.frames[i];
            const std::string out_file = prefix + "_" + std::to_string(i) + ".rgba";
            if (!write_rgba(out_file, *frame.image)) return 2;
            std::cout << frame.duration_ms << " " << frame.x << " " << frame.y << " " << frame.width << " "
                      << frame.height << " " << (frame.blend ? "blend" : "noblend") << " "
                      << (frame.dispose_to_background ? "dispose" : "keep") << "\n";
        }
        return 0;
    }

    if (mode == "--decode-vp8") {
        // The bytes a `VP8 ` chunk carries: the frame tag and everything after
        // it, with no RIFF around it.
        if (argc < 4) return 2;
        litebird::Vp8Decoder::Frame frame;
        std::string error;
        const auto status = litebird::Vp8Decoder::decode(bytes.data(), bytes.size(), frame, error);
        if (status != litebird::Vp8Decoder::Status::Ok) {
            std::cerr << "VP8 error (" << litebird::Vp8Decoder::status_string(status) << "): " << error << "\n";
            return 1;
        }
        std::vector<uint32_t> pixels;
        litebird::Vp8Decoder::to_argb(frame, nullptr, 0, pixels);
        litebird::ImageData image;
        image.width = frame.width;
        image.height = frame.height;
        image.pixels = std::move(pixels);
        if (!write_ppm(argv[3], image)) {
            std::cerr << "Failed to write PPM\n";
            return 2;
        }
        if (argc >= 5 && !write_rgba(argv[4], image)) {
            std::cerr << "Failed to write RGBA\n";
            return 2;
        }
        std::cout << image.width << " " << image.height << "\n";
        return 0;
    }

    if (mode == "--fuzz-vp8") {
        litebird::Vp8Decoder::Frame frame;
        std::string error;
        if (litebird::Vp8Decoder::decode(bytes.data(), bytes.size(), frame, error) ==
            litebird::Vp8Decoder::Status::Ok) {
            std::vector<uint32_t> pixels;
            litebird::Vp8Decoder::to_argb(frame, nullptr, 0, pixels);
        }
        return 0;
    }

    if (mode == "--probe-webp") {
        const auto status = litebird::WebPDecoder::probe(bytes.data(), bytes.size());
        std::cout << litebird::WebPDecoder::status_string(status) << "\n";
        return status == litebird::WebPDecoder::Status::Ok ? 0 : 1;
    }

    if (mode == "--fuzz-webp") {
        (void)litebird::WebPDecoder::decode_detailed(bytes.data(), bytes.size());
        (void)litebird::WebPDecoder::decode_frames(bytes.data(), bytes.size(), 16);
        return 0;
    }

    if (mode == "--probe-avif") {
        const bool is_avif = litebird::AvifContainer::is_avif(bytes.data(), bytes.size());
        const auto file = litebird::AvifContainer::parse(bytes.data(), bytes.size());
        const char* status_name = "Unknown";
        switch (file.status) {
            case litebird::AvifContainer::Status::Ok: status_name = "Ok"; break;
            case litebird::AvifContainer::Status::NotAvif: status_name = "NotAvif"; break;
            case litebird::AvifContainer::Status::Malformed: status_name = "Malformed"; break;
            case litebird::AvifContainer::Status::Unsupported: status_name = "Unsupported"; break;
        }
        std::cout << "status=" << status_name << "\n";
        std::cout << "is_avif=" << (is_avif ? 1 : 0) << "\n";
        std::cout << "major_brand=" << file.major_brand << "\n";
        std::cout << "primary_id=" << file.primary_id << "\n";
        std::cout << "items=" << file.items.size() << "\n";
        if (file.status == litebird::AvifContainer::Status::Ok) {
            const auto& p = file.primary;
            std::cout << "type=" << p.type << "\n";
            std::cout << "ispe=" << (p.has_ispe ? 1 : 0) << " " << p.width << " " << p.height << "\n";
            std::cout << "pixi=" << (p.has_pixi ? 1 : 0);
            if (p.has_pixi) {
                std::cout << " ";
                for (size_t i = 0; i < p.bit_depth.size(); ++i) {
                    if (i > 0) std::cout << ",";
                    std::cout << static_cast<unsigned>(p.bit_depth[i]);
                }
            }
            std::cout << "\n";
            std::cout << "av1c=" << (p.has_av1c ? 1 : 0)
                      << " profile=" << static_cast<unsigned>(p.av1c.seq_profile)
                      << " level=" << static_cast<unsigned>(p.av1c.seq_level_idx_0)
                      << " high_bd=" << (p.av1c.high_bitdepth ? 1 : 0)
                      << " twelve_bit=" << (p.av1c.twelve_bit ? 1 : 0)
                      << " mono=" << (p.av1c.monochrome ? 1 : 0)
                      << " sub_x=" << (p.av1c.subsampling_x ? 1 : 0)
                      << " sub_y=" << (p.av1c.subsampling_y ? 1 : 0)
                      << " csp=" << static_cast<unsigned>(p.av1c.chroma_sample_position)
                      << "\n";
            std::cout << "colr=" << (p.colour.present ? 1 : 0)
                      << " nclx=" << (p.colour.is_nclx ? 1 : 0)
                      << " " << p.colour.primaries
                      << " " << p.colour.transfer
                      << " " << p.colour.matrix
                      << " " << (p.colour.full_range ? 1 : 0) << "\n";
            std::cout << "payload_bytes=" << p.payload.size() << "\n";
            if (argc >= 4) {
                std::ofstream ofs(argv[3], std::ios::binary);
                if (!ofs) {
                    std::cerr << "Failed to write AV1 payload\n";
                    return 2;
                }
                if (!p.payload.empty()) {
                    ofs.write(reinterpret_cast<const char*>(p.payload.data()),
                              static_cast<std::streamsize>(p.payload.size()));
                }
                if (!ofs.good()) return 2;
            }
            return 0;
        }
        std::cout << "error=" << file.error << "\n";
        return 1;
    }

    if (mode == "--decode-avif") {
        if (argc < 4) return 2;
        const auto file = litebird::AvifContainer::parse(bytes.data(), bytes.size());
        if (file.status != litebird::AvifContainer::Status::Ok) {
            std::cerr << "AVIF decode error (" << litebird::AvifContainer::status_string(file.status)
                      << "): " << file.error << "\n";
            return 1;
        }
        auto img = litebird::ImageDecoder::decode_bytes(bytes.data(), bytes.size(), "image/avif");
        if (!img || !img->is_valid()) {
            std::cerr << "AVIF decode error (no_av1_decoder): container Ok ("
                      << file.primary.width << "x" << file.primary.height
                      << ", " << file.primary.payload.size() << " AV1 bytes) but ImageDecoder returned null\n";
            return 3;
        }
        if (!write_ppm(argv[3], *img)) {
            std::cerr << "Failed to write PPM\n";
            return 2;
        }
        if (argc >= 5 && !write_rgba(argv[4], *img)) {
            std::cerr << "Failed to write RGBA\n";
            return 2;
        }
        std::cout << img->width << " " << img->height << "\n";
        return 0;
    }

    if (mode == "--fuzz-avif") {
        (void)litebird::AvifContainer::parse(bytes.data(), bytes.size());
        (void)litebird::ImageDecoder::decode_bytes(bytes.data(), bytes.size(), "image/avif");
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
