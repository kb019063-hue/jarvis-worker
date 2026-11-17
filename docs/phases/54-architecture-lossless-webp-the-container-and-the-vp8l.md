# Phase 54 Architecture: Lossless WebP — the Container and the `VP8L` Bitstream

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 54 Architecture: Lossless WebP — the Container and the `VP8L` Bitstream` section.
> The text below is the section as it stood, unchanged.


### 1. The hole

`ImageDecoder` dispatched on magic bytes: PNG, JPEG, GIF, BMP, PPM, then an SVG
sniff. WebP has none of those signatures, so the function returned `nullptr` and
every page serving `.webp` — the default for most modern `<picture>` sources and
image CDNs — drew an empty box. `AUDIT.md`, finding 60.

### 2. Layers, top to bottom

`src/render/WebPDecoder.cpp` is four layers, each one testable on its own:

1. **`read_chunks`** — walks `RIFF`/`WEBP` chunks with RIFF padding, accepts a
   simple file (one `VP8L`) or an extended one (`VP8X` canvas, then the image),
   and clamps a RIFF size larger than the buffer instead of trusting it. An
   extended file whose animation flag is set, or which carries `ANIM`/`ANMF`,
   stops here as `UnsupportedAnimation`; an empty chunk list as `Corrupt`. A
   `VP8 ` chunk stops here too until Phase 57, which decodes it, and an `ALPH`
   chunk with it.
2. **`BitReader`** — least-significant-bit-first, a five-byte window, and an
   explicit end-of-stream latch so that reads past the end are reported as
   truncated instead of returning fabricated zero bits.
3. **`HuffmanTree`** — canonical codes built from code lengths, with the
   over-subscription check. `single_symbol_` is the important state: a
   one-symbol prefix code has code length 0 in libwebp (`bits = 0`), so it
   consumes **no bits at all**. Reading the spec's `code_lengths[symbol0] = 1`
   literally shifts the rest of the stream by one bit per use.
4. **`Vp8lDecoder`** — image header (`0x2f`, 14-bit width-1/height-1, alpha
   hint, 3-bit version), the transform list, the per-level prefix groups with a
   1-bit colour-cache flag and 4-bit cache size, meta prefix codes, the pixel
   loop, and the four inverse transforms.

### 3. The five alphabets and the pixel loop

Every level decodes with the same five prefix groups: green (`256 + 24 + cache
size` symbols: literals, then the length/distance prefixes packed in the high
byte), red, blue, alpha (256 each), and distance (40 symbols). A pixel is a
`green` symbol: `>= 256` means a length/distance copy — the length is
`prefix < 4 ? prefix + 1 : ((2 + (prefix & 1)) << ((prefix - 2) >> 1)) + bits + 1`
and the distance comes from the 40-symbol code, with the first 120 values mapped
back to 2-D offsets by `kCodeToPlane` and larger ones by `code - 120`.

### 4. The four transforms

The transforms are self-similar: the predictor, cross-colour, subtract-green and
colour-indexing descriptions are each a *sub-image*, decoded by recursing into
the same decoder. They are inverted in reverse order once the entropy-decoded
image exists. Predictor mode 0 of the first column reads the pixel to the left
(wrapping to the row above), the rightmost column's top-right pixel is the first
pixel of the row being rebuilt, and the borders use the `0xff000000` /
left / top defaults that `lossless.c` uses — three rules that a from-scratch
implementation gets wrong long before it gets the Huffman layer wrong.

### 5. Verification

- `make verify-media` → **63/63**, of which 30 are WebP: 17 lossless images
  decoded bit-exactly in RGBA and in the PPM channels against libwebp through
  ImageMagick, a `VP8X` container, five refusals, four `probe()`
  classifications, determinism.
- `make fuzz-media` → **2,400 cases** (600 WebP) over hostile seeds: a 16384 x
  16384 header, a lossy chunk, animation flags, RIFF sizes past the buffer.
- `make test` → **195 suites** (`193 → 195`): every pixel of five embedded
  fixtures, the `data:` URI path, and the `<img>` box taking the decoded
  intrinsic size; plus the refusal matrix.
- `make asan` green; `make conformance`/`dom`/`js`/`url`/`xml`/`wpt`, `diff-trees`,
  `diff-xml` and `deep` are untouched by the change and re-run below.

### 6. What is still open

Lossy `VP8` (macroblock prediction, DCT, loop filter — a video codec, not a
variation), animated WebP (`ANMF` compositing and disposal), the `ALPH` chunk
that only accompanies a lossy image, and AVIF (an AV1 still image). SVG
`<text>` remains open too. The AGENTS.md row now names exactly these. *Phase 57
closed the first and third of them — `VP8` key frames and the `ALPH` plane — in
`src/render/Vp8Decoder.cpp`; the write-up moved to
[`docs/phases/57-lossy-webp-vp8-key-frame.md`](docs/phases/57-lossy-webp-vp8-key-frame.md)
when new write-ups stopped going into this document.*

---
