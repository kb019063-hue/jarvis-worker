#pragma once

#include "litebird/render/ImageDecoder.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace litebird {

/// Zero-dependency WebP decoder: the RIFF container, the lossless (VP8L)
/// bitstream, and (since Phase 57) the lossy `VP8` still image behind it.
///
/// VP8L is a Huffman-coded LZ77 stream over ARGB pixels, preceded by up to four
/// self-similarity transforms (predictor, cross-colour, subtract-green and
/// colour-indexing) and carrying an optional colour cache. The transforms are
/// stored as sub-images inside the same stream, so decoding is recursive: the
/// predictor and colour-transform images, the entropy (meta prefix code) image
/// and the palette are all decoded with the same image-stream reader they
/// describe.
///
/// A lossy image is a `VP8 ` key frame, decoded by `Vp8Decoder` and handed back
/// here through its YUV planes and a shared RGB conversion; the `ALPH` chunk
/// that carries a lossy image's alpha plane is decoded with the same VP8L
/// reader as a lossless file.
///
/// An animated file (a `VP8X` canvas with the animation flag, an `ANIM` chunk
/// and one `ANMF` chunk per frame) is decoded frame by frame: each `ANMF` holds
/// a rectangle, a duration, the blending method and the disposal method, and a
/// `VP8 ` or `VP8L` image (with its own `ALPH` when it is lossy and
/// translucent). Frames are composited on the canvas the way libwebp composites
/// them, and every frame is handed back whole. `decode()` — what an `<img>`
/// gets — is the first frame, as it is for a GIF.
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
        UnsupportedAlpha,      ///< `ALPH` with level-reduction preprocessing
        TooLarge,              ///< dimension or pixel budget exceeded
        Truncated,             ///< the container or a chunk ends early
        Corrupt,               ///< the bitstream does not satisfy the format
    };

    struct DecodeResult {
        std::shared_ptr<ImageData> image; // 0xAARRGGBB, as ImageData expects
        Status status{Status::NotWebP};
        std::string error; // empty on success
        bool lossy{false}; // the pixels came through the VP8 codec, not VP8L

        bool ok() const { return image != nullptr && status == Status::Ok; }
    };

    /// One frame of an animation: the canvas as it stands after the frame has
    /// been drawn, which is what a viewer shows for the frame's duration.
    struct AnimationFrame {
        std::shared_ptr<ImageData> image; // the whole canvas, 0xAARRGGBB
        int duration_ms{0};
        /// The rectangle the frame painted, in canvas pixels.
        int x{0};
        int y{0};
        int width{0};
        int height{0};
        /// The standard's blending method: true = alpha-blend the frame over the
        /// canvas, false = write its pixels over the canvas as they are.
        bool blend{true};
        /// The disposal method: true = clear the rectangle once the frame is
        /// done (a later frame sees a transparent rectangle there).
        bool dispose_to_background{false};
    };

    struct Animation {
        int canvas_width{0};
        int canvas_height{0};
        /// `ANIM`'s background colour, 0xAARRGGBB as the file stores it. It is
        /// reported, not painted: the canvas starts transparent, which is what
        /// libwebp's decoder does and what a page shows an animation over.
        uint32_t background{0};
        /// Play the animation this many times; 0 means forever (and is what
        /// every encoder writes when a file loops at all).
        int loop_count{0};
        /// True when the file is an animation rather than a still image that
        /// was asked for its frames.
        bool animated{false};
        /// True when the first frame's pixels came through the VP8 codec.
        /// decode_detailed() reports the same thing for a still image.
        bool first_is_lossy{false};
        std::vector<AnimationFrame> frames;
        std::string error; // empty on success

        bool ok() const { return !frames.empty() && error.empty(); }
    };

    /// Bounds on an animation, shared with the GIF decoder's: frames of
    /// kMaxDecodedPixels each, kMaxAnimationPixels over all of them, and at most
    /// kMaxFrames of them.
    static constexpr int64_t kMaxAnimationPixels = 64ll * 1024 * 1024;
    static constexpr size_t kMaxFrames = 1024;

    /// Returns true if the buffer starts with `RIFF`, four size bytes, `WEBP`.
    static bool is_webp(const uint8_t* data, size_t size);

    /// Decodes a VP8L file to 0xAARRGGBB pixels, or returns nullptr.
    static std::shared_ptr<ImageData> decode(const uint8_t* data, size_t size);
    static std::shared_ptr<ImageData> decode(const std::vector<uint8_t>& data);

    /// Decodes with a status and a message describing a refusal or a failure.
    static DecodeResult decode_detailed(const uint8_t* data, size_t size);

    /// Decodes every frame of an animation (up to `max_frames` and the pixel
    /// budget), composited on the canvas. A still image comes back as one frame
    /// with no duration. `decode_frames_...` never throws: what went wrong is in
    /// `error`.
    static Animation decode_frames(const uint8_t* data, size_t size, size_t max_frames = kMaxFrames);
    static Animation decode_frames(const std::vector<uint8_t>& data, size_t max_frames = kMaxFrames) {
        return decode_frames(data.data(), data.size(), max_frames);
    }

    /// The frame the file shows when it is not played: the first one. Null for
    /// anything that does not decode.
    static std::shared_ptr<ImageData> first_frame(const uint8_t* data, size_t size);

    /// Whether the container holds a VP8L image this decoder can read, without
    /// decoding it: useful to tell "lossy" from "not an image at all" without
    /// paying for the pixels.
    static Status probe(const uint8_t* data, size_t size);

    static const char* status_string(Status status);
};

} // namespace litebird
