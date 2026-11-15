#pragma once

#include "litebird/render/ImageDecoder.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace litebird {

enum class GifDisposal : uint8_t {
    Unspecified = 0,
    DoNotDispose = 1,
    RestoreToBackground = 2,
    RestoreToPrevious = 3,
};

struct GifFrame {
    std::shared_ptr<ImageData> image; // Full composited canvas (0xAARRGGBB)
    int delay_ms{0};                  // Frame duration in milliseconds
    GifDisposal disposal{GifDisposal::Unspecified};
    int left{0};
    int top{0};
    int width{0};
    int height{0};
};

struct GifDecodeResult {
    int width{0};
    int height{0};
    int loop_count{0}; // 0 = infinite loop (NETSCAPE2.0) or default
    bool has_loop_extension{false};
    std::vector<GifFrame> frames;
    std::string error;

    bool ok() const { return !frames.empty() && error.empty(); }
};

/// Zero-dependency GIF87a / GIF89a decoder supporting LZW decompression,
/// global and local color tables, transparency, 4-pass interlacing, and
/// multi-frame animation composition with all four disposal methods.
class GifDecoder {
public:
    static constexpr int64_t kMaxDecodedPixels = 64ll * 1024 * 1024;
    static constexpr int64_t kMaxAnimationPixels = 64ll * 1024 * 1024;
    static constexpr size_t kMaxFrames = 1024;

    /// Returns true if the buffer starts with "GIF87a" or "GIF89a".
    static bool is_gif(const uint8_t* data, size_t size);

    /// Decodes all frames (up to max_frames and total pixel budget) with full
    /// animation composition and disposal handling.
    static GifDecodeResult decode_frames(
        const uint8_t* data,
        size_t size,
        size_t max_frames = kMaxFrames
    );

    /// Decodes the first composited frame into 0xAARRGGBB ImageData, or nullptr on error.
    static std::shared_ptr<ImageData> decode(const uint8_t* data, size_t size);
    static std::shared_ptr<ImageData> decode(const std::vector<uint8_t>& data);
};

} // namespace litebird
