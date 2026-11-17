#include "litebird/render/ImageDecoder.hpp"
#include "litebird/net/DataUrl.hpp"
#include "litebird/net/Deflate.hpp"
#include "litebird/render/GifDecoder.hpp"
#include "litebird/render/WebPDecoder.hpp"
#include "litebird/render/JpegDecoder.hpp"
#include "litebird/render/SvgRenderer.hpp"
#include "litebird/xml/XmlDom.hpp"
#include "litebird/xml/XmlParser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>

namespace litebird {

namespace {

uint16_t read_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint16_t read_u16_be(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t read_u32_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

int32_t read_i32_le(const uint8_t* p) {
    return static_cast<int32_t>(read_u32_le(p));
}

void append_u32_be(std::vector<uint8_t>& out, uint32_t val) {
    out.push_back(static_cast<uint8_t>((val >> 24) & 0xFFu));
    out.push_back(static_cast<uint8_t>((val >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((val >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(val & 0xFFu));
}

void append_png_chunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* payload, size_t len) {
    append_u32_be(out, static_cast<uint32_t>(len));
    const size_t crc_start = out.size();
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(type), reinterpret_cast<const uint8_t*>(type) + 4);
    if (len > 0 && payload) {
        out.insert(out.end(), payload, payload + len);
    }
    const uint32_t crc = Deflate::crc32(out.data() + crc_start, out.size() - crc_start);
    append_u32_be(out, crc);
}

// Hard ceiling on decoded pixel count. A hostile header can claim 65535 x 65536
// pixels in 154 bytes; without a cap the decoder either overflows `int` in
// `width * height` or tries to allocate terabytes. 64 megapixels keeps the
// product provably inside both `int` and `size_t`.
constexpr int64_t kMaxDecodedPixels = 64ll * 1024 * 1024;
constexpr size_t kMaxCompressedPngBytes = 32u * 1024u * 1024u;

bool validate_pixel_count(int64_t width, int64_t height) {
    if (width <= 0 || height <= 0) return false;
    if (width > INT32_MAX || height > INT32_MAX) return false;
    if (width > kMaxDecodedPixels / height) return false;
    return width * height <= kMaxDecodedPixels;
}

uint8_t paeth_predictor(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    if (pb <= pc) return static_cast<uint8_t>(b);
    return static_cast<uint8_t>(c);
}

bool unfilter_scanline(
    uint8_t filter_type,
    uint8_t* cur,
    const uint8_t* prev,
    size_t row_bytes,
    size_t bpp
) {
    switch (filter_type) {
        case 0: // None
            return true;
        case 1: // Sub
            for (size_t i = bpp; i < row_bytes; ++i) {
                cur[i] = static_cast<uint8_t>(cur[i] + cur[i - bpp]);
            }
            return true;
        case 2: // Up
            if (prev) {
                for (size_t i = 0; i < row_bytes; ++i) {
                    cur[i] = static_cast<uint8_t>(cur[i] + prev[i]);
                }
            }
            return true;
        case 3: // Average
            for (size_t i = 0; i < row_bytes; ++i) {
                const int a = (i >= bpp) ? cur[i - bpp] : 0;
                const int b = prev ? prev[i] : 0;
                cur[i] = static_cast<uint8_t>(cur[i] + ((a + b) >> 1));
            }
            return true;
        case 4: // Paeth
            for (size_t i = 0; i < row_bytes; ++i) {
                const int a = (i >= bpp) ? cur[i - bpp] : 0;
                const int b = prev ? prev[i] : 0;
                const int c = (prev && i >= bpp) ? prev[i - bpp] : 0;
                cur[i] = static_cast<uint8_t>(cur[i] + paeth_predictor(a, b, c));
            }
            return true;
        default:
            return false;
    }
}

uint16_t read_subbyte_sample(const uint8_t* row, size_t sample_idx, int bit_depth) {
    if (bit_depth == 8) return row[sample_idx];
    if (bit_depth == 16) return read_u16_be(row + sample_idx * 2);
    const size_t samples_per_byte = static_cast<size_t>(8 / bit_depth);
    const size_t byte_idx = sample_idx / samples_per_byte;
    const size_t slot = samples_per_byte - 1 - (sample_idx % samples_per_byte);
    const int shift = static_cast<int>(slot) * bit_depth;
    const uint8_t mask = static_cast<uint8_t>((1u << bit_depth) - 1u);
    return static_cast<uint16_t>((row[byte_idx] >> shift) & mask);
}

uint8_t scale_sample_to_u8(uint16_t raw, int bit_depth) {
    switch (bit_depth) {
        case 1: return raw ? 255u : 0u;
        case 2: return static_cast<uint8_t>(raw * 85u);
        case 4: return static_cast<uint8_t>(raw * 17u);
        case 8: return static_cast<uint8_t>(raw);
        case 16: return static_cast<uint8_t>(raw >> 8);
        default: return 0u;
    }
}

} // namespace

std::shared_ptr<ImageData> ImageDecoder::decode_png(const uint8_t* data, size_t size) {
    static constexpr uint8_t kPngSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (!data || size < 33) return nullptr;
    if (std::memcmp(data, kPngSig, 8) != 0) return nullptr;

    size_t pos = 8;
    bool seen_ihdr = false;
    bool seen_iend = false;
    uint32_t width = 0;
    uint32_t height = 0;
    int bit_depth = 0;
    int color_type = 0;
    int interlace = 0;

    std::vector<std::array<uint8_t, 4>> palette;
    bool has_trns_key = false;
    uint16_t trns_r = 0, trns_g = 0, trns_b = 0;
    std::vector<uint8_t> compressed_idat;

    while (pos + 12 <= size) {
        const uint32_t chunk_len = read_u32_be(data + pos);
        if (chunk_len > size - pos - 12) return nullptr;
        const uint8_t* type_ptr = data + pos + 4;
        const uint8_t* chunk_data = data + pos + 8;
        const uint32_t expected_crc = read_u32_be(data + pos + 8 + chunk_len);
        const uint32_t actual_crc = Deflate::crc32(type_ptr, 4 + static_cast<size_t>(chunk_len));
        if (expected_crc != actual_crc) return nullptr;

        const std::string_view type(reinterpret_cast<const char*>(type_ptr), 4);
        pos += 12 + static_cast<size_t>(chunk_len);

        if (!seen_ihdr) {
            if (type != "IHDR" || chunk_len != 13) return nullptr;
            width = read_u32_be(chunk_data);
            height = read_u32_be(chunk_data + 4);
            bit_depth = chunk_data[8];
            color_type = chunk_data[9];
            const int compression = chunk_data[10];
            const int filter = chunk_data[11];
            interlace = chunk_data[12];

            if (!validate_pixel_count(static_cast<int64_t>(width), static_cast<int64_t>(height))) return nullptr;
            if (compression != 0 || filter != 0 || (interlace != 0 && interlace != 1)) return nullptr;

            bool valid_depth = false;
            if (color_type == 0) valid_depth = (bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8 || bit_depth == 16);
            else if (color_type == 2 || color_type == 4 || color_type == 6) valid_depth = (bit_depth == 8 || bit_depth == 16);
            else if (color_type == 3) valid_depth = (bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8);
            if (!valid_depth) return nullptr;

            seen_ihdr = true;
        } else if (type == "IHDR") {
            return nullptr;
        } else if (type == "PLTE") {
            if (chunk_len == 0 || chunk_len % 3 != 0 || chunk_len > 256 * 3) return nullptr;
            const size_t entries = chunk_len / 3;
            palette.resize(entries);
            for (size_t i = 0; i < entries; ++i) {
                palette[i] = {chunk_data[i * 3], chunk_data[i * 3 + 1], chunk_data[i * 3 + 2], 255u};
            }
        } else if (type == "tRNS") {
            if (color_type == 3) {
                for (size_t i = 0; i < chunk_len && i < palette.size(); ++i) {
                    palette[i][3] = chunk_data[i];
                }
            } else if (color_type == 0 && chunk_len >= 2) {
                has_trns_key = true;
                trns_g = read_u16_be(chunk_data);
            } else if (color_type == 2 && chunk_len >= 6) {
                has_trns_key = true;
                trns_r = read_u16_be(chunk_data);
                trns_g = read_u16_be(chunk_data + 2);
                trns_b = read_u16_be(chunk_data + 4);
            }
        } else if (type == "IDAT") {
            if (chunk_len > kMaxCompressedPngBytes - compressed_idat.size()) return nullptr;
            compressed_idat.insert(compressed_idat.end(), chunk_data, chunk_data + chunk_len);
        } else if (type == "IEND") {
            seen_iend = true;
            break;
        }
    }

    if (!seen_ihdr || !seen_iend || compressed_idat.empty()) return nullptr;
    if (color_type == 3 && palette.empty()) return nullptr;

    int channels = 1;
    if (color_type == 2) channels = 3;
    else if (color_type == 4) channels = 2;
    else if (color_type == 6) channels = 4;

    const size_t bits_per_pixel = static_cast<size_t>(channels) * static_cast<size_t>(bit_depth);
    const size_t filter_bpp = std::max<size_t>(1, (bits_per_pixel + 7) / 8);

    auto row_byte_len = [&](uint32_t pass_w) -> size_t {
        const uint64_t total_bits = static_cast<uint64_t>(pass_w) * static_cast<uint64_t>(bits_per_pixel);
        return static_cast<size_t>((total_bits + 7u) / 8u);
    };

    struct PassGeom {
        uint32_t x0, y0, dx, dy;
    };
    static constexpr std::array<PassGeom, 7> kAdam7 = {{
        {0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
        {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}
    }};
    static constexpr std::array<PassGeom, 1> kSinglePass = {{{0, 0, 1, 1}}};

    const size_t num_passes = (interlace == 1) ? 7 : 1;
    uint64_t expected_raw_bytes = 0;
    for (size_t p = 0; p < num_passes; ++p) {
        const PassGeom g = (interlace == 1) ? kAdam7[p] : kSinglePass[0];
        if (width <= g.x0 || height <= g.y0) continue;
        const uint32_t pw = (width - g.x0 + g.dx - 1) / g.dx;
        const uint32_t ph = (height - g.y0 + g.dy - 1) / g.dy;
        expected_raw_bytes += static_cast<uint64_t>(ph) * (1u + static_cast<uint64_t>(row_byte_len(pw)));
        if (expected_raw_bytes > 512ull * 1024ull * 1024ull) return nullptr;
    }

    std::vector<uint8_t> raw_scanlines;
    raw_scanlines.reserve(static_cast<size_t>(expected_raw_bytes));
    if (!Deflate::inflate_zlib(compressed_idat.data(), compressed_idat.size(),
                               raw_scanlines, static_cast<size_t>(expected_raw_bytes))) {
        return nullptr;
    }
    if (raw_scanlines.size() != static_cast<size_t>(expected_raw_bytes)) return nullptr;

    auto img = std::make_shared<ImageData>();
    img->width = static_cast<int>(width);
    img->height = static_cast<int>(height);
    img->pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height), 0xFF000000u);

    size_t raw_cursor = 0;
    for (size_t p = 0; p < num_passes; ++p) {
        const PassGeom g = (interlace == 1) ? kAdam7[p] : kSinglePass[0];
        if (width <= g.x0 || height <= g.y0) continue;
        const uint32_t pw = (width - g.x0 + g.dx - 1) / g.dx;
        const uint32_t ph = (height - g.y0 + g.dy - 1) / g.dy;
        const size_t rbytes = row_byte_len(pw);

        std::vector<uint8_t> prev_row(rbytes, 0);
        std::vector<uint8_t> cur_row(rbytes, 0);

        for (uint32_t py = 0; py < ph; ++py) {
            if (raw_cursor + 1 + rbytes > raw_scanlines.size()) return nullptr;
            const uint8_t filter_type = raw_scanlines[raw_cursor++];
            std::memcpy(cur_row.data(), raw_scanlines.data() + raw_cursor, rbytes);
            raw_cursor += rbytes;

            if (!unfilter_scanline(filter_type, cur_row.data(), py == 0 ? nullptr : prev_row.data(), rbytes, filter_bpp)) {
                return nullptr;
            }

            const int dst_y = static_cast<int>(g.y0 + py * g.dy);
            for (uint32_t px = 0; px < pw; ++px) {
                const int dst_x = static_cast<int>(g.x0 + px * g.dx);
                uint8_t r = 0, gr = 0, b = 0, a = 255;

                if (color_type == 0) {
                    const uint16_t v_raw = read_subbyte_sample(cur_row.data(), px, bit_depth);
                    const uint8_t v = scale_sample_to_u8(v_raw, bit_depth);
                    r = gr = b = v;
                    if (has_trns_key && v_raw == trns_g) a = 0;
                } else if (color_type == 2) {
                    const uint16_t rr = read_subbyte_sample(cur_row.data(), static_cast<size_t>(px) * 3 + 0, bit_depth);
                    const uint16_t gg = read_subbyte_sample(cur_row.data(), static_cast<size_t>(px) * 3 + 1, bit_depth);
                    const uint16_t bb = read_subbyte_sample(cur_row.data(), static_cast<size_t>(px) * 3 + 2, bit_depth);
                    r = scale_sample_to_u8(rr, bit_depth);
                    gr = scale_sample_to_u8(gg, bit_depth);
                    b = scale_sample_to_u8(bb, bit_depth);
                    if (has_trns_key && rr == trns_r && gg == trns_g && bb == trns_b) a = 0;
                } else if (color_type == 3) {
                    const uint16_t idx = read_subbyte_sample(cur_row.data(), px, bit_depth);
                    if (idx >= palette.size()) return nullptr;
                    r = palette[idx][0];
                    gr = palette[idx][1];
                    b = palette[idx][2];
                    a = palette[idx][3];
                } else if (color_type == 4) {
                    const uint16_t vv = read_subbyte_sample(cur_row.data(), static_cast<size_t>(px) * 2 + 0, bit_depth);
                    const uint16_t aa = read_subbyte_sample(cur_row.data(), static_cast<size_t>(px) * 2 + 1, bit_depth);
                    r = gr = b = scale_sample_to_u8(vv, bit_depth);
                    a = scale_sample_to_u8(aa, bit_depth);
                } else if (color_type == 6) {
                    const uint16_t rr = read_subbyte_sample(cur_row.data(), static_cast<size_t>(px) * 4 + 0, bit_depth);
                    const uint16_t gg = read_subbyte_sample(cur_row.data(), static_cast<size_t>(px) * 4 + 1, bit_depth);
                    const uint16_t bb = read_subbyte_sample(cur_row.data(), static_cast<size_t>(px) * 4 + 2, bit_depth);
                    const uint16_t aa = read_subbyte_sample(cur_row.data(), static_cast<size_t>(px) * 4 + 3, bit_depth);
                    r = scale_sample_to_u8(rr, bit_depth);
                    gr = scale_sample_to_u8(gg, bit_depth);
                    b = scale_sample_to_u8(bb, bit_depth);
                    a = scale_sample_to_u8(aa, bit_depth);
                }

                const uint32_t argb = (static_cast<uint32_t>(a) << 24) |
                                      (static_cast<uint32_t>(r) << 16) |
                                      (static_cast<uint32_t>(gr) << 8) |
                                      static_cast<uint32_t>(b);
                img->set_pixel(dst_x, dst_y, argb);
            }

            prev_row.swap(cur_row);
        }
    }

    return img;
}

bool ImageDecoder::encode_png(const ImageData& image, std::vector<uint8_t>& out) {
    if (!image.is_valid()) return false;
    if (!validate_pixel_count(image.width, image.height)) return false;

    static constexpr uint8_t kPngSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    out.clear();
    out.insert(out.end(), kPngSig, kPngSig + 8);

    uint8_t ihdr[13]{};
    ihdr[0] = static_cast<uint8_t>((static_cast<uint32_t>(image.width) >> 24) & 0xFFu);
    ihdr[1] = static_cast<uint8_t>((static_cast<uint32_t>(image.width) >> 16) & 0xFFu);
    ihdr[2] = static_cast<uint8_t>((static_cast<uint32_t>(image.width) >> 8) & 0xFFu);
    ihdr[3] = static_cast<uint8_t>(static_cast<uint32_t>(image.width) & 0xFFu);
    ihdr[4] = static_cast<uint8_t>((static_cast<uint32_t>(image.height) >> 24) & 0xFFu);
    ihdr[5] = static_cast<uint8_t>((static_cast<uint32_t>(image.height) >> 16) & 0xFFu);
    ihdr[6] = static_cast<uint8_t>((static_cast<uint32_t>(image.height) >> 8) & 0xFFu);
    ihdr[7] = static_cast<uint8_t>(static_cast<uint32_t>(image.height) & 0xFFu);
    ihdr[8] = 8; // 8-bit
    ihdr[9] = 6; // RGBA
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    append_png_chunk(out, "IHDR", ihdr, sizeof(ihdr));

    const size_t stride = 1u + static_cast<size_t>(image.width) * 4u;
    std::vector<uint8_t> raw(static_cast<size_t>(image.height) * stride, 0);
    for (int y = 0; y < image.height; ++y) {
        uint8_t* row = raw.data() + static_cast<size_t>(y) * stride;
        row[0] = 0; // Filter None
        for (int x = 0; x < image.width; ++x) {
            const uint32_t argb = image.get_pixel(x, y);
            row[1 + static_cast<size_t>(x) * 4 + 0] = static_cast<uint8_t>((argb >> 16) & 0xFFu);
            row[1 + static_cast<size_t>(x) * 4 + 1] = static_cast<uint8_t>((argb >> 8) & 0xFFu);
            row[1 + static_cast<size_t>(x) * 4 + 2] = static_cast<uint8_t>(argb & 0xFFu);
            row[1 + static_cast<size_t>(x) * 4 + 3] = static_cast<uint8_t>((argb >> 24) & 0xFFu);
        }
    }

    std::vector<uint8_t> zstream;
    Deflate::compress_zlib_stored(raw.data(), raw.size(), zstream);
    append_png_chunk(out, "IDAT", zstream.data(), zstream.size());
    append_png_chunk(out, "IEND", nullptr, 0);
    return true;
}

bool ImageDecoder::save_png(const ImageData& image, const std::string& path) {
    std::vector<uint8_t> bytes;
    if (!encode_png(image, bytes)) return false;
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

std::shared_ptr<ImageData> ImageDecoder::decode_svg(std::string_view bytes, float hint_w, float hint_h) {
    std::string utf8;
    std::string encoding;
    xml::XmlError error;
    if (!xml::decode_xml(bytes, utf8, encoding, error)) return nullptr;

    auto doc = std::make_shared<Document>();
    doc->set_content_type("image/svg+xml");
    if (!xml::parse_xml_document(doc, utf8, error)) return nullptr;

    auto root = doc->document_element();
    if (!root || root->tag_name() != "svg") return nullptr;

    float w = hint_w;
    float h = hint_h;
    if (w <= 0.0f && root->has_attribute("width")) {
        try { w = std::stof(root->get_attribute("width")); } catch (...) {}
    }
    if (h <= 0.0f && root->has_attribute("height")) {
        try { h = std::stof(root->get_attribute("height")); } catch (...) {}
    }
    if ((w <= 0.0f || h <= 0.0f) && root->has_attribute("viewBox")) {
        std::string vb = root->get_attribute("viewBox");
        for (char& c : vb) if (c == ',') c = ' ';
        std::stringstream ss(vb);
        float mx = 0, my = 0, vw = 0, vh = 0;
        if (ss >> mx >> my >> vw >> vh) {
            if (w <= 0.0f && vw > 0.0f) w = vw;
            if (h <= 0.0f && vh > 0.0f) h = vh;
        }
    }
    if (w <= 0.0f) w = 150.0f;
    if (h <= 0.0f) h = 150.0f;
    return SvgRenderer::render(root, w, h);
}

std::shared_ptr<ImageData> ImageDecoder::decode_bmp(const uint8_t* data, size_t size) {
    if (!data || size < 54) return nullptr;
    if (data[0] != 'B' || data[1] != 'M') return nullptr;

    uint32_t offset = read_u32_le(data + 10);
    int32_t w = read_i32_le(data + 18);
    int32_t h = read_i32_le(data + 22);
    uint16_t bpp = read_u16_le(data + 28);
    uint32_t compression = read_u32_le(data + 30);

    if (w <= 0 || h == 0 || (bpp != 24 && bpp != 32) || compression != 0) {
        return nullptr;
    }

    bool top_down = (h < 0);
    if (h == INT32_MIN) return nullptr;
    int64_t height = top_down ? -static_cast<int64_t>(h) : static_cast<int64_t>(h);
    int64_t width = w;

    if (offset >= size) return nullptr;
    if (!validate_pixel_count(width, height)) return nullptr;
    if (width * (bpp == 24 ? 3 : 4) + 4 > INT32_MAX) return nullptr;

    auto img = std::make_shared<ImageData>();
    img->width = static_cast<int>(width);
    img->height = static_cast<int>(height);
    img->pixels.resize(static_cast<size_t>(width * height), 0xFF000000);

    int row_stride = (bpp == 24) ? static_cast<int>((width * 3 + 3) & ~static_cast<int64_t>(3))
                                 : static_cast<int>(width * 4);

    for (int y = 0; y < static_cast<int>(height); ++y) {
        int src_y = top_down ? y : (static_cast<int>(height) - 1 - y);
        size_t row_offset = offset + static_cast<size_t>(src_y) * static_cast<size_t>(row_stride);
        size_t row_bytes = (bpp == 24) ? static_cast<size_t>(width) * 3 : static_cast<size_t>(width) * 4;
        if (row_offset > size || row_bytes > size - row_offset) break;

        const uint8_t* row_ptr = data + row_offset;
        for (int x = 0; x < static_cast<int>(width); ++x) {
            uint8_t b = row_ptr[0];
            uint8_t g = row_ptr[1];
            uint8_t r = row_ptr[2];
            uint8_t a = (bpp == 32) ? row_ptr[3] : 255;
            row_ptr += (bpp == 24 ? 3 : 4);

            uint32_t argb = (static_cast<uint32_t>(a) << 24) |
                            (static_cast<uint32_t>(r) << 16) |
                            (static_cast<uint32_t>(g) << 8) |
                            static_cast<uint32_t>(b);
            img->set_pixel(x, y, argb);
        }
    }

    return img;
}

std::shared_ptr<ImageData> ImageDecoder::decode_ppm(const uint8_t* data, size_t size) {
    if (!data || size < 8) return nullptr;
    if (data[0] != 'P') return nullptr;

    bool is_p6 = (data[1] == '6');
    bool is_p3 = (data[1] == '3');
    if (!is_p6 && !is_p3) return nullptr;

    size_t cursor = 2;
    auto skip_ws_and_comments = [&]() {
        while (cursor < size) {
            if (std::isspace(data[cursor])) {
                cursor++;
            } else if (data[cursor] == '#') {
                while (cursor < size && data[cursor] != '\n') cursor++;
            } else {
                break;
            }
        }
    };

    auto read_int = [&]() -> int {
        skip_ws_and_comments();
        if (cursor >= size || !std::isdigit(data[cursor])) return -1;
        int val = 0;
        bool overflow = false;
        while (cursor < size && std::isdigit(data[cursor])) {
            const int digit = data[cursor] - '0';
            if (val > (INT_MAX - digit) / 10) {
                overflow = true;
            } else if (!overflow) {
                val = val * 10 + digit;
            }
            cursor++;
        }
        return overflow ? -1 : val;
    };

    int width = read_int();
    int height = read_int();
    int maxval = read_int();

    if (width <= 0 || height <= 0 || maxval <= 0 || maxval > 255) return nullptr;
    if (!validate_pixel_count(width, height)) return nullptr;

    if (is_p6 && cursor < size && std::isspace(data[cursor])) {
        cursor++;
    }

    auto img = std::make_shared<ImageData>();
    img->width = width;
    img->height = height;
    img->pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height), 0xFF000000);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t r = 0, g = 0, b = 0;
            if (is_p6) {
                if (cursor + 3 > size) return img;
                r = data[cursor++];
                g = data[cursor++];
                b = data[cursor++];
            } else {
                int ri = read_int();
                int gi = read_int();
                int bi = read_int();
                r = static_cast<uint8_t>(std::clamp(ri, 0, 255));
                g = static_cast<uint8_t>(std::clamp(gi, 0, 255));
                b = static_cast<uint8_t>(std::clamp(bi, 0, 255));
            }
            uint32_t argb = (0xFFu << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
            img->set_pixel(x, y, argb);
        }
    }

    return img;
}

std::shared_ptr<ImageData> ImageDecoder::decode_data_uri(std::string_view data_uri) {
    std::string_view trimmed = data_uri;
    if (const auto hash = trimmed.find('#'); hash != std::string_view::npos) {
        trimmed = trimmed.substr(0, hash);
    }
    MimeType mime;
    std::string body;
    if (!process_data_url(trimmed, mime, body)) return nullptr;
    return decode_bytes(reinterpret_cast<const uint8_t*>(body.data()), body.size(), mime.essence());
}

std::shared_ptr<ImageData> ImageDecoder::decode_bytes(
    const uint8_t* data,
    size_t size,
    std::string_view content_type
) {
    if (!data || size == 0) return nullptr;
    if (size >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        return decode_png(data, size);
    }
    if (JpegDecoder::is_jpeg(data, size)) {
        return JpegDecoder::decode(data, size);
    }
    if (GifDecoder::is_gif(data, size)) {
        return GifDecoder::decode(data, size);
    }
    if (WebPDecoder::is_webp(data, size)) {
        return WebPDecoder::decode(data, size);
    }
    if (size >= 4 && data[0] == 'B' && data[1] == 'M') {
        return decode_bmp(data, size);
    }
    if (size >= 4 && data[0] == 'P' && (data[1] == '6' || data[1] == '3')) {
        return decode_ppm(data, size);
    }

    MimeType mime;
    const bool is_svg_mime = MimeType::parse(content_type, mime) && mime.essence() == "image/svg+xml";
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    size_t skip = 0;
    while (skip < sv.size() && std::isspace(static_cast<unsigned char>(sv[skip]))) ++skip;
    const bool looks_like_svg = sv.substr(skip).starts_with("<svg") || sv.substr(skip).starts_with("<?xml");
    if (is_svg_mime || looks_like_svg) {
        return decode_svg(sv);
    }
    return nullptr;
}

std::shared_ptr<ImageData> ImageDecoder::decode(const std::vector<uint8_t>& data, std::string_view content_type) {
    return decode_bytes(data.data(), data.size(), content_type);
}

std::shared_ptr<ImageData> ImageDecoder::decode_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return nullptr;

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
    return decode(buffer);
}

std::shared_ptr<ImageData> ImageDecoder::create_test_pattern(int w, int h, Color color) {
    if (!validate_pixel_count(w, h)) return nullptr;
    auto img = std::make_shared<ImageData>();
    img->width = w;
    img->height = h;
    img->pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h));

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool check = ((x / 8) + (y / 8)) % 2 == 0;
            uint32_t argb = check ? color.to_argb() : Color(240, 240, 240).to_argb();
            img->set_pixel(x, y, argb);
        }
    }
    return img;
}

} // namespace litebird
