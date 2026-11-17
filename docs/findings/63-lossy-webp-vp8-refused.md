<!-- Moved out of AUDIT.md when new write-ups began going to docs/
     (docs/README.md); AUDIT.md keeps the open findings. -->

# Finding 63 — A lossy WebP drew nothing: the `VP8` key frame was refused

Phase 57. The architecture is in
[`docs/phases/57-lossy-webp-vp8-key-frame.md`](../phases/57-lossy-webp-vp8-key-frame.md).

## 1. What was wrong

Phase 54 read the `RIFF`/`WEBP` container and the lossless
(`VP8L`) bitstream, and answered a `VP8 ` chunk with `UnsupportedLossy`. That is
the wrong half of the format to have shipped: libwebp's default output at
quality 75 is lossy, and the lossless files Phase 54 could read are the ones
`cwebp` writes only when asked. So `<img src="x.webp">` — what `convert`,
`cwebp` at its defaults, and every image CDN hand a page — laid out an empty
box, in silence, on the commonest kind of WebP in the world.

## 2. What changed
 `include/litebird/render/Vp8Decoder.hpp`, `src/render/Vp8Decoder.cpp`
and `src/render/Vp8Tables.cpp` (the constant tables: coefficient
probabilities, the key-frame mode trees, coefficient bands, quantiser and
category tables, zig-zag), with the container half in
`src/render/WebPDecoder.cpp`:

- **Header and coder.** The three-byte frame tag, the `9d 01 2a` start code,
  14-bit width and height; an inter-coded frame is `NotKeyFrame`, a profile
  above 3 `UnsupportedProfile`, a frame past the pixel budget `TooLarge`
  before anything is allocated. The boolean decoder is VP8's arithmetic coder,
  with literals and signed literals on top of it; the token partitions (1, 2, 4
  or 8, sized by three bytes each in the first partition) are separate readers
  over the same buffer.
- **Header probabilities.** Segmentation (four segments with per-segment
  quantiser and filter deltas and an optional coded segment map), the filter
  type/level/sharpness and the loop-filter deltas, the base quantiser with its
  five per-plane deltas, and the `[4][8][3][11]` coefficient probabilities with
  an "is updated" bit each.
- **Macroblock headers.** Mode rows are read a whole row at a time before that
  row's residuals: a 16x16 luma and 8x8 chroma mode from four, or sixteen 4x4
  modes from ten through the `[10][10][9]` context table, whose context is the
  modes of the macroblocks to the left and above.
- **Residuals.** The eob / zero-run / "is ±1" probability triple per
  coefficient band, the category tables for the larger values, the above and
  left nonzero-coefficient contexts, then Y2 (the DC block of a 16x16
  macroblock — inverse Walsh–Hadamard, or `(dc + 3) >> 3` spread when it holds
  one coefficient) and the 4x4 IDCT with libwebp's fixed-point constants.
- **Prediction.** 16x16 DC/V/H/TM, 8x8 chroma DC/V/H/TM with the 117 clamp, and
  the ten 4x4 modes, all reading the 16-sample border the frame carries, with
  top-right replication by 4, 8 and 12 samples and the missing-edge rules from
  `reconintra.h`.
- **The loop filter** as a pass over the whole frame after every macroblock is
  reconstructed (a filtered edge is a source for its neighbours, so doing it
  inside the macroblock loop decodes to a picture that is plausible and wrong):
  the level folded by sharpness, the inner edge limit `2 * level + ilevel`,
  `+4` on macroblock edges, HEV limits at 40 and 15, in the bitstream's order —
  horizontal macroblock edges, horizontal inner, vertical macroblock, vertical
  inner, luma then chroma.
- **`to_argb`.** Fancy (bilinear 9/3/3/1) chroma upsampling and the fixed-point
  BT.601 studio-swing matrix, then the alpha plane.
- **The container.** A `VP8 ` chunk in a simple file or in a `VP8X` file whose
  optional `ALPH` chunk carries the alpha plane: one header byte (compression
  raw or signature-less `VP8L`, one of three spatial filters, preprocessing,
  two reserved bits), then the plane. Level-reduction preprocessing is
  `UnsupportedAlpha` — undoing it needs the quantiser the encoder used, which
  the file does not carry — a plane that is not `width * height` bytes is
  `Corrupt`, and reserved bits set are `Corrupt`. `UnsupportedLossy` is gone
  from the `Status` enum, and `probe()` no longer classifies a lossy file as a
  refusal.

## 2b. What the oracle taught
 Nothing here has an error budget: VP8's transforms,
filters and colour matrix are specified down to their fixed-point constants, so
libwebp's pixels are the only right answer. The oracle was libwebp 1.2.4 —
`dwebp -yuv` for the planes (60 sweep files, `max |Δ| = 0` on every one, with
`-nofilter` on both sides to separate reconstruction from filtering) and
`convert f.webp -depth 8 rgba:-` for the pixels. Four faults accounted for every
mismatch the sweeps found: a probability pointer taken from the wrong context
(the symptom was a multi-megabyte "coefficient stream ended mid-row"), a 4x4
predictor reading the wrong edge sample at odd frame sizes (17x13, 33x17), a
row-pair walk in `to_argb` that ran past the last row, and the horizontal
`ALPH` unfilter starting from zero where libwebp starts from `prev[0]`.

## 3. How it is verified
 `make verify-media` **88/88**, of which 26 checks are new and
all lossy: 11 generated streams (1x1, 3x3, 16x16, 17x13, 32x32, 33x17, 100x75,
129x97, 16x64, 64x16, 96x64 at quality 30) decoded **pixel for pixel equal** to
ImageMagick's — `max_diff = 0` — the loop filter (off; strength 0 and 100;
simple; sharpness 7; 3,072 pixels differ from the unfiltered output, so the
check cannot pass by the filter being the identity), `partitions 2/3`,
`segments 2/4`, `sns 0/100`, `method 6`, an alpha plane from both compression
methods and all three spatial filters (rebuilt forward by hand and then
required equal and non-flat), a truncated payload refused, and determinism.
`make test` **206 → 208**:
`webp_lossy_vp8_decodes_bit_exactly` (a flat 8x8 and a 16x16 plasma as embedded
bytes, every pixel against libwebp's, the `lossy` flag set and not set for a
lossless file, determinism, and `ImageDecoder::decode(bytes, "image/webp")`
agreeing) and `webp_lossy_alpha_plane_and_container_consistency` (a
`VP8X`+`ALPH`+`VP8 ` file built chunk by chunk, so its RIFF size is computed
rather than copied; the alpha plane `16 * y + x` read back through all four
filters; a short plane, level-reduction and reserved bits refused); the
refusal matrix in `webp_unsupported_and_malformed_input_is_refused` now covers
truncation at four points and an inter-coded frame instead of the lossy
refusal. `make fuzz-media` gains seven lossy seeds (partitions 2, filter-type 1,
strength 100, alpha methods 0 and 1) and seven hand-built `VP8X`+`ALPH`+`VP8 `
containers with hostile header bytes: **2,400 cases**, 0 findings (a one-off
5,000-iterations-a-format run, 20,000 cases, was clean too). The rest of the
scoreboard: `make conformance` 1792/1792, `make js-conformance` 1003/1017,
`make dom-conformance` 669/669, `make url-conformance` 1188/1188,
`make xml-conformance` 1718/1718, `make wpt` 9538/9744 subtests, `make
diff-trees` 0 differ, `make diff-xml` 0 DIFFER and 3,000/3,000 round trips,
`make deep` 1,332 runs 0 failed, `make fuzz` 2,400 media cases and 5,008 colour
cases 0 findings, `make asan` green (the 203 suites as the branch then stood,
the 88 media checks, JS
1,002/1,017 against a floor of 1,001, DOM 669/669, XML 1,718/1,718, WPT
9,538/9,744 and the sanitizer fuzzers, all under ASan+UBSan; `asan-deep` 882
runs 0 failed).

## 4. What is still open
 Animated WebP (`ANIM`/`ANMF` compositing and disposal), AVIF (an
AV1 still image), and SVG `<text>`. Inside the codec: only key frames (all a
still image has), profiles 0–3 (no encoder emits more), `ALPH` level-reduction
refused rather than approximated, and a `VP8X` canvas larger than the image
decoded at the image's own size rather than scaled up (the format allows both;
an image larger than its canvas is refused).
