#pragma once

#include "litebird/render/ImageDecoder.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace litebird {

struct JpegDecodeResult {
    std::shared_ptr<ImageData> image;
    int width{0};
    int height{0};
    int components{0};
    bool progressive{false};
    std::string error;

    bool ok() const { return image != nullptr && image->is_valid() && error.empty(); }
};

/// Zero-dependency ITU-T T.81 / JFIF / Adobe JPEG decoder.
/// Supports Baseline DCT (SOF0), Extended Sequential DCT (SOF1), and
/// Progressive DCT (SOF2), including Grayscale, YCbCr (4:4:4, 4:2:2, 4:2:0,
/// 4:4:0, arbitrary sampling factors), RGB, CMYK, and YCCK with restart
/// markers (DRI/RST0-RST7).
class JpegDecoder {
public:
    /// Hard pixel ceiling matching ImageDecoder (64 megapixels).
    static constexpr int64_t kMaxDecodedPixels = 64ll * 1024 * 1024;
    /// Hard ceiling on progressive DCT coefficient storage (128 MiB).
    static constexpr size_t kMaxProgressiveBytes = 128u * 1024u * 1024u;
    /// Hard ceiling on number of SOS scans in a single file.
    static constexpr int kMaxScans = 256;

    /// Returns true if the buffer starts with the JPEG SOI marker (FF D8).
    static bool is_jpeg(const uint8_t* data, size_t size);

    /// Decodes a JPEG byte buffer and returns detailed status + ImageData.
    static JpegDecodeResult decode_detailed(const uint8_t* data, size_t size);

    /// Decodes a JPEG byte buffer into 0xAARRGGBB ImageData, or nullptr on error.
    static std::shared_ptr<ImageData> decode(const uint8_t* data, size_t size);
    static std::shared_ptr<ImageData> decode(const std::vector<uint8_t>& data);
};

} // namespace litebird
