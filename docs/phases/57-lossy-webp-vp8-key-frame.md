<!-- Moved out of ARCHITECTURE.md when new write-ups began going to docs/
     (docs/README.md); the phase table in ARCHITECTURE.md points here. -->

# Phase 57 — Lossy WebP: the `VP8` key frame, the `ALPH` plane, and the loop filter

Task row: **Lossy `VP8`, animated WebP, AVIF, and SVG `<text>`** — the lossy
half, which is now **Animated WebP, AVIF, and SVG `<text>`**. Finding 63.

## 1. What was wrong

Phase 54 read the container and the lossless (`VP8L`) bitstream, and refused
every `VP8 ` chunk as `UnsupportedLossy` — a status this phase retired along
with the refusal. That is the wrong half of the format to have: libwebp's
default output at quality 75 is lossy, and the files libwebp only writes on
request are the lossless ones, so `<img src="x.webp">` drew nothing for what
most encoders — `convert`, `cwebp`, every CDN — actually produce. The lossy
half is not a variation of `VP8L`: it is a VP8 *video* key frame, the intra
frame of a codec, which never refers to a picture before it and is therefore
self-contained. `AUDIT.md`, finding 63.

## 2. What changed, layer by layer

`include/litebird/render/Vp8Decoder.hpp` and `src/render/Vp8Decoder.cpp` decode
one key frame into the three planes libwebp reconstructs, with the constant
tables in `src/render/Vp8Tables.cpp`; `to_argb` turns the planes into the
`0xAARRGGBB` pixels `ImageData` expects. The layers, each one testable alone:

1. **Frame header.** The three-byte frame tag (key-frame bit, profile, the
   show-frame bit, the length of the first partition), the `9d 01 2a` start
   code, and 14-bit width and height. An inter-coded frame stops here as
   `NotKeyFrame` — it needs the picture before it, which a still image does not
   have — a profile above 3 as `UnsupportedProfile`, a zero dimension as
   `Corrupt`, and a frame past `kMaxDecodedPixels` or `kMaxDimension` as
   `TooLarge`, before anything is allocated.
2. **The boolean decoder.** VP8's arithmetic coder: `get_bool(p)` narrows a
   `range`/`value` pair against an 8-bit probability and renormalises a byte at
   a time; `get_literal(n)` and `get_signed_literal(n)` are built on it. The
   first partition carries the macroblock modes; the residual tokens live in
   one to eight partitions whose three-byte sizes are read from the start of
   the token area.
3. **Header probabilities.** Segmentation (four segments, a per-segment
   quantiser and filter-strength delta, an optional segment map with its own
   coded tree), the filter type/level/sharpness, the loop-filter deltas, the
   base quantiser with its five per-plane deltas, and the coefficient
   probabilities — `[4][8][3][11]` defaults, each with its own "is updated"
   bit.
4. **Per-macroblock headers.** Segment id, the skip-coefficients flag, the
   16x16 luma and 8x8 chroma prediction modes (four each, through their own
   tree), and, for a macroblock not predicted as 16x16, sixteen 4x4 modes
   chosen from ten through the `[10][10][9]` context table. The modes of a
   whole macroblock row are read before its residuals, because a 4x4
   macroblock's context is its left and above neighbours' modes.
5. **Residuals.** `get_coeffs` reads a block's tokens with the VP8 context
   (the above and left nonzero-coefficient counts), the coefficient bands
   (`kCoeffBands`), the eob / zero-run / "is ±1" probability triple, and the
   category tables for the larger values. Y2 — the 4x4 block of DC
   coefficients of a 16x16 macroblock — is coded first and either goes through
   the inverse Walsh–Hadamard transform or, when it carries a single
   coefficient, is spread as `(dc + 3) >> 3`; the sixteen luma blocks and eight
   chroma blocks then get the 4x4 IDCT with libwebp's fixed-point constants
   (`20091`/`35468` and a `+4` before the shift).
6. **Intra prediction.** 16x16 DC/V/H/TM, 8x8 chroma DC/V/H/TM with the 117
   clamp, and the ten 4x4 modes, all reading the 16-sample border the frame
   carries on every side, with a missing top or left edge handled the way
   `vp8/common/reconintra.h` does it — top-right replication by 4, 8 and 12
   samples included.
7. **The loop filter.** A per-macroblock level (`level >> sharpness` folded in,
   the inner edge limit `2 * level + ilevel`, and `+4` on macroblock edges, HEV
   limits at 40 and 15), applied in the order the bitstream prescribes:
   horizontal macroblock edges, horizontal inner edges, vertical macroblock
   edges, vertical inner edges, luma then chroma. It is a pass over the whole
   frame after every macroblock has been reconstructed — not work inside the
   macroblock loop — because a filtered edge is a source for its neighbours,
   which is the one structural mistake that still decodes to a picture and is
   wrong by a few levels per pixel.
8. **`to_argb`.** The "fancy" bilinear chroma upsampling (9/3/3/1 weights,
   `YUV_FIX2 = 6`) and the fixed-point BT.601 studio-swing matrix, then the
   optional alpha plane.

## 3. What changed in the container: `VP8X`, `ALPH`, and the alpha plane

A lossy image is either a simple file (one `VP8 ` chunk) or an extended one: a
`VP8X` canvas whose flag byte says what follows, an optional `ALPH` chunk, and
the `VP8 ` chunk. `ALPH` is one header byte — bits 0–1 choose raw (0) or a
signature-less `VP8L` stream (1), bits 2–3 the spatial filter, bits 4–5 the
preprocessing, bits 6–7 reserved — and then the plane, one byte a pixel per row,
filtered. The spatial filters are undone against the plane already rebuilt:
none, left, above, and `left + above - above-left` modulo 256, with every one of
them falling back to the horizontal filter on the first row — and the horizontal
filter itself starting from `prev[0]` rather than from zero, which is libwebp's
`HorizontalUnfilter` and not the textbook "predict from the left" reading.
Level-reduction preprocessing is refused as `UnsupportedAlpha`: undoing it needs
the quantisation table the encoder used, which the file does not carry.
Reserved bits set are `Corrupt`, and a plane that is not `width * height` bytes
is refused rather than half-applied.

## 4. The oracle, and what it cost

Nothing here can be "close enough": VP8's transforms, filters and the YUV matrix
are specified to the last fixed-point constant, so libwebp's output is the only
correct answer and the work is not finished until the pixels are equal. The
oracle was libwebp 1.2.4 itself: `dwebp -yuv` for the planes (60 sweep files,
`max |Δ| = 0` on every one, with `-nofilter` on both sides to separate the
reconstruction from the filter) and `convert f.webp -depth 8 rgba:-` for the
pixels. Every mismatch that the sweeps found was one of four: a probability
pointer read from the wrong context (a multi-megabyte "coefficient stream ended
mid-row"), a 4x4 predictor that used the wrong edge sample on an odd-sized
frame, a row-pair walk in `to_argb` that ran past the last row, and the
`ALPH` first-row rule above. What the sweeps could not see — the loop filter,
the token partitions, the segmentation, and the alpha plane, none of which a
flat gradient exercises — is what the conformance checks below build by hand.

## 5. How it is verified

- `make verify-media` → **88/88**, of which **26** are lossy WebP checks:
  11 generated streams (1x1, 3x3, 16x16, 17x13, 32x32, 33x17, 100x75, 129x97,
  16x64, 64x16, and 96x64 at quality 30) decoded **exactly** against ImageMagick
  — `max_diff = 0`, which the helper reports as 99.00 dB — the loop filter
  (off, strength 0/100, simple, sharpness 7; 3,072 pixels differ, so the check
  proves the filter runs and is not the identity), `partitions 2/3`,
  `segments 2/4`, `sns 0/100`, `method 6`, the alpha plane from both compression
  methods and all three filters (rebuilt forward by hand, then required equal and
  non-flat), a truncated payload refused, and determinism.
- `make fuzz-media` → **2,400 cases** (600 WebP) over the lossless seeds, seven
  new lossy encodes (partitions 2, filter-type 1, strength 100, alpha methods
  0 and 1) and seven hand-built `VP8X`+`ALPH`+`VP8 ` containers with hostile
  header bytes: 0 findings. A one-off run at 5,000 iterations a format
  (20,000 cases) was clean too.
- `make test` → **208 suites** (`206 → 208`):
  `webp_lossy_vp8_decodes_bit_exactly` (a flat 8x8 and a 16x16 plasma embedded
  as bytes, every pixel compared against libwebp's, `lossy` set, lossless not
  set, determinism, and `ImageDecoder::decode(bytes, "image/webp")` agreeing) and
  `webp_lossy_alpha_plane_and_container_consistency` (a `VP8X`+`ALPH`+`VP8 `
  file built chunk by chunk in the test, so its RIFF size is computed rather
  than copied; the raw plane `16 * y + x` read back through all four filters;
  a short plane, level-reduction preprocessing and reserved bits refused). The
  refusal matrix in `webp_unsupported_and_malformed_input_is_refused` now covers
  truncation at four points, an inter-coded frame (message contains
  "inter-coded", not "key frame"), and the container cases.
- `make asan`, and the rest of the scoreboard, below.

## 6. What is still open

Animated WebP (`ANIM`/`ANMF` compositing and disposal) and AVIF are still
refused with a status, and SVG `<text>` is still unrendered; the AGENTS.md row
now names exactly those three. Inside the codec: only key frames are decoded
(which is all a still image has), profiles 0–3 only (no encoder emits more), and
`ALPH` level-reduction is refused rather than approximated. `VP8X` canvas
scaling — a canvas larger than the image, which the format allows — is not
applied; the image is decoded at its own size.
