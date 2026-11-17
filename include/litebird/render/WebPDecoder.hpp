#pragma once

#include "litebird/render/ImageDecoder.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace litebird {

/// Zero-dependency WebP decoder: the RIFF container and the lossless (VP8L)
/// bitstream.
///
/// VP8L is a Huffman-coded LZ77 stream over ARGB pixels, preceded by up to four
/// self-similarity transforms (predictor, cross-colour, subtract-green and
/// colour-indexing) and carrying an optional colour cache. The transforms are
/// stored as sub-images inside the same stream, so decoding is recursive: the
/// predictor and colour-transform images, the entropy (meta prefix code) image
/// and the palette are all decoded with the same image-stream reader they
/// describe.
///
/// The lossy `VP8` codec — a keyframe of the VP8 video codec — is *not* decoded
/// here: a file that needs it is reported as `UnsupportedLossy` rather than
/// being approximated or half-drawn. Animated WebP (`ANIM`/`ANMF`, whose frames
/// are VP8 or VP8L images composited on a canvas) is reported as
/// `UnsupportedAnimation` for the same reason.
class WebPDecoder {
public:
    /// Decoded pixel budget, shared with the GIF decoder's bound: an image may
    /// not claim more pixels than this, whatever its header says.
    static constexpr int64_t kMaxDecodedPixels = 64ll * 1024 * 1024;
    /// VP8L stores width and height minus one in 14 bits each.
    static constexpr int kMaxDimension = 16384;

    enum class Status {
        Ok = 0,
        NotWebP,               ///< no `RIFF....WEBP` header
        UnsupportedLossy,      ///< a `VP8 ` (lossy) chunk: needs the VP8 codec
        UnsupportedAnimation,  ///< `ANIM`/`ANMF` present
        TooLarge,              ///< dimension or pixel budget exceeded
        Truncated,             ///< the container or a chunk ends early
        Corrupt,               ///< the bitstream does not satisfy the format
    };

    struct DecodeResult {
        std::shared_ptr<ImageData> image; // 0xAARRGGBB, as ImageData expects
        Status status{Status::NotWebP};
        std::string error; // empty on success

        bool ok() const { return image != nullptr && status == Status::Ok; }
    };

    /// Returns true if the buffer starts with `RIFF`, four size bytes, `WEBP`.
    static bool is_webp(const uint8_t* data, size_t size);

    /// Decodes a VP8L file to 0xAARRGGBB pixels, or returns nullptr.
    static std::shared_ptr<ImageData> decode(const uint8_t* data, size_t size);
    static std::shared_ptr<ImageData> decode(const std::vector<uint8_t>& data);

    /// Decodes with a status and a message describing a refusal or a failure.
    static DecodeResult decode_detailed(const uint8_t* data, size_t size);

    /// Whether the container holds a VP8L image this decoder can read, without
    /// decoding it: useful to tell "lossy" from "not an image at all" without
    /// paying for the pixels.
    static Status probe(const uint8_t* data, size_t size);

    static const char* status_string(Status status);
};

} // namespace litebird
