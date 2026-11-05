#include "litebird/render/ImageDecoder.hpp"
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cctype>

namespace litebird {

static uint16_t read_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

static int32_t read_i32_le(const uint8_t* p) {
    return static_cast<int32_t>(read_u32_le(p));
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
    int height = std::abs(h);
    int width = w;

    if (offset >= size) return nullptr;

    auto img = std::make_shared<ImageData>();
    img->width = width;
    img->height = height;
    img->pixels.resize(width * height, 0xFF000000);

    int row_stride = (bpp == 24) ? ((width * 3 + 3) & ~3) : (width * 4);

    for (int y = 0; y < height; ++y) {
        int src_y = top_down ? y : (height - 1 - y);
        size_t row_offset = offset + static_cast<size_t>(src_y) * row_stride;
        if (row_offset + (bpp == 24 ? width * 3 : width * 4) > size) break;

        const uint8_t* row_ptr = data + row_offset;
        for (int x = 0; x < width; ++x) {
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
        while (cursor < size && std::isdigit(data[cursor])) {
            val = val * 10 + (data[cursor] - '0');
            cursor++;
        }
        return val;
    };

    int width = read_int();
    int height = read_int();
    int maxval = read_int();

    if (width <= 0 || height <= 0 || maxval <= 0 || maxval > 255) return nullptr;

    if (is_p6 && cursor < size && std::isspace(data[cursor])) {
        cursor++; // skip single separator
    }

    auto img = std::make_shared<ImageData>();
    img->width = width;
    img->height = height;
    img->pixels.resize(width * height, 0xFF000000);

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

static const std::string BASE64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::vector<uint8_t> base64_decode(std::string_view encoded) {
    std::vector<uint8_t> result;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[static_cast<unsigned char>(BASE64_CHARS[i])] = i;

    int val = 0, valb = -8;
    for (char c : encoded) {
        if (T[static_cast<unsigned char>(c)] == -1) {
            if (c == '=') break;
            continue;
        }
        val = (val << 6) + T[static_cast<unsigned char>(c)];
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

std::shared_ptr<ImageData> ImageDecoder::decode_data_uri(std::string_view data_uri) {
    if (!data_uri.starts_with("data:image/")) return nullptr;
    size_t comma = data_uri.find(',');
    if (comma == std::string_view::npos) return nullptr;

    std::string_view meta = data_uri.substr(0, comma);
    std::string_view body = data_uri.substr(comma + 1);

    if (meta.find(";base64") != std::string_view::npos) {
        auto raw = base64_decode(body);
        return decode(raw);
    }
    return nullptr;
}

std::shared_ptr<ImageData> ImageDecoder::decode(const std::vector<uint8_t>& data) {
    if (data.size() < 4) return nullptr;
    if (data[0] == 'B' && data[1] == 'M') {
        return decode_bmp(data.data(), data.size());
    }
    if (data[0] == 'P' && (data[1] == '6' || data[1] == '3')) {
        return decode_ppm(data.data(), data.size());
    }
    return nullptr;
}

std::shared_ptr<ImageData> ImageDecoder::decode_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return nullptr;

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
    return decode(buffer);
}

std::shared_ptr<ImageData> ImageDecoder::create_test_pattern(int w, int h, Color color) {
    auto img = std::make_shared<ImageData>();
    img->width = w;
    img->height = h;
    img->pixels.resize(w * h);

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
