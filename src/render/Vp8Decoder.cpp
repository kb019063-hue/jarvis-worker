// The lossy half of WebP: a VP8 key frame.
//
// A VP8 key frame is a still picture in a video codec's clothes. Every one of
// its macroblocks is intra-coded (there is no previous frame to predict from),
// and the frame is fully specified: the transforms, the predictors and the loop
// filter are defined in integer arithmetic with fixed rounding, so a correct
// decoder produces the same samples as every other correct decoder. That is
// what lets `make verify-media` compare against libwebp pixel for pixel rather
// than by eye.
//
// The layers, bottom up:
//
//   1. `BoolReader` — the arithmetic (range) coder of RFC 6386 section 7.3.
//   2. The frame header: segmentation, the loop filter, the quantiser, and the
//      per-node coefficient probabilities, each starting from the default table
//      and updated by the stream.
//   3. The macroblock modes (16x16, 4x4 and chroma intra, segments, skips).
//   4. The DCT coefficients: tokens in a tree of eleven probability nodes per
//      band, with the "has a nonzero neighbour" contexts.
//   5. Reconstruction: the intra predictors, the 4x4 inverse DCT, the inverse
//      Walsh-Hadamard transform that carries a 16x16 block's sixteen DCs, and
//      the loop filter that smooths block edges.
//
// Everything here reads its neighbours out of the frame buffer's border instead
// of testing whether they exist: the border is seeded with the values RFC 6386
// section 12.2 prescribes (127 above, 129 to the left), which is exactly what
// libvpx and libwebp do, and is why the DC prediction of a block with no
// neighbours averages sixteen 127s and sixteen 129s to 128.

#include "litebird/render/Vp8Decoder.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "litebird/render/Vp8Tables.hpp"

namespace litebird {
namespace {

using namespace litebird::vp8;

constexpr int kLotsOfBits = 0x40000000;
constexpr int kNumSegments = 4;

// Intra prediction modes: the first four are the 16x16 and chroma modes, and
// all ten are legal for a 4x4 block.
enum IntraMode {
  kDcPred = 0,
  kTmPred = 1,
  kVePred = 2,
  kHePred = 3,
  kRdPred = 4,
  kVrPred = 5,
  kLdPred = 6,
  kVlPred = 7,
  kHdPred = 8,
  kHuPred = 9,
};

int clip_int(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

uint8_t clip8(int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); }

int clip_signed2(int v) { return v < -16 ? -16 : (v > 15 ? 15 : v); }

int abs0(int v) { return v < 0 ? (v < -255 ? 255 : -v) : (v > 255 ? 255 : v); }

// ---------------------------------------------------------------------------
// The arithmetic decoder (RFC 6386 section 7.3).
//
// `value_` holds the next 32 bits of the stream, the top byte of which is the
// one being decoded; `count_` counts the bits buffered below it. When the
// stream runs out the count is boosted by a sentinel larger than any real
// stream can reach, so a count that lands back inside (32, kLotsOfBits) means
// bits were consumed that were never in the file.
// ---------------------------------------------------------------------------
class BoolReader {
 public:
  void reset(const uint8_t* data, size_t size) {
    data_ = data;
    size_ = size;
    pos_ = 0;
    value_ = 0;
    count_ = -8;
    range_ = 255;
    overrun_ = false;
    refill();
  }

  uint32_t get_bit(uint32_t probability) {
    if (count_ < 0) refill();
    const uint32_t split = 1 + (((range_ - 1) * probability) >> 8);
    const uint32_t big_split = split << 24;
    uint32_t range = split;
    uint32_t bit = 0;
    if (value_ >= big_split) {
      range = range_ - split;
      value_ -= big_split;
      bit = 1;
    }
    int shift = 0;
    while (range < 128) {
      range <<= 1;
      ++shift;
    }
    range_ = range;
    value_ <<= shift;
    count_ -= shift;
    check_overrun();
    return bit;
  }

  bool get_bool(uint32_t probability) { return get_bit(probability) != 0; }

  /// L(n): n literal bits, each coded with probability 128.
  uint32_t get_literal(int bits) {
    uint32_t value = 0;
    for (int b = bits - 1; b >= 0; --b) value |= get_bit(0x80) << b;
    return value;
  }

  /// A literal followed by a sign bit: the delta form the header uses.
  int get_signed_literal(int bits) {
    const int value = static_cast<int>(get_literal(bits));
    return get_bool(0x80) ? -value : value;
  }

  /// A coefficient's sign: the range is halved, with no probability code.
  int get_signed_magnitude(int magnitude) {
    if (count_ < 0) refill();
    const uint32_t split = (range_ + 1) >> 1;
    const uint32_t big_split = split << 24;
    int value;
    if (value_ < big_split) {
      range_ = split;
      value = magnitude;
    } else {
      range_ = range_ - split;
      value_ -= big_split;
      value = -magnitude;
    }
    range_ += range_;
    value_ += value_;
    count_ -= 1;
    check_overrun();
    return value;
  }

  bool overrun() const { return overrun_; }

 private:
  void check_overrun() {
    if (count_ > 32 && count_ < kLotsOfBits) overrun_ = true;
  }

  void refill() {
    int shift = 16 - count_;
    const int bits_left = static_cast<int>((size_ - pos_) * 8);
    const int x = shift + 8 - bits_left;
    int loop_end = 0;
    if (x >= 0) {
      count_ += kLotsOfBits;
      loop_end = x;
    }
    if (x < 0 || bits_left > 0) {
      while (shift >= loop_end) {
        count_ += 8;
        value_ |= static_cast<uint32_t>(data_[pos_++]) << shift;
        shift -= 8;
      }
    }
  }

  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t pos_ = 0;
  uint32_t value_ = 0;
  int count_ = -8;
  uint32_t range_ = 255;
  bool overrun_ = false;
};

// Each of the four block types has eight coefficient bands; each band has
// three contexts (no nonzero neighbour, one, two) with eleven nodes each.
using ProbaRow = uint8_t[11];

struct QuantMatrix {
  int y1[2] = {4, 4};  ///< {DC, AC} for a luma block coded on its own.
  int y2[2] = {4, 4};  ///< {DC, AC} for the Walsh-Hadamard block.
  int uv[2] = {4, 4};  ///< {DC, AC} for a chroma block.
};

struct FilterStrength {
  int limit = 0;
  int ilevel = 0;
  int hev_threshold = 0;
};

struct MbInfo {
  int segment = 0;
  bool skip = false;
  // Whether this macroblock coded any coefficient at all: like `is_4x4`, the
  // loop filter treats a macroblock with residuals differently from an empty
  // one, and an empty one may still be flagged as skipped only when the frame
  // carries a skip probability.
  bool coded = false;
  bool is_4x4 = false;
  uint8_t modes[16] = {};
  int uv_mode = kDcPred;
  int eob[25] = {};
};

// ---------------------------------------------------------------------------
// One frame's decode state and every step of its decode.
// ---------------------------------------------------------------------------
class FrameDecoder {
 public:
  Vp8Decoder::Status run(const uint8_t* data, size_t size, Vp8Decoder::Frame& frame,
                         std::string& error);

 private:
  Vp8Decoder::Status parse_header(const uint8_t* data, size_t size, std::string& error);
  bool parse_token_partitions(const uint8_t* data, size_t size, std::string& error);
  void parse_quant();
  void parse_probabilities();
  void precompute_filter_strengths();

  void parse_mode_row();
  int parse_residuals(int mb_x);
  int get_coeffs(const uint8_t* const (*bands)[3], int ctx, int dc_q, int ac_q, int first,
                 int16_t* out);
  bool parse_macroblock(int mb_x);
  void reconstruct(int mb_x, int mb_y);
  void filter_macroblock(int mb_x, int mb_y);

  void predict_16x16(uint8_t* dst, int stride, int mode, bool no_top, bool no_left);
  void predict_8x8(uint8_t* dst, int stride, int mode, bool no_top, bool no_left);
  void predict_4x4(uint8_t* dst, int stride, int mode);
  void add_transform(const int16_t* coeffs, uint8_t* dst, int stride);
  void inverse_wht(const int16_t* dc, int16_t* out);

  Vp8Decoder::Frame* frame_ = nullptr;

  BoolReader modes_;                     // partition 0: the header and the modes
  std::vector<BoolReader> token_parts_;  // the coefficient partitions
  BoolReader* tokens_ = nullptr;         // the one the current row reads

  int width_ = 0;
  int height_ = 0;
  int mb_width_ = 0;
  int mb_height_ = 0;

  // Segmentation (section 9.3).
  bool use_segment_ = false;
  bool update_segment_map_ = false;
  bool segment_absolute_ = true;
  int segment_quantizer_[kNumSegments] = {};
  int segment_filter_strength_[kNumSegments] = {};
  uint8_t segment_tree_probs_[3] = {255, 255, 255};

  // Loop filter (section 9.4).
  bool filter_simple_ = false;
  int filter_level_ = 0;
  int filter_sharpness_ = 0;
  bool use_lf_delta_ = false;
  int ref_lf_delta_[4] = {};
  int mode_lf_delta_[4] = {};
  FilterStrength filter_strengths_[kNumSegments][2];

  // What the loop filter needs to know about one macroblock. It is recorded
  // while the macroblock is decoded and used by the filter pass that runs once
  // the whole frame is reconstructed: prediction reads unfiltered samples, so
  // filtering cannot happen until every prediction that could read the pixels
  // has been made.
  struct MbFilterInfo {
    uint8_t limit = 0;
    uint8_t ilevel = 0;
    uint8_t hev = 0;
    bool inner = false;
  };
  std::vector<MbFilterInfo> filter_info_;

  // Quantiser (section 9.6).
  int base_q_ = 0;
  QuantMatrix quant_[kNumSegments];

  // Entropy contexts (section 13).
  ProbaRow coeff_probs_[4][8][3] = {};
  const uint8_t* bands_[4][17][3] = {};
  bool use_skip_proba_ = false;
  uint8_t skip_probability_ = 0;

  // The row being decoded, and the neighbour contexts the tokens read.
  std::vector<MbInfo> mb_row_;
  std::vector<uint8_t> intra_above_;
  uint8_t intra_left_[4] = {};
  std::vector<uint8_t> nz_above_y_;
  uint8_t nz_left_y_[4] = {};
  std::vector<uint8_t> nz_above_y2_;
  uint8_t nz_left_y2_ = 0;
  std::vector<uint8_t> nz_above_u_;
  std::vector<uint8_t> nz_above_v_;
  uint8_t nz_left_u_[2] = {};
  uint8_t nz_left_v_[2] = {};

  int16_t coeffs_[25][16] = {};
};

// ---------------------------------------------------------------------------
// The header.
// ---------------------------------------------------------------------------
Vp8Decoder::Status FrameDecoder::parse_header(const uint8_t* data, size_t size, std::string& error) {
  if (size < 10) {
    error = "a VP8 frame needs at least ten bytes of header";
    return Vp8Decoder::Status::Truncated;
  }
  const uint32_t bits = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                        (static_cast<uint32_t>(data[2]) << 16);
  const bool key_frame = (bits & 1) == 0;
  const int profile = static_cast<int>((bits >> 1) & 7);
  const bool show = ((bits >> 4) & 1) != 0;
  const size_t first_partition_size = bits >> 5;

  if (!key_frame) {
    error = "this VP8 frame is inter-coded and needs the frame before it";
    return Vp8Decoder::Status::NotKeyFrame;
  }
  if (profile > 3) {
    error = "VP8 profile " + std::to_string(profile) + " is not defined";
    return Vp8Decoder::Status::UnsupportedProfile;
  }
  if (!show) {
    error = "a VP8 frame that is not shown has no picture";
    return Vp8Decoder::Status::Corrupt;
  }
  if (first_partition_size > size - 3) {
    error = "the VP8 header claims a first partition longer than the frame";
    return Vp8Decoder::Status::Truncated;
  }
  if (size < 10 || data[3] != 0x9d || data[4] != 0x01 || data[5] != 0x2a) {
    error = "the VP8 start code is missing";
    return Vp8Decoder::Status::Corrupt;
  }
  width_ = ((data[7] << 8) | data[6]) & 0x3fff;
  height_ = ((data[9] << 8) | data[8]) & 0x3fff;
  if (width_ == 0 || height_ == 0) {
    error = "a VP8 frame with a zero dimension has no picture";
    return Vp8Decoder::Status::Corrupt;
  }
  if (width_ > Vp8Decoder::kMaxDimension || height_ > Vp8Decoder::kMaxDimension) {
    error = "a VP8 frame of " + std::to_string(width_) + "x" + std::to_string(height_) +
            " is past the dimension limit";
    return Vp8Decoder::Status::TooLarge;
  }
  if (static_cast<int64_t>(width_) * height_ > Vp8Decoder::kMaxDecodedPixels) {
    error = "a VP8 frame of " + std::to_string(width_) + "x" + std::to_string(height_) +
            " is past the decoded-pixel budget";
    return Vp8Decoder::Status::TooLarge;
  }
  mb_width_ = (width_ + 15) / 16;
  mb_height_ = (height_ + 15) / 16;

  const uint8_t* cursor = data + 10;
  size_t remaining = size - 10;
  modes_.reset(cursor, first_partition_size);
  cursor += first_partition_size;
  remaining -= first_partition_size;

  // The colour-space and clamping bits are read and dropped: both are advisory,
  // and the conversion at the end uses the fixed BT.601 matrix either way.
  modes_.get_bit(0x80);
  modes_.get_bit(0x80);

  use_segment_ = modes_.get_bool(0x80);
  if (use_segment_) {
    update_segment_map_ = modes_.get_bool(0x80);
    if (modes_.get_bool(0x80)) {  // the segment data is sent in this frame
      segment_absolute_ = modes_.get_bool(0x80);
      for (int s = 0; s < kNumSegments; ++s) {
        segment_quantizer_[s] = modes_.get_bool(0x80) ? modes_.get_signed_literal(7) : 0;
      }
      for (int s = 0; s < kNumSegments; ++s) {
        segment_filter_strength_[s] = modes_.get_bool(0x80) ? modes_.get_signed_literal(6) : 0;
      }
    }
    if (update_segment_map_) {
      for (int s = 0; s < 3; ++s) {
        segment_tree_probs_[s] =
            modes_.get_bool(0x80) ? static_cast<uint8_t>(modes_.get_literal(8)) : 255;
      }
    }
  }

  filter_simple_ = modes_.get_bool(0x80);
  filter_level_ = static_cast<int>(modes_.get_literal(6));
  filter_sharpness_ = static_cast<int>(modes_.get_literal(3));
  use_lf_delta_ = modes_.get_bool(0x80);
  if (use_lf_delta_ && modes_.get_bool(0x80)) {
    for (int i = 0; i < 4; ++i) {
      if (modes_.get_bool(0x80)) ref_lf_delta_[i] = modes_.get_signed_literal(6);
    }
    for (int i = 0; i < 4; ++i) {
      if (modes_.get_bool(0x80)) mode_lf_delta_[i] = modes_.get_signed_literal(6);
    }
  }
  if (modes_.overrun()) {
    error = "the VP8 frame header ended in the middle";
    return Vp8Decoder::Status::Truncated;
  }

  if (!parse_token_partitions(cursor, remaining, error)) return Vp8Decoder::Status::Truncated;
  parse_quant();
  // The "update probabilities" bit is read and dropped: a key frame always
  // sends its coefficient probabilities.
  modes_.get_bit(0x80);
  parse_probabilities();
  precompute_filter_strengths();
  if (modes_.overrun()) {
    error = "the VP8 frame header ended in the middle";
    return Vp8Decoder::Status::Truncated;
  }
  return Vp8Decoder::Status::Ok;
}

bool FrameDecoder::parse_token_partitions(const uint8_t* data, size_t size, std::string& error) {
  const int partitions = 1 << modes_.get_literal(2);
  if (size < 3 * static_cast<size_t>(partitions - 1)) {
    error = "the VP8 token partition sizes are cut off";
    return false;
  }
  const uint8_t* sizes = data;
  const uint8_t* start = data + 3 * static_cast<size_t>(partitions - 1);
  size_t left = size - 3 * static_cast<size_t>(partitions - 1);
  token_parts_.clear();
  token_parts_.resize(static_cast<size_t>(partitions));
  for (int p = 0; p + 1 < partitions; ++p) {
    size_t part_size = static_cast<size_t>(sizes[0]) | (static_cast<size_t>(sizes[1]) << 8) |
                       (static_cast<size_t>(sizes[2]) << 16);
    sizes += 3;
    if (part_size > left) part_size = left;
    token_parts_[static_cast<size_t>(p)].reset(start, part_size);
    start += part_size;
    left -= part_size;
  }
  token_parts_.back().reset(start, left);
  return true;
}

void FrameDecoder::parse_quant() {
  base_q_ = static_cast<int>(modes_.get_literal(7));
  const int dq_y1_dc = modes_.get_bool(0x80) ? modes_.get_signed_literal(4) : 0;
  const int dq_y2_dc = modes_.get_bool(0x80) ? modes_.get_signed_literal(4) : 0;
  const int dq_y2_ac = modes_.get_bool(0x80) ? modes_.get_signed_literal(4) : 0;
  const int dq_uv_dc = modes_.get_bool(0x80) ? modes_.get_signed_literal(4) : 0;
  const int dq_uv_ac = modes_.get_bool(0x80) ? modes_.get_signed_literal(4) : 0;

  for (int s = 0; s < kNumSegments; ++s) {
    int q;
    if (use_segment_) {
      q = segment_quantizer_[s];
      if (!segment_absolute_) q += base_q_;
    } else {
      if (s > 0) {
        quant_[s] = quant_[0];
        continue;
      }
      q = base_q_;
    }
    QuantMatrix& m = quant_[s];
    m.y1[0] = kDcTable[clip_int(q + dq_y1_dc, 0, 127)];
    m.y1[1] = kAcTable[clip_int(q, 0, 127)];
    m.y2[0] = kDcTable[clip_int(q + dq_y2_dc, 0, 127)] * 2;
    // The Walsh-Hadamard AC step is the AC step scaled by 155/100, and
    // (x * 101581) >> 16 is that multiply for every step VP8 defines.
    m.y2[1] = (kAcTable[clip_int(q + dq_y2_ac, 0, 127)] * 101581) >> 16;
    if (m.y2[1] < 8) m.y2[1] = 8;
    m.uv[0] = kDcTable[clip_int(q + dq_uv_dc, 0, 117)];
    m.uv[1] = kAcTable[clip_int(q + dq_uv_ac, 0, 127)];
  }
}

void FrameDecoder::parse_probabilities() {
  for (int t = 0; t < 4; ++t) {
    for (int b = 0; b < 8; ++b) {
      for (int c = 0; c < 3; ++c) {
        for (int p = 0; p < 11; ++p) {
          coeff_probs_[t][b][c][p] = modes_.get_bit(kCoeffUpdateProbs[t][b][c][p])
                                         ? static_cast<uint8_t>(modes_.get_literal(8))
                                         : kDefaultCoeffProbs[t][b][c][p];
        }
      }
    }
  }
  // Position 16 is past the end of a block; its band entry is the sentinel the
  // token loop reads after the sixteenth coefficient, and it must not be
  // confused with band zero.
  for (int t = 0; t < 4; ++t) {
    for (int n = 0; n < 17; ++n) {
      const int band = kCoeffBands[n];
      for (int c = 0; c < 3; ++c) bands_[t][n][c] = coeff_probs_[t][band][c];
    }
  }
  use_skip_proba_ = modes_.get_bool(0x80);
  if (use_skip_proba_) skip_probability_ = static_cast<uint8_t>(modes_.get_literal(8));
}

void FrameDecoder::precompute_filter_strengths() {
  for (int s = 0; s < kNumSegments; ++s) {
    for (int is_4x4 = 0; is_4x4 <= 1; ++is_4x4) filter_strengths_[s][is_4x4] = FilterStrength{};
  }
  if (filter_level_ == 0) return;
  for (int s = 0; s < kNumSegments; ++s) {
    int base;
    if (use_segment_) {
      base = segment_filter_strength_[s];
      if (!segment_absolute_) base += filter_level_;
    } else {
      base = filter_level_;
    }
    for (int is_4x4 = 0; is_4x4 <= 1; ++is_4x4) {
      FilterStrength& strength = filter_strengths_[s][is_4x4];
      int level = base;
      if (use_lf_delta_) {
        level += ref_lf_delta_[0];
        if (is_4x4) level += mode_lf_delta_[0];
      }
      level = clip_int(level, 0, 63);
      if (level == 0) continue;
      int ilevel = level;
      if (filter_sharpness_ > 0) {
        ilevel >>= filter_sharpness_ > 4 ? 2 : 1;
        if (ilevel > 9 - filter_sharpness_) ilevel = 9 - filter_sharpness_;
      }
      if (ilevel < 1) ilevel = 1;
      strength.ilevel = ilevel;
      strength.limit = 2 * level + ilevel;
      strength.hev_threshold = level >= 40 ? 2 : (level >= 15 ? 1 : 0);
    }
  }
}

// ---------------------------------------------------------------------------
// Macroblock modes (RFC 6386 section 11).
// ---------------------------------------------------------------------------
void FrameDecoder::parse_mode_row() {
  // `intra_above_` is not reset here: it holds the modes of the 4x4 blocks in
  // the row above, which the key-frame B-mode probabilities are indexed by.
  intra_left_[0] = intra_left_[1] = intra_left_[2] = intra_left_[3] = kDcPred;
  for (int mb_x = 0; mb_x < mb_width_; ++mb_x) {
    MbInfo& mb = mb_row_[static_cast<size_t>(mb_x)];
    mb = MbInfo{};
    if (update_segment_map_) {
      mb.segment = modes_.get_bit(segment_tree_probs_[0])
                       ? static_cast<int>(modes_.get_bit(segment_tree_probs_[2]) + 2)
                       : static_cast<int>(modes_.get_bit(segment_tree_probs_[1]));
    }
    if (use_skip_proba_) mb.skip = modes_.get_bool(skip_probability_);
    mb.is_4x4 = !modes_.get_bool(kBlockSizeProb);
    uint8_t* above = intra_above_.data() + 4 * mb_x;
    if (!mb.is_4x4) {
      int mode;
      if (modes_.get_bit(156)) {
        mode = modes_.get_bit(128) ? kTmPred : kHePred;
      } else {
        mode = modes_.get_bit(163) ? kVePred : kDcPred;
      }
      for (int i = 0; i < 16; ++i) mb.modes[i] = static_cast<uint8_t>(mode);
      std::fill(above, above + 4, static_cast<uint8_t>(mode));
      std::fill(intra_left_, intra_left_ + 4, static_cast<uint8_t>(mode));
    } else {
      for (int y = 0; y < 4; ++y) {
        int left_mode = intra_left_[y];
        for (int x = 0; x < 4; ++x) {
          const uint8_t* prob = kKeyframeBModeProbs[above[x]][left_mode];
          int node = kIntra4ModeTree[modes_.get_bit(prob[0])];
          while (node > 0) node = kIntra4ModeTree[2 * node + modes_.get_bit(prob[node])];
          left_mode = -node;
          above[x] = static_cast<uint8_t>(left_mode);
        }
        for (int x = 0; x < 4; ++x) mb.modes[y * 4 + x] = above[x];
        intra_left_[y] = static_cast<uint8_t>(left_mode);
      }
    }
    if (modes_.get_bit(kKeyframeUvModeProb[0])) {
      if (modes_.get_bit(kKeyframeUvModeProb[1])) {
        mb.uv_mode = modes_.get_bit(kKeyframeUvModeProb[2]) ? kTmPred : kHePred;
      } else {
        mb.uv_mode = kVePred;
      }
    } else {
      mb.uv_mode = kDcPred;
    }
  }
}

// ---------------------------------------------------------------------------
// Coefficients (RFC 6386 section 13.2).
// ---------------------------------------------------------------------------
int FrameDecoder::get_coeffs(const uint8_t* const (*bands)[3], int ctx, int dc_q, int ac_q, int first,
                             int16_t* out) {
  BoolReader& reader = *tokens_;

  const uint8_t* p = bands[first][ctx];
  for (int n = first; n < 16; ++n) {
    if (!reader.get_bit(p[0])) return n;
    while (!reader.get_bit(p[1])) {  // a run of zero coefficients
      ++n;
      if (n == 16) return 16;
      p = bands[n][0];
    }
    int magnitude;
    if (!reader.get_bit(p[2])) {
      magnitude = 1;
      p = bands[n + 1][1];
    } else {
      int value;
      if (!reader.get_bit(p[3])) {
        if (!reader.get_bit(p[4])) {
          value = 2;
        } else {
          value = 3 + static_cast<int>(reader.get_bit(p[5]));
        }
      } else if (!reader.get_bit(p[6])) {
        if (!reader.get_bit(p[7])) {
          value = 5 + static_cast<int>(reader.get_bit(159));
        } else {
          value = 7 + 2 * static_cast<int>(reader.get_bit(165));
          value += static_cast<int>(reader.get_bit(145));
        }
      } else {
        // The four large categories each have their own fixed code, read until
        // the table's zero sentinel.
        const int bit1 = static_cast<int>(reader.get_bit(p[8]));
        const int bit0 = static_cast<int>(reader.get_bit(p[9 + bit1]));
        const int category = 2 * bit1 + bit0;
        const uint8_t* table =
            category == 0 ? kCat3 : (category == 1 ? kCat4 : (category == 2 ? kCat5 : kCat6));
        value = 0;
        for (; *table != 0; ++table) value += value + static_cast<int>(reader.get_bit(*table));
        value += 3 + (8 << category);
      }
      magnitude = value;
      p = bands[n + 1][2];
    }
    out[kZigzag[n]] = static_cast<int16_t>(reader.get_signed_magnitude(magnitude) *
                                           (n > 0 ? ac_q : dc_q));
  }
  return 16;
}

int FrameDecoder::parse_residuals(int mb_x) {
  MbInfo& mb = mb_row_[static_cast<size_t>(mb_x)];
  const QuantMatrix& q = quant_[mb.segment];
  std::memset(coeffs_, 0, sizeof(coeffs_));
  int non_zero = 0;
  bool coded = false;

  int first = 0;
  int block_type;
  if (!mb.is_4x4) {
    int16_t dc[16] = {};
    const int ctx = nz_above_y2_[static_cast<size_t>(mb_x)] + nz_left_y2_;
    const int eob = get_coeffs(bands_[1], ctx, q.y2[0], q.y2[1], 0, dc);
    nz_above_y2_[static_cast<size_t>(mb_x)] = nz_left_y2_ = static_cast<uint8_t>(eob > 0);
    coded = eob > 0;
    // The Walsh-Hadamard transform turns the macroblock's sixteen DC
    // coefficients into one coefficient per luma block. A stream that sends
    // only the DC has the same answer without the transform.
    if (eob > 1) {
      inverse_wht(dc, &coeffs_[0][0]);
    } else {
      const int dc0 = (dc[0] + 3) >> 3;
      for (int i = 0; i < 16; ++i) coeffs_[i][0] = static_cast<int16_t>(dc0);
    }
    first = 1;
    block_type = 0;
  } else {
    block_type = 3;
  }

  uint8_t* above = nz_above_y_.data() + 4 * mb_x;
  for (int y = 0; y < 4; ++y) {
    int left = nz_left_y_[y];
    for (int x = 0; x < 4; ++x) {
      const int ctx = left + above[x];
      const int block = y * 4 + x;
      const int eob = get_coeffs(bands_[block_type], ctx, q.y1[0], q.y1[1], first, coeffs_[block]);
      const int total = eob + first;
      mb.eob[block] = total;
      non_zero += total;
      if (eob > first) coded = true;
      left = eob > first ? 1 : 0;
      above[x] = static_cast<uint8_t>(left);
    }
    nz_left_y_[y] = static_cast<uint8_t>(left);
  }

  for (int plane = 0; plane < 2; ++plane) {  // U, then V
    uint8_t* above_plane = (plane == 0 ? nz_above_u_ : nz_above_v_).data() + 2 * mb_x;
    uint8_t* left_plane = plane == 0 ? nz_left_u_ : nz_left_v_;
    for (int y = 0; y < 2; ++y) {
      int left = left_plane[y];
      for (int x = 0; x < 2; ++x) {
        const int ctx = left + above_plane[x];
        const int block = 16 + plane * 4 + y * 2 + x;
        const int eob = get_coeffs(bands_[2], ctx, q.uv[0], q.uv[1], 0, coeffs_[block]);
        mb.eob[block] = eob;
        non_zero += eob;
        if (eob > 0) coded = true;
        left = eob > 0 ? 1 : 0;
        above_plane[x] = static_cast<uint8_t>(left);
      }
      left_plane[y] = static_cast<uint8_t>(left);
    }
  }
  mb.coded = coded;
  return non_zero;
}

bool FrameDecoder::parse_macroblock(int mb_x) {
  MbInfo& mb = mb_row_[static_cast<size_t>(mb_x)];
  if (mb.skip) {
    nz_above_y_[static_cast<size_t>(4 * mb_x) + 0] = 0;
    nz_above_y_[static_cast<size_t>(4 * mb_x) + 1] = 0;
    nz_above_y_[static_cast<size_t>(4 * mb_x) + 2] = 0;
    nz_above_y_[static_cast<size_t>(4 * mb_x) + 3] = 0;
    nz_left_y_[0] = nz_left_y_[1] = nz_left_y_[2] = nz_left_y_[3] = 0;
    if (!mb.is_4x4) {
      nz_above_y2_[static_cast<size_t>(mb_x)] = 0;
      nz_left_y2_ = 0;
    }
    nz_above_u_[static_cast<size_t>(2 * mb_x)] = 0;
    nz_above_u_[static_cast<size_t>(2 * mb_x) + 1] = 0;
    nz_above_v_[static_cast<size_t>(2 * mb_x)] = 0;
    nz_above_v_[static_cast<size_t>(2 * mb_x) + 1] = 0;
    nz_left_u_[0] = nz_left_u_[1] = nz_left_v_[0] = nz_left_v_[1] = 0;
    for (int i = 0; i < 25; ++i) mb.eob[i] = 0;
    mb.coded = false;
    return true;
  }
  parse_residuals(mb_x);
  return !tokens_->overrun();
}

// ---------------------------------------------------------------------------
// Inverse transforms (RFC 6386 sections 14.3 and 14.4). Both are exact integer
// arithmetic with the rounding term baked in, which is why decoding is
// reproducible rather than approximate.
// ---------------------------------------------------------------------------
namespace {
// (20091 * x) >> 16 + x and (35468 * x) >> 16: sqrt(2) and sqrt(2) - 1 in 16.16
// fixed point, the constants the specification names.
int mul1(int a) {
  return static_cast<int>((static_cast<int32_t>((static_cast<uint32_t>(a) * 20091u)) >> 16) +
                          static_cast<uint32_t>(a));
}
int mul2(int a) {
  return static_cast<int>(static_cast<int32_t>(static_cast<uint32_t>(a) * 35468u) >> 16);
}
}  // namespace

void FrameDecoder::inverse_wht(const int16_t* dc, int16_t* out) {
  int tmp[16];
  for (int i = 0; i < 4; ++i) {
    const int a0 = dc[0 + i] + dc[12 + i];
    const int a1 = dc[4 + i] + dc[8 + i];
    const int a2 = dc[4 + i] - dc[8 + i];
    const int a3 = dc[0 + i] - dc[12 + i];
    tmp[0 + i] = a0 + a1;
    tmp[8 + i] = a0 - a1;
    tmp[4 + i] = a3 + a2;
    tmp[12 + i] = a3 - a2;
  }
  // The second pass writes one DC per luma block: the four blocks of row i take
  // the four outputs, each landing at coefficient zero of its own block.
  for (int i = 0; i < 4; ++i) {
    const int base = tmp[0 + i * 4] + 3;
    const int a0 = base + tmp[3 + i * 4];
    const int a1 = tmp[1 + i * 4] + tmp[2 + i * 4];
    const int a2 = tmp[1 + i * 4] - tmp[2 + i * 4];
    const int a3 = base - tmp[3 + i * 4];
    out[(4 * i + 0) * 16] = static_cast<int16_t>((a0 + a1) >> 3);
    out[(4 * i + 1) * 16] = static_cast<int16_t>((a3 + a2) >> 3);
    out[(4 * i + 2) * 16] = static_cast<int16_t>((a0 - a1) >> 3);
    out[(4 * i + 3) * 16] = static_cast<int16_t>((a3 - a2) >> 3);
  }
}

void FrameDecoder::add_transform(const int16_t* in, uint8_t* dst, int stride) {
  int c[16];  // transposed: c[column * 4 + row]
  for (int i = 0; i < 4; ++i) {  // the vertical pass, column by column
    const int a = in[0 + i] + in[8 + i];
    const int b = in[0 + i] - in[8 + i];
    const int cc = mul2(in[4 + i]) - mul1(in[12 + i]);
    const int d = mul1(in[4 + i]) + mul2(in[12 + i]);
    c[0 + i * 4] = a + d;
    c[1 + i * 4] = b + cc;
    c[2 + i * 4] = b - cc;
    c[3 + i * 4] = a - d;
  }
  for (int y = 0; y < 4; ++y) {  // the horizontal pass, row by row
    const int dc = c[y] + 4;
    const int a = dc + c[8 + y];
    const int b = dc - c[8 + y];
    const int cc = mul2(c[4 + y]) - mul1(c[12 + y]);
    const int d = mul1(c[4 + y]) + mul2(c[12 + y]);
    uint8_t* row = dst + y * stride;
    row[0] = clip8(row[0] + ((a + d) >> 3));
    row[1] = clip8(row[1] + ((b + cc) >> 3));
    row[2] = clip8(row[2] + ((b - cc) >> 3));
    row[3] = clip8(row[3] + ((a - d) >> 3));
  }
}

// ---------------------------------------------------------------------------
// Intra prediction (RFC 6386 section 12.3).
// ---------------------------------------------------------------------------
namespace {
int avg3(int a, int b, int c) { return (a + 2 * b + c + 2) >> 2; }
int avg2(int a, int b) { return (a + b + 1) >> 1; }
}  // namespace

void FrameDecoder::predict_16x16(uint8_t* dst, int stride, int mode, bool no_top, bool no_left) {
  const uint8_t* top = dst - stride;
  switch (mode) {
    case kDcPred: {
      int dc;
      if (no_top && no_left) {
        dc = 0x80;
      } else if (no_top) {
        int sum = 0;
        for (int y = 0; y < 16; ++y) sum += dst[y * stride - 1];
        dc = (sum + 8) >> 4;
      } else if (no_left) {
        int sum = 0;
        for (int x = 0; x < 16; ++x) sum += top[x];
        dc = (sum + 8) >> 4;
      } else {
        int sum = 0;
        for (int i = 0; i < 16; ++i) sum += top[i] + dst[i * stride - 1];
        dc = (sum + 16) >> 5;
      }
      for (int y = 0; y < 16; ++y) std::memset(dst + y * stride, dc, 16);
      return;
    }
    case kVePred:
      for (int y = 0; y < 16; ++y) std::memcpy(dst + y * stride, top, 16);
      return;
    case kHePred:
      for (int y = 0; y < 16; ++y) std::memset(dst + y * stride, dst[y * stride - 1], 16);
      return;
    default: {  // kTmPred: clamp(left + top - top-left)
      const int top_left = top[-1];
      for (int y = 0; y < 16; ++y) {
        const int left = dst[y * stride - 1];
        uint8_t* row = dst + y * stride;
        for (int x = 0; x < 16; ++x) row[x] = clip8(left + top[x] - top_left);
      }
      return;
    }
  }
}

void FrameDecoder::predict_8x8(uint8_t* dst, int stride, int mode, bool no_top, bool no_left) {
  const uint8_t* top = dst - stride;
  switch (mode) {
    case kDcPred: {
      int dc;
      if (no_top && no_left) {
        dc = 0x80;
      } else if (no_top) {
        int sum = 0;
        for (int y = 0; y < 8; ++y) sum += dst[y * stride - 1];
        dc = (sum + 4) >> 3;
      } else if (no_left) {
        int sum = 0;
        for (int x = 0; x < 8; ++x) sum += top[x];
        dc = (sum + 4) >> 3;
      } else {
        int sum = 0;
        for (int i = 0; i < 8; ++i) sum += top[i] + dst[i * stride - 1];
        dc = (sum + 8) >> 4;
      }
      for (int y = 0; y < 8; ++y) std::memset(dst + y * stride, dc, 8);
      return;
    }
    case kVePred:
      for (int y = 0; y < 8; ++y) std::memcpy(dst + y * stride, top, 8);
      return;
    case kHePred:
      for (int y = 0; y < 8; ++y) std::memset(dst + y * stride, dst[y * stride - 1], 8);
      return;
    default: {
      const int top_left = top[-1];
      for (int y = 0; y < 8; ++y) {
        const int left = dst[y * stride - 1];
        uint8_t* row = dst + y * stride;
        for (int x = 0; x < 8; ++x) row[x] = clip8(left + top[x] - top_left);
      }
      return;
    }
  }
}

void FrameDecoder::predict_4x4(uint8_t* dst, int stride, int mode) {
  const uint8_t* top = dst - stride;
  // Named as the specification names them: A..H along the row above, I..L down
  // the column to the left, X at their corner. Addresses are written DST(x, y)
  // exactly as the reference implementation writes them, which keeps the
  // admittedly confusing diagonals of the four blend modes readable.
  const int A = top[0], B = top[1], C = top[2], D = top[3];
  const int E = top[4], F = top[5], G = top[6], H = top[7];
  const int I = dst[-1], J = dst[stride - 1], K = dst[2 * stride - 1], L = dst[3 * stride - 1];
  const int X = top[-1];
  auto DST = [dst, stride](int x, int y) -> uint8_t& { return dst[x + y * stride]; };
  switch (mode) {
    case kDcPred: {
      const int dc = (A + B + C + D + I + J + K + L + 4) >> 3;
      for (int y = 0; y < 4; ++y) std::memset(dst + y * stride, dc, 4);
      return;
    }
    case kTmPred:
      for (int y = 0; y < 4; ++y) {
        const int left_pixel = dst[y * stride - 1];
        for (int x = 0; x < 4; ++x) DST(x, y) = clip8(left_pixel + top[x] - X);
      }
      return;
    case kVePred: {
      const uint8_t vals[4] = {static_cast<uint8_t>(avg3(X, A, B)), static_cast<uint8_t>(avg3(A, B, C)),
                               static_cast<uint8_t>(avg3(B, C, D)), static_cast<uint8_t>(avg3(C, D, E))};
      for (int y = 0; y < 4; ++y) std::memcpy(dst + y * stride, vals, 4);
      return;
    }
    case kHePred:
      for (int y = 0; y < 4; ++y) {
        const int v = y == 0 ? avg3(X, I, J) : y == 1 ? avg3(I, J, K) : y == 2 ? avg3(J, K, L) : avg3(K, L, L);
        std::memset(dst + y * stride, v, 4);
      }
      return;
    case kLdPred:
      DST(0, 0) = avg3(A, B, C);
      DST(1, 0) = DST(0, 1) = avg3(B, C, D);
      DST(2, 0) = DST(1, 1) = DST(0, 2) = avg3(C, D, E);
      DST(3, 0) = DST(2, 1) = DST(1, 2) = DST(0, 3) = avg3(D, E, F);
      DST(3, 1) = DST(2, 2) = DST(1, 3) = avg3(E, F, G);
      DST(3, 2) = DST(2, 3) = avg3(F, G, H);
      DST(3, 3) = avg3(G, H, H);
      return;
    case kRdPred:
      DST(0, 3) = avg3(J, K, L);
      DST(1, 3) = DST(0, 2) = avg3(I, J, K);
      DST(2, 3) = DST(1, 2) = DST(0, 1) = avg3(X, I, J);
      DST(3, 3) = DST(2, 2) = DST(1, 1) = DST(0, 0) = avg3(A, X, I);
      DST(3, 2) = DST(2, 1) = DST(1, 0) = avg3(B, A, X);
      DST(3, 1) = DST(2, 0) = avg3(C, B, A);
      DST(3, 0) = avg3(D, C, B);
      return;
    case kVrPred:
      DST(0, 0) = DST(1, 2) = avg2(X, A);
      DST(1, 0) = DST(2, 2) = avg2(A, B);
      DST(2, 0) = DST(3, 2) = avg2(B, C);
      DST(3, 0) = avg2(C, D);
      DST(0, 3) = avg3(K, J, I);
      DST(0, 2) = avg3(J, I, X);
      DST(0, 1) = DST(1, 3) = avg3(I, X, A);
      DST(1, 1) = DST(2, 3) = avg3(X, A, B);
      DST(2, 1) = DST(3, 3) = avg3(A, B, C);
      DST(3, 1) = avg3(B, C, D);
      return;
    case kVlPred:
      DST(0, 0) = avg2(A, B);
      DST(1, 0) = DST(0, 2) = avg2(B, C);
      DST(2, 0) = DST(1, 2) = avg2(C, D);
      DST(3, 0) = DST(2, 2) = avg2(D, E);
      DST(0, 1) = avg3(A, B, C);
      DST(1, 1) = DST(0, 3) = avg3(B, C, D);
      DST(2, 1) = DST(1, 3) = avg3(C, D, E);
      DST(3, 1) = DST(2, 3) = avg3(D, E, F);
      DST(3, 2) = avg3(E, F, G);
      DST(3, 3) = avg3(F, G, H);
      return;
    case kHdPred:
      DST(0, 0) = DST(2, 1) = avg2(I, X);
      DST(0, 1) = DST(2, 2) = avg2(J, I);
      DST(0, 2) = DST(2, 3) = avg2(K, J);
      DST(0, 3) = avg2(L, K);
      DST(3, 0) = avg3(A, B, C);
      DST(2, 0) = avg3(X, A, B);
      DST(1, 0) = DST(3, 1) = avg3(I, X, A);
      DST(1, 1) = DST(3, 2) = avg3(J, I, X);
      DST(1, 2) = DST(3, 3) = avg3(K, J, I);
      DST(1, 3) = avg3(L, K, J);
      return;
    case kHuPred:
      DST(0, 0) = avg2(I, J);
      DST(2, 0) = DST(0, 1) = avg2(J, K);
      DST(2, 1) = DST(0, 2) = avg2(K, L);
      DST(1, 0) = avg3(I, J, K);
      DST(3, 0) = DST(1, 1) = avg3(J, K, L);
      DST(3, 1) = DST(1, 2) = avg3(K, L, L);
      DST(3, 2) = DST(2, 2) = DST(0, 3) = DST(1, 3) = DST(2, 3) = DST(3, 3) = L;
      return;
    default:
      break;
  }
  for (int y = 0; y < 4; ++y) std::memset(dst + y * stride, 128, 4);
}

// ---------------------------------------------------------------------------
// Reconstruction of one macroblock.
// ---------------------------------------------------------------------------
void FrameDecoder::reconstruct(int mb_x, int mb_y) {
  MbInfo& mb = mb_row_[static_cast<size_t>(mb_x)];
  Vp8Decoder::Frame& f = *frame_;
  const int ys = f.y_stride;
  const int cs = f.uv_stride;
  const int x0 = 16 * mb_x;
  const int y0 = 16 * mb_y;

  uint8_t* y_dst = f.y.data() + (y0 + Vp8Decoder::kBorder) * ys + (x0 + Vp8Decoder::kBorder);
  uint8_t* u_dst = f.u.data() + (y0 / 2 + Vp8Decoder::kBorder) * cs + (x0 / 2 + Vp8Decoder::kBorder);
  uint8_t* v_dst = f.v.data() + (y0 / 2 + Vp8Decoder::kBorder) * cs + (x0 / 2 + Vp8Decoder::kBorder);

  const bool no_top = mb_y == 0;
  const bool no_left = mb_x == 0;

  if (mb.is_4x4) {
    // The four pixels to the right of the row above, which the diagonal
    // predictors read. At the right edge of the frame they repeat the last
    // pixel of that row, and the rows below them repeat the first: exactly what
    // libvpx's scratch cache holds.
    uint8_t* top_right = y_dst - ys + 16;
    if (no_top) {
      std::memset(top_right, 127, 4);
    } else if (mb_x + 1 >= mb_width_) {
      std::memset(top_right, top_right[-1], 4);
    }
    // The same four samples stand in for the missing above-right pixels of the
    // lower block rows: they are read from the row above each block row.
    std::memcpy(top_right + 4 * ys, top_right, 4);
    std::memcpy(top_right + 8 * ys, top_right, 4);
    std::memcpy(top_right + 12 * ys, top_right, 4);
    for (int n = 0; n < 16; ++n) {
      uint8_t* dst = y_dst + (n / 4) * 4 * ys + (n % 4) * 4;
      predict_4x4(dst, ys, mb.modes[n]);
      if (mb.eob[n] > 0) add_transform(coeffs_[n], dst, ys);
    }
  } else {
    predict_16x16(y_dst, ys, mb.modes[0], no_top, no_left);
    for (int n = 0; n < 16; ++n) {
      if (mb.eob[n] > 0) add_transform(coeffs_[n], y_dst + (n / 4) * 4 * ys + (n % 4) * 4, ys);
    }
  }

  predict_8x8(u_dst, cs, mb.uv_mode, no_top, no_left);
  predict_8x8(v_dst, cs, mb.uv_mode, no_top, no_left);
  for (int plane = 0; plane < 2; ++plane) {
    uint8_t* plane_dst = plane == 0 ? u_dst : v_dst;
    for (int n = 0; n < 4; ++n) {
      const int block = 16 + plane * 4 + n;
      if (mb.eob[block] > 0) add_transform(coeffs_[block], plane_dst + (n / 2) * 4 * cs + (n % 2) * 4, cs);
    }
  }
}

// ---------------------------------------------------------------------------
// The loop filter (RFC 6386 section 15). It runs after a macroblock row has
// been reconstructed, in the order libvpx uses, because a filter reads what its
// neighbours left behind and the order is part of the output.
// ---------------------------------------------------------------------------
namespace {
struct FilterParams {
  int limit;  ///< `2 * level + ilevel`, the edge threshold
  int ilevel;
  int hev;
};

bool needs_filter(const uint8_t* p, int step, int threshold) {
  const int p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step];
  return 4 * abs0(p0 - q0) + abs0(p1 - q1) <= threshold;
}

bool needs_filter2(const uint8_t* p, int step, int threshold, int inner) {
  const int p3 = p[-4 * step], p2 = p[-3 * step], p1 = p[-2 * step];
  const int p0 = p[-step], q0 = p[0];
  const int q1 = p[step], q2 = p[2 * step], q3 = p[3 * step];
  if (4 * abs0(p0 - q0) + abs0(p1 - q1) > threshold) return false;
  return abs0(p3 - p2) <= inner && abs0(p2 - p1) <= inner && abs0(p1 - p0) <= inner &&
         abs0(q3 - q2) <= inner && abs0(q2 - q1) <= inner && abs0(q1 - q0) <= inner;
}

bool high_edge_variance(const uint8_t* p, int step, int threshold) {
  const int p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step];
  return abs0(p1 - p0) > threshold || abs0(q1 - q0) > threshold;
}

void filter2(uint8_t* p, int step) {  // four pixels in, two out
  const int p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step];
  const int a = 3 * (q0 - p0) + clip_int(p1 - q1, -128, 127);
  const int a1 = clip_signed2((a + 4) >> 3);
  const int a2 = clip_signed2((a + 3) >> 3);
  p[-step] = clip8(p0 + a2);
  p[0] = clip8(q0 - a1);
}

void filter4(uint8_t* p, int step) {  // four in, four out
  const int p1 = p[-2 * step], p0 = p[-step], q0 = p[0], q1 = p[step];
  const int a = 3 * (q0 - p0);
  const int a1 = clip_signed2((a + 4) >> 3);
  const int a2 = clip_signed2((a + 3) >> 3);
  const int a3 = (a1 + 1) >> 1;
  p[-2 * step] = clip8(p1 + a3);
  p[-step] = clip8(p0 + a2);
  p[0] = clip8(q0 - a1);
  p[step] = clip8(q1 - a3);
}

void filter6(uint8_t* p, int step) {  // six in, six out
  const int p2 = p[-3 * step], p1 = p[-2 * step], p0 = p[-step];
  const int q0 = p[0], q1 = p[step], q2 = p[2 * step];
  const int a = clip_int(3 * (q0 - p0) + clip_int(p1 - q1, -128, 127), -128, 127);
  const int a1 = (27 * a + 63) >> 7;
  const int a2 = (18 * a + 63) >> 7;
  const int a3 = (9 * a + 63) >> 7;
  p[-3 * step] = clip8(p2 + a3);
  p[-2 * step] = clip8(p1 + a2);
  p[-step] = clip8(p0 + a1);
  p[0] = clip8(q0 - a1);
  p[step] = clip8(q1 - a2);
  p[2 * step] = clip8(q2 - a3);
}

// `hstride` walks across the edge, `vstride` along it.
void filter_loop(bool outer, uint8_t* p, int hstride, int vstride, int size,
                 const FilterParams& params) {
  const int threshold = 2 * params.limit + 1;
  while (size-- > 0) {
    if (needs_filter2(p, hstride, threshold, params.ilevel)) {
      if (high_edge_variance(p, hstride, params.hev)) {
        filter2(p, hstride);
      } else if (outer) {
        filter6(p, hstride);
      } else {
        filter4(p, hstride);
      }
    }
    p += vstride;
  }
}

void simple_loop(uint8_t* p, int hstride, int vstride, int size, int level) {
  const int limit = 2 * level + 1;
  while (size-- > 0) {
    if (needs_filter(p, hstride, limit)) filter2(p, hstride);
    p += vstride;
  }
}
}  // namespace

void FrameDecoder::filter_macroblock(int mb_x, int mb_y) {
  if (filter_level_ == 0) return;
  const MbFilterInfo& info =
      filter_info_[static_cast<size_t>(mb_y) * static_cast<size_t>(mb_width_) + static_cast<size_t>(mb_x)];
  if (info.limit == 0) return;
  const FilterStrength strength{info.limit, info.ilevel, info.hev};
  const bool inner = info.inner;

  Vp8Decoder::Frame& f = *frame_;
  const int ys = f.y_stride;
  const int cs = f.uv_stride;
  const int x0 = 16 * mb_x;
  const int y0 = 16 * mb_y;
  uint8_t* y = f.y.data() + (y0 + Vp8Decoder::kBorder) * ys + (x0 + Vp8Decoder::kBorder);
  uint8_t* u = f.u.data() + (y0 / 2 + Vp8Decoder::kBorder) * cs + (x0 / 2 + Vp8Decoder::kBorder);
  uint8_t* v = f.v.data() + (y0 / 2 + Vp8Decoder::kBorder) * cs + (x0 / 2 + Vp8Decoder::kBorder);

  if (filter_simple_) {
    // The simple filter touches only luma, and only the two nearest pixels on
    // each side of an edge.
    const int outer_level = strength.limit + 4;
    if (mb_x > 0) simple_loop(y, 1, ys, 16, outer_level);
    if (inner) {
      for (int k = 1; k < 4; ++k) simple_loop(y + 4 * k, 1, ys, 16, strength.limit);
    }
    if (mb_y > 0) simple_loop(y, ys, 1, 16, outer_level);
    if (inner) {
      for (int k = 1; k < 4; ++k) simple_loop(y + 4 * k * ys, ys, 1, 16, strength.limit);
    }
    return;
  }

  const FilterParams outer{strength.limit + 4, strength.ilevel, strength.hev_threshold};
  const FilterParams inside{strength.limit, strength.ilevel, strength.hev_threshold};
  if (mb_x > 0) {  // the left macroblock edge
    filter_loop(true, y, 1, ys, 16, outer);
    filter_loop(true, u, 1, cs, 8, outer);
    filter_loop(true, v, 1, cs, 8, outer);
  }
  if (inner) {
    for (int k = 1; k < 4; ++k) filter_loop(false, y + 4 * k, 1, ys, 16, inside);
    // Chroma is eight pixels wide, so it has a single inner edge.
    filter_loop(false, u + 4, 1, cs, 8, inside);
    filter_loop(false, v + 4, 1, cs, 8, inside);
  }
  if (mb_y > 0) {  // the top macroblock edge
    filter_loop(true, y, ys, 1, 16, outer);
    filter_loop(true, u, cs, 1, 8, outer);
    filter_loop(true, v, cs, 1, 8, outer);
  }
  if (inner) {
    for (int k = 1; k < 4; ++k) filter_loop(false, y + 4 * k * ys, ys, 1, 16, inside);
    filter_loop(false, u + 4 * cs, cs, 1, 8, inside);
    filter_loop(false, v + 4 * cs, cs, 1, 8, inside);
  }
}

// ---------------------------------------------------------------------------
// The decode itself.
// ---------------------------------------------------------------------------
Vp8Decoder::Status FrameDecoder::run(const uint8_t* data, size_t size, Vp8Decoder::Frame& frame,
                                     std::string& error) {
  const Vp8Decoder::Status status = parse_header(data, size, error);
  if (status != Vp8Decoder::Status::Ok) return status;

  frame_ = &frame;
  frame.width = width_;
  frame.height = height_;
  frame.mb_width = mb_width_;
  frame.mb_height = mb_height_;
  frame.y_stride = width_ + 2 * Vp8Decoder::kBorder;
  frame.uv_stride = (width_ + 1) / 2 + 2 * Vp8Decoder::kBorder;
  const int chroma_height = (height_ + 1) / 2;
  const size_t y_size = static_cast<size_t>(frame.y_stride) * static_cast<size_t>(height_ + 2 * Vp8Decoder::kBorder);
  const size_t c_size = static_cast<size_t>(frame.uv_stride) * static_cast<size_t>(chroma_height + 2 * Vp8Decoder::kBorder);
  frame.y.assign(y_size, 0);
  frame.u.assign(c_size, 0);
  frame.v.assign(c_size, 0);

  // The border: 127 above (and above-right), 129 to the left. A block with no
  // neighbours then predicts 128 without a special case, and the diagonal 4x4
  // predictors read defined values everywhere.
  for (int i = 0; i < Vp8Decoder::kBorder; ++i) {
    std::memset(frame.y.data() + static_cast<size_t>(i) * frame.y_stride, 127, frame.y_stride);
    std::memset(frame.u.data() + static_cast<size_t>(i) * frame.uv_stride, 127, frame.uv_stride);
    std::memset(frame.v.data() + static_cast<size_t>(i) * frame.uv_stride, 127, frame.uv_stride);
  }
  for (int row = 0; row < height_ + Vp8Decoder::kBorder; ++row) {
    frame.y.data()[static_cast<size_t>(row + Vp8Decoder::kBorder) * frame.y_stride + Vp8Decoder::kBorder - 1] = 129;
  }
  for (int row = 0; row < chroma_height + Vp8Decoder::kBorder; ++row) {
    frame.u.data()[static_cast<size_t>(row + Vp8Decoder::kBorder) * frame.uv_stride + Vp8Decoder::kBorder - 1] = 129;
    frame.v.data()[static_cast<size_t>(row + Vp8Decoder::kBorder) * frame.uv_stride + Vp8Decoder::kBorder - 1] = 129;
  }

  mb_row_.assign(static_cast<size_t>(mb_width_), MbInfo{});
  intra_above_.assign(static_cast<size_t>(4 * mb_width_), kDcPred);
  std::fill(intra_above_.begin(), intra_above_.end(), static_cast<uint8_t>(kDcPred));
  nz_above_y_.assign(static_cast<size_t>(4 * mb_width_), 0);
  nz_above_y2_.assign(static_cast<size_t>(mb_width_), 0);
  nz_above_u_.assign(static_cast<size_t>(2 * mb_width_), 0);
  nz_above_v_.assign(static_cast<size_t>(2 * mb_width_), 0);
  filter_info_.assign(static_cast<size_t>(mb_width_) * static_cast<size_t>(mb_height_), MbFilterInfo{});

  const int partitions = static_cast<int>(token_parts_.size());
  for (int mb_y = 0; mb_y < mb_height_; ++mb_y) {
    tokens_ = &token_parts_[static_cast<size_t>(mb_y % partitions)];
    nz_left_y_[0] = nz_left_y_[1] = nz_left_y_[2] = nz_left_y_[3] = 0;
    nz_left_y2_ = 0;
    nz_left_u_[0] = nz_left_u_[1] = nz_left_v_[0] = nz_left_v_[1] = 0;
    parse_mode_row();
    if (modes_.overrun()) {
      error = "the VP8 mode stream ended in the middle of a row";
      return Vp8Decoder::Status::Truncated;
    }
    for (int mb_x = 0; mb_x < mb_width_; ++mb_x) {
      if (!parse_macroblock(mb_x)) {
        error = "the VP8 coefficient stream ended in the middle of a row";
        return Vp8Decoder::Status::Truncated;
      }
      reconstruct(mb_x, mb_y);
      const MbInfo& mb = mb_row_[static_cast<size_t>(mb_x)];
      const FilterStrength& strength = filter_strengths_[mb.segment][mb.is_4x4 ? 1 : 0];
      MbFilterInfo& info =
          filter_info_[static_cast<size_t>(mb_y) * static_cast<size_t>(mb_width_) + static_cast<size_t>(mb_x)];
      info.limit = static_cast<uint8_t>(strength.limit);
      info.ilevel = static_cast<uint8_t>(strength.ilevel);
      info.hev = static_cast<uint8_t>(strength.hev_threshold);
      // The inner edges are filtered for macroblocks coded with 4x4 modes and
      // for macroblocks that carried residuals at all.
      info.inner = mb.is_4x4 || mb.coded;
    }
  }
  for (int mb_y = 0; mb_y < mb_height_; ++mb_y) {
    for (int mb_x = 0; mb_x < mb_width_; ++mb_x) filter_macroblock(mb_x, mb_y);
  }
  return Vp8Decoder::Status::Ok;
}

}  // namespace

// ---------------------------------------------------------------------------
// The public surface.
// ---------------------------------------------------------------------------
bool Vp8Decoder::is_vp8(const uint8_t* data, size_t size) {
  return data != nullptr && size >= 10 && data[3] == 0x9d && data[4] == 0x01 && data[5] == 0x2a;
}

Vp8Decoder::Status Vp8Decoder::decode(const uint8_t* data, size_t size, Frame& frame,
                                      std::string& error) {
  frame = Frame{};
  if (data == nullptr || size == 0) {
    error = "there are no bytes to decode";
    return Status::Truncated;
  }
  FrameDecoder decoder;
  return decoder.run(data, size, frame, error);
}

void Vp8Decoder::to_argb(const Frame& frame, const uint8_t* alpha, int alpha_stride,
                         std::vector<uint32_t>& out) {
  const int width = frame.width;
  const int height = frame.height;
  out.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0xFF000000u);
  if (width <= 0 || height <= 0) return;

  const auto y_row = [&](int y) {
    return frame.y.data() + static_cast<size_t>(y + kBorder) * frame.y_stride + kBorder;
  };
  const auto u_row = [&](int y) {
    return frame.u.data() + static_cast<size_t>(y + kBorder) * frame.uv_stride + kBorder;
  };
  const auto v_row = [&](int y) {
    return frame.v.data() + static_cast<size_t>(y + kBorder) * frame.uv_stride + kBorder;
  };

  // The chroma samples of a 4:2:0 image sit between pairs of luma rows, so the
  // converter works on two luma rows at a time: the "top" row of a pair is
  // weighted 3:1 towards the chroma row above it, and 1:3 on the bottom row,
  // with the same weights horizontally, and a 1/16 rounding term.
  const auto convert = [&](int luma, int row, int column, int u, int v) -> uint32_t {
    auto mul_hi = [](int value, int coefficient) { return (value * coefficient) >> 8; };
    auto clip = [](int value) -> int {
      constexpr int kMask = (256 << 6) - 1;
      if ((value & ~kMask) == 0) return value >> 6;
      return value < 0 ? 0 : 255;
    };
    const int r = clip(mul_hi(luma, 19077) + mul_hi(v, 26149) - 14234);
    const int g = clip(mul_hi(luma, 19077) - mul_hi(u, 6419) - mul_hi(v, 13320) + 8708);
    const int b = clip(mul_hi(luma, 19077) + mul_hi(u, 33050) - 17685);
    const uint8_t a =
        alpha != nullptr
            ? alpha[static_cast<size_t>(row) * alpha_stride + static_cast<size_t>(column)]
            : 0xFF;
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
  };

  const auto pair = [&](const uint8_t* top_y, const uint8_t* bottom_y, const uint8_t* top_u,
                        const uint8_t* top_v, const uint8_t* cur_u, const uint8_t* cur_v, int y_top,
                        int y_bottom) {
    uint32_t* top_dst = out.data() + static_cast<size_t>(y_top) * width;
    uint32_t* bottom_dst = bottom_y != nullptr ? out.data() + static_cast<size_t>(y_bottom) * width : nullptr;
    const int last_pair = (width - 1) >> 1;
    uint32_t tl_uv = static_cast<uint32_t>(top_u[0]) | (static_cast<uint32_t>(top_v[0]) << 16);
    uint32_t l_uv = static_cast<uint32_t>(cur_u[0]) | (static_cast<uint32_t>(cur_v[0]) << 16);
    {
      const uint32_t uv0 = (3 * tl_uv + l_uv + 0x00020002u) >> 2;
      top_dst[0] = convert(top_y[0], y_top, 0, static_cast<int>(uv0 & 0xff), static_cast<int>(uv0 >> 16));
      if (bottom_dst != nullptr) {
        const uint32_t uv1 = (3 * l_uv + tl_uv + 0x00020002u) >> 2;
        bottom_dst[0] = convert(bottom_y[0], y_bottom, 0, static_cast<int>(uv1 & 0xff), static_cast<int>(uv1 >> 16));
      }
    }
    for (int x = 1; x <= last_pair; ++x) {
      const uint32_t t_uv = static_cast<uint32_t>(top_u[x]) | (static_cast<uint32_t>(top_v[x]) << 16);
      const uint32_t uv = static_cast<uint32_t>(cur_u[x]) | (static_cast<uint32_t>(cur_v[x]) << 16);
      const uint32_t avg = tl_uv + t_uv + l_uv + uv + 0x00080008u;
      const uint32_t diag_12 = (avg + 2 * (t_uv + l_uv)) >> 3;
      const uint32_t diag_03 = (avg + 2 * (tl_uv + uv)) >> 3;
      {
        const uint32_t uv0 = (diag_12 + tl_uv) >> 1;
        const uint32_t uv1 = (diag_03 + t_uv) >> 1;
        top_dst[2 * x - 1] = convert(top_y[2 * x - 1], y_top, 2 * x - 1, static_cast<int>(uv0 & 0xff),
                                     static_cast<int>(uv0 >> 16));
        top_dst[2 * x - 0] = convert(top_y[2 * x - 0], y_top, 2 * x - 0, static_cast<int>(uv1 & 0xff),
                                     static_cast<int>(uv1 >> 16));
      }
      if (bottom_dst != nullptr) {
        const uint32_t uv0 = (diag_03 + l_uv) >> 1;
        const uint32_t uv1 = (diag_12 + uv) >> 1;
        bottom_dst[2 * x - 1] = convert(bottom_y[2 * x - 1], y_bottom, 2 * x - 1, static_cast<int>(uv0 & 0xff),
                                        static_cast<int>(uv0 >> 16));
        if (2 * x < width) {
          bottom_dst[2 * x + 0] = convert(bottom_y[2 * x + 0], y_bottom, 2 * x + 0, static_cast<int>(uv1 & 0xff),
                                          static_cast<int>(uv1 >> 16));
        }
      }
      tl_uv = t_uv;
      l_uv = uv;
    }
    if ((width & 1) == 0) {
      const uint32_t uv0 = (3 * tl_uv + l_uv + 0x00020002u) >> 2;
      top_dst[width - 1] = convert(top_y[width - 1], y_top, width - 1, static_cast<int>(uv0 & 0xff),
                                   static_cast<int>(uv0 >> 16));
      if (bottom_dst != nullptr) {
        const uint32_t uv1 = (3 * l_uv + tl_uv + 0x00020002u) >> 2;
        bottom_dst[width - 1] = convert(bottom_y[width - 1], y_bottom, width - 1, static_cast<int>(uv1 & 0xff),
                                        static_cast<int>(uv1 >> 16));
      }
    }
  };

  // Row 0 mirrors the first chroma row above and below it.
  pair(y_row(0), nullptr, u_row(0), v_row(0), u_row(0), v_row(0), 0, 1);
  // Row 1 onwards: a pair of luma rows sits between two chroma rows, so row y
  // is weighted towards chroma row (y - 1) / 2 and row y + 1 towards (y + 1) / 2.
  int y = 1;
  for (; y + 1 < height; y += 2) {
    const int chroma_top = (y - 1) / 2;
    const int chroma_bottom = (y + 1) / 2;
    pair(y_row(y), y_row(y + 1), u_row(chroma_top), v_row(chroma_top), u_row(chroma_bottom),
         v_row(chroma_bottom), y, y + 1);
  }
  if ((height & 1) == 0) {
    const int chroma = height / 2 - 1;
    pair(y_row(height - 1), nullptr, u_row(chroma), v_row(chroma), u_row(chroma), v_row(chroma),
         height - 1, height);
  }
}

const char* Vp8Decoder::status_string(Status status) {
  switch (status) {
    case Status::Ok:
      return "ok";
    case Status::NotKeyFrame:
      return "a VP8 frame that is not a key frame needs the frame before it";
    case Status::UnsupportedProfile:
      return "an undefined VP8 profile";
    case Status::Corrupt:
      return "the VP8 bitstream contradicts itself";
    case Status::Truncated:
      return "the VP8 bitstream ends in the middle of the frame";
    case Status::TooLarge:
      return "the VP8 frame is larger than the decoder budget";
  }
  return "an unknown VP8 error";
}

}  // namespace litebird
