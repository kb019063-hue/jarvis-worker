#pragma once

#include "litebird/common/Color.hpp"
#include "litebird/render/RasterBudget.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace litebird {

struct ImageData {
    int width{0};
    int height{0};
    std::vector<uint32_t> pixels; // Format: 0xAARRGGBB
    /// What this bitmap holds of the budget for canvas and SVG bitmaps
    /// (RasterBudget.hpp). Nothing, for an image decoded from a file.
    RasterReservation budget;

    bool is_valid() const {
        return width > 0 && height > 0 && pixels.size() >= static_cast<size_t>(width) * static_cast<size_t>(height);
    }

    uint32_t get_pixel(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return 0;
        return pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
    }

    Color get_pixel_color(int x, int y) const {
        return Color::from_argb(get_pixel(x, y));
    }

    void set_pixel(int x, int y, uint32_t argb) {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = argb;
        }
    }
};

class ImageDecoder {
public:
    static std::shared_ptr<ImageData> decode(const std::vector<uint8_t>& data, std::string_view content_type = {});
    static std::shared_ptr<ImageData> decode_bytes(const uint8_t* data, size_t size, std::string_view content_type = {});
    static std::shared_ptr<ImageData> decode_file(const std::string& path);
    static std::shared_ptr<ImageData> decode_data_uri(std::string_view data_uri);

    // Decoders for formats
    static std::shared_ptr<ImageData> decode_png(const uint8_t* data, size_t size);
    static std::shared_ptr<ImageData> decode_bmp(const uint8_t* data, size_t size);
    static std::shared_ptr<ImageData> decode_ppm(const uint8_t* data, size_t size);
    static std::shared_ptr<ImageData> decode_svg(std::string_view bytes, float hint_w = -1.0f, float hint_h = -1.0f);

    // Lossless PNG encoding
    static bool encode_png(const ImageData& image, std::vector<uint8_t>& out);
    static std::vector<uint8_t> encode_png(const ImageData& image) {
        std::vector<uint8_t> out;
        encode_png(image, out);
        return out;
    }
    static bool save_png(const ImageData& image, const std::string& path);

    // Test generator
    static std::shared_ptr<ImageData> create_test_pattern(int w, int h, Color color);
};

class Element;

/// The decoded pixels of an `<img>`, fetched under the document's own request
/// context (CORS when the element has a `crossorigin` attribute) and cached
/// per document. Null when the element has no source, the fetch is refused, or
/// the bytes do not decode. Declared here because `drawImage` needs the same
/// pixels the layout pass paints, and it must not decode them a second time
/// under different rules.
std::shared_ptr<ImageData> load_image_for_element(const Element& element);

} // namespace litebird
