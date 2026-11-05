#pragma once

#include "litebird/common/Color.hpp"
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

namespace litebird {

struct ImageData {
    int width{0};
    int height{0};
    std::vector<uint32_t> pixels; // Format: 0xAARRGGBB

    bool is_valid() const {
        return width > 0 && height > 0 && pixels.size() >= static_cast<size_t>(width * height);
    }

    uint32_t get_pixel(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return 0;
        return pixels[y * width + x];
    }

    Color get_pixel_color(int x, int y) const {
        return Color::from_argb(get_pixel(x, y));
    }

    void set_pixel(int x, int y, uint32_t argb) {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            pixels[y * width + x] = argb;
        }
    }
};

class ImageDecoder {
public:
    static std::shared_ptr<ImageData> decode(const std::vector<uint8_t>& data);
    static std::shared_ptr<ImageData> decode_file(const std::string& path);
    static std::shared_ptr<ImageData> decode_data_uri(std::string_view data_uri);

    // Decoders for formats
    static std::shared_ptr<ImageData> decode_bmp(const uint8_t* data, size_t size);
    static std::shared_ptr<ImageData> decode_ppm(const uint8_t* data, size_t size);

    // Test generator
    static std::shared_ptr<ImageData> create_test_pattern(int w, int h, Color color);
};

} // namespace litebird
