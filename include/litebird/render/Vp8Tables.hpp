#pragma once

#include <cstdint>

// The tables VP8's bitstream is defined in terms of. They live in
// `src/render/Vp8Tables.cpp` with the RFC section each one comes from, and are
// not tunable: the arithmetic decoder cannot resynchronise, so one wrong
// probability turns the rest of the frame into noise.
namespace litebird::vp8 {

// Coefficient probabilities, indexed [block type][band][previous-coefficient
// context][node]. Block types: 0 = luma without a Y2 block (or after one),
// 1 = the Y2 block, 2 = chroma, 3 = luma coded as sixteen 4x4 blocks.
extern const uint8_t kDefaultCoeffProbs[4][8][3][11];
extern const uint8_t kCoeffUpdateProbs[4][8][3][11];

// Key-frame 4x4 intra-mode probabilities, indexed by the mode above and the
// mode to the left of the block being decoded.
extern const uint8_t kKeyframeBModeProbs[10][10][9];
extern const uint8_t kKeyframeYModeProb[4];
extern const uint8_t kKeyframeUvModeProb[3];
extern const uint8_t kBlockSizeProb;

// The tree over the ten 4x4 modes: entry `i` is either a leaf (the mode, which
// makes the walk stop) or the node to visit next if the bit is 0 or 1, at
// `2 * i` and `2 * i + 1`.
extern const int8_t kIntra4ModeTree[18];

// Coefficient band per scan position, the zig-zag order, and the fixed
// probabilities of the large-value categories.
extern const uint8_t kCoeffBands[17];
extern const uint8_t kZigzag[16];
extern const uint8_t kCat3[4];
extern const uint8_t kCat4[5];
extern const uint8_t kCat5[6];
extern const uint8_t kCat6[12];

// DC and AC quantiser steps, indexed by the quantiser index.
extern const uint8_t kDcTable[128];
extern const uint16_t kAcTable[128];

}  // namespace litebird::vp8
