# 60. ✅ WebP was a hole in the image pipeline (Phase 54)

> Moved out of `AUDIT.md` — written there as a `## 60. ✅ WebP was a hole in the image pipeline (Phase 54)` section.
> The text below is the section as it stood, unchanged.


**The hole.** `ImageDecoder::decode_bytes` recognized PNG, JPEG, GIF, BMP and
PPM by their magic bytes and fell back to an SVG sniff. A `RIFF....WEBP` file
matched neither, so it returned `nullptr`: `<img src="x.webp">` — which is what
a page serving WebP through `<picture>`, a CDN or `canvas` hands the engine
today — laid out an empty box. Nothing failed; the image was simply not there.

**The fix.** `include/litebird/render/WebPDecoder.hpp` and
`src/render/WebPDecoder.cpp` read the container and the lossless (VP8L)
bitstream, with no dependency added:

- **Container.** `RIFF`/`WEBP`, a simple file with one `VP8L` chunk or an
  extended `VP8X` file (whose canvas is checked against the image), a chunk walk
  with RIFF padding, and a misreported RIFF size clamped rather than trusted.
- **Entropy layer.** A least-significant-bit-first bit reader, canonical prefix
  codes decoded bit by bit with the over-subscription check, both code-length
  forms (simple, and the 19-symbol code-length code with its repeat symbols),
  meta prefix codes (an *entropy image* of code-group indices), the colour
  cache (`(0x1e35a7bd * argb) >> (32 - bits)`), and LZ77 with the 120
  near-neighbour distance codes mapped back to scan-line distances.
- **The four transforms**, each described by a sub-image decoded recursively
  with the same reader and inverted in reverse order: predictor (14 modes, with
  the rightmost column's top-right pixel being the first pixel of the row being
  rebuilt), cross-colour (3.5-fixed-point per-channel deltas), subtract-green,
  and colour indexing with delta-coded palettes and pixel bundling (8, 4 or 2
  indices per byte for 2, 4 and 16 colours).

The rule worth recording: a prefix code with a **single symbol costs no bits at
all**. libwebp builds that table with `bits == 0`, so the symbol is implied and
the stream carries nothing for it; reading the specification's `code_lengths[symbol0]
= 1` literally and consuming one bit shifts every following read. The symptom
was a 1x1 image whose alpha literal decoded as "corrupt" while the palette entry
three bits later — `#123456` — came out right, which is what pointed at the
missing rule rather than at an off-by-one in the container.

**Refusals rather than wrong pictures.** Animated WebP (`ANIM`/`ANMF`) is
reported as `UnsupportedAnimation`, a short file as `Truncated`, and a dimension
or a decoded-pixel budget past the limit as `TooLarge`; lossy `VP8` was
`UnsupportedLossy` until Phase 57 decoded it and took the status out.
`WebPDecoder::probe()` returns the same classification without decoding.

**Verification.** `make verify-media` gained 30 checks (63 in total): 17
generated lossless images — palettes of 2, 4, 16 and 256 colours, alpha ramps
and a transparent disc, bilevel noise, 1x1, 7x3, 3x40, 129x3, a 300x20 repeat
that exercises LZ77, 200x150 and 256x256 — decoded **bit-exactly** in RGBA and
then in the PPM colour channels against libwebp 1.2.4 through ImageMagick
(lossless has no error budget), an extended `VP8X` container, five refusals
(lossy, animated, truncated, a JPEG, an empty file), four `probe()`
classifications, and a determinism check. `make fuzz-media` gained 600 WebP
mutation cases over hand-built hostile seeds (a 16384 x 16384 header, a lossy
chunk, an animation flag, a RIFF size past the buffer): 2,400 cases in total,
0 crashes. `make test` **193 → 195** suites —
`webp_lossless_decodes_every_transform` (five fixtures embedded as bytes, every
pixel compared against libwebp's decode, plus an
`<img src="data:image/webp;base64,…">` end-to-end check that the box takes the
decoded 9 x 5 intrinsic size) and
`webp_unsupported_and_malformed_input_is_refused` (lossy, animated, five
truncation points, a container with no image chunk, a null pointer, a
dimension bomb, a header-only stream). The rest of the scoreboard is unchanged:
`make conformance` 1792/1792, `make dom-conformance` 669/669,
`make js-conformance` 974/989, `make url-conformance` 1188/1188,
`make xml-conformance` 1718/1718, `make wpt` 7998/8009, `make diff-trees` 0
differ, `make diff-xml` 0 DIFFER and 3000/3000 round trips, `make deep` 1332
runs 0 failed, `make asan` green.

**Still open.** Animated WebP (frame compositing and disposal) and AVIF (an
AV1 still image, a different codec entirely). The lossy `VP8` codec and the
`ALPH` chunk that only accompanies a lossy image were closed by Phase 57:
[`docs/findings/63-lossy-webp-vp8-refused.md`](docs/findings/63-lossy-webp-vp8-refused.md).

---
