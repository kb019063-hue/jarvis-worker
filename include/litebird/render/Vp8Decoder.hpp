#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace litebird {

/// A standalone decoder for the lossy half of WebP: a `VP8 ` key frame.
///
/// WebP's lossy images are VP8 key frames — the intra frame of a video codec
/// that never needs a previous frame — wrapped in a RIFF container. This class
/// reads the frame and hands back the three planes it reconstructs; turning
/// those into pixels is `to_argb`, which follows libwebp's own arithmetic so
/// that the result matches what every other WebP decoder produces, sample for
/// sample, because VP8's transforms and filters are specified exactly.
class Vp8Decoder {
 public:
  enum class Status {
    Ok,
    NotKeyFrame,         ///< An inter frame: needs a previous frame we do not have.
    UnsupportedProfile,  ///< A profile above 3, which no encoder emits.
    Corrupt,             ///< The bitstream contradicts itself.
    Truncated,           ///< The stream ended in the middle of a frame.
    TooLarge,            ///< Dimensions past the budget, refused before allocating.
  };

  /// One decoded frame. The planes carry a border on every side so that the
  /// intra predictors and the loop filter can read their neighbours without a
  /// bounds check, and `*_stride` is the distance between rows, border
  /// included: pixel (x, y) of the luma plane is `y[y_stride * (y + kBorder) +
  /// x + kBorder]`.
  struct Frame {
    int width = 0;
    int height = 0;
    int mb_width = 0;
    int mb_height = 0;
    int y_stride = 0;
    int uv_stride = 0;
    std::vector<uint8_t> y;
    std::vector<uint8_t> u;
    std::vector<uint8_t> v;
  };

  static constexpr int kBorder = 16;
  static constexpr int64_t kMaxDecodedPixels = 64LL << 20;
  static constexpr int kMaxDimension = 16384;

  /// True if these bytes start with a VP8 frame signature.
  static bool is_vp8(const uint8_t* data, size_t size);

  /// Decode one key frame. On failure `frame` is left empty and `error` says
  /// what was wrong in a phrase a person can act on.
  static Status decode(const uint8_t* data, size_t size, Frame& frame, std::string& error);

  /// Convert a decoded frame to 0xAARRGGBB pixels: bilinear ("fancy") chroma
  /// upsampling followed by the fixed-point BT.601 studio-swing matrix, both
  /// exactly as libwebp computes them. `alpha`, when not null, is a
  /// full-resolution alpha plane of `width * height` bytes read with
  /// `alpha_stride`; otherwise every pixel is opaque.
  static void to_argb(
      const Frame& frame,
      const uint8_t* alpha,
      int alpha_stride,
      std::vector<uint32_t>& out
  );

  static const char* status_string(Status status);
};

}  // namespace litebird
