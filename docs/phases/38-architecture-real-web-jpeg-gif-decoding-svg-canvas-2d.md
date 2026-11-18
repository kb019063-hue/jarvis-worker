# Phase 38 Architecture: Real-Web JPEG/GIF Decoding, SVG & Canvas 2D

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 38 Architecture: Real-Web JPEG/GIF Decoding, SVG & Canvas 2D` section.
> The text below is the section as it stood, unchanged.


Phase 38 replaces Litebird's toy vector/canvas rasterizers and fills the missing
real-web raster image decoders (JPEG and GIF) with zero-dependency C++20
implementations verified pixel-by-pixel against `libjpeg` and ImageMagick.

### 1. Standalone ITU-T T.81 JPEG Decoder (`JpegDecoder.hpp`, `src/render/JpegDecoder.cpp`)

- **Markers & frame modes:** `SOI`/`EOI`, `DQT` (8-bit and 16-bit), `DHT`
  (9-bit fast lookup table with 16-bit bit-by-bit fallback and over-subscription
  checks), `DRI` / `RST0`–`RST7`, `APP0` (JFIF), `APP14` (Adobe color transform),
  `SOF0` (Baseline DCT), `SOF1` (Extended Sequential DCT), and `SOF2`
  (Progressive DCT).
- **Progressive scans:** Maintains per-component DCT coefficient buffers across
  arbitrary scan scripts: interleaved/non-interleaved DC first (`Ah=0`) and DC
  refinement (`Ah>0`), AC first with EOB runs (`EOB0`–`EOB14`, `ZRL`), and AC
  successive-approximation refinement.
- **IDCT, chroma upsampling & color:** Separable double-precision 8×8 IDCT with
  DC-only row/column fast paths, centered `[3/4, 1/4]` triangle chroma
  upsampling (4:4:4, 4:2:2, 4:2:0, 4:4:0, odd dimensions), and Grayscale,
  YCbCr, RGB, CMYK, and Adobe YCCK conversions.
- **Security bounds:** Rejects streams exceeding `kMaxDecodedPixels` (16M px) or
  `kMaxScans` (64), checks all segment lengths and Huffman trees, and verifies
  against `RasterBudget`.

### 2. Standalone GIF87a / GIF89a Decoder (`GifDecoder.hpp`, `src/render/GifDecoder.cpp`)

- **LZW & palettes:** Full 2..8-bit variable-width LZW (up to 12-bit codes) with
  deferred clear-code support when the 4096-entry table fills, Global and Local
  Color Tables, and 4-pass GIF interlacing.
- **Animation & disposal:** Graphic Control Extensions (transparency index,
  centisecond frame delays, all four disposal methods: `None`, `DoNotDispose`,
  `RestoreBackground`, `RestorePrevious`), `NETSCAPE2.0` loop count, and
  sub-rectangle compositing onto a persistent canvas buffer.
- **Bounds:** Enforces `kMaxDecodedPixels` and `kMaxFrames` (default 256).

### 3. Standalone Lossless WebP Decoder (`WebPDecoder.hpp`, `src/render/WebPDecoder.cpp`)

- **Container:** `RIFF`/`WEBP` with a padding-aware chunk walk, a simple file
  holding one `VP8L` chunk, one `VP8 ` (lossy) chunk, or an extended `VP8X`
  file whose canvas is checked against the image and whose optional `ALPH`
  chunk carries the alpha plane; `ANIM`/`ANMF` (animation), a RIFF size past
  the buffer, and a container with no image chunk each get their own `Status`
  (`UnsupportedAnimation`, `UnsupportedAlpha`, `Truncated`, `Corrupt`) instead
  of a null image.
- **Bitstream:** LSB-first bit reader with an explicit end-of-stream latch;
  canonical prefix codes (simple codes, and code-length codes with repeat
  symbols) with a **single-symbol code that consumes zero bits**, the rule that
  silently breaks every stream when it is missed; meta prefix codes (an entropy
  image selects one of up to 256 prefix groups per 16-pixel tile), the
  32-entry colour cache, and LZ77 with the 120 near-neighbour distance codes
  mapped onto scan-line distances.
- **Transforms:** predictor (14 modes, with the rightmost column's top-right
  pixel taken from the row being rebuilt), cross-colour (3.5-fixed-point
  per-channel delta), subtract-green, and colour-indexing (delta-coded palettes
  of up to 256 entries, with 8/4/2 indices packed per byte and a zero tail).
  Each is described by a sub-image decoded with the same reader, and inverted in
  reverse order after the pixel loop — which is also why decoding is recursive
  and the pixel budget is charged once, up front.
- **Bounds:** `kMaxDecodedPixels` (64M px) and `kMaxDimension` (16384) are
  checked from the 14-bit header before any allocation.

### 4. Upgraded Canvas 2D (`Canvas2D.hpp`, `src/render/Canvas2D.cpp`)

- **Transform & state stack:** Full 2D affine Current Transformation Matrix
  (`TransformMatrix2D`) with `save()`/`restore()`, `translate()`, `rotate()`,
  `scale()`, `transform()`, `setTransform()`, `resetTransform()`, `globalAlpha`,
  and an intersectable alpha clip mask (`clip()`).
- **Scanline rasterizer & stroking:** 4× vertical + analytical horizontal
  anti-aliased scanline fill supporting `FillRule::NonZero` and
  `FillRule::EvenOdd`; polygon-ribbon thick stroking with `butt`/`round`/
  `square` caps, `miter`/`bevel`/`round` joins, dash arrays (`setLineDash`/
  `getLineDash`/`lineDashOffset`), and `stroke_gradient`.
- **Paths, gradients, pixels & JS:** `roundRect`, `ellipse`, `arcTo`, `rect`,
  `isPointInPath`, inverse-CTM linear/radial gradients with `Pad`/`Reflect`/
  `Repeat` spread modes, `createImageData`/`getImageData`/`putImageData`,
  CTM-aware `drawImage`, and real JS property accessors via
  `JsValue::define_accessor` (`globalAlpha`, `lineCap`, `lineJoin`,
  `miterLimit`, `lineDashOffset`).

### 5. Real-World SVG Renderer (`SvgRenderer.hpp`, `src/render/SvgRenderer.cpp`)

- **DOM & CSS integration:** Iterative ID indexing (safe on 200,000-deep DOMs),
  `<defs>` exclusion, presentation attributes + inline `style` parsing,
  `#RGBA`/`#RRGGBBAA`/`rgba()` colors, `currentColor` inheritance, `opacity` /
  `fill-opacity` / `stroke-opacity`, `fill-rule`, `display:none` /
  `visibility:hidden`.
- **Geometry & paint servers:** Full SVG path grammar including W3C F.6.5/F.6.6
  `A/a` elliptical arcs, `S/s`/`T/t` smooth curves, compact arc flags, and
  scientific notation; `viewBox` + `preserveAspectRatio` (`meet`/`slice`/`none`);
  full `transform` list stack; `<linearGradient>`/`<radialGradient>` with `href`
  inheritance, `objectBoundingBox`/`userSpaceOnUse`, `gradientTransform`, and
  `spreadMethod`; `<use>` (with recursion/cycle guard) + `<symbol>`;
  `<clipPath>`; rounded `<rect rx ry>`; nested `<svg>`.

### 6. Verification & what remains open

`make verify-media` compares 21 generated JPEGs against `libjpeg` (52–99 dB
PSNR, MAE ≤ 0.39), 9 single/animated GIFs against ImageMagick `-coalesce`
(exact `max_diff = 0` on every frame), 27 lossless and 26 lossy WebP checks
against libwebp bit-exactly (plus a `VP8X` container, refusals and `probe()`
classifications, added in Phase 54 and Phase 57), and 5 SVG feature suites. `make fuzz-media`
mutates JPEG, GIF, WebP, and SVG inputs (2,400 cases in Release, 6,000 under
ASan+UBSan+LSan). `JpegDecoder` and `GifDecoder`, like `WebPDecoder`, are wired
into `ImageDecoder.cpp`; animated WebP, AVIF, SVG
`<text>`/CSS stylesheets/filters, and Canvas 2D shadows/patterns remain open
(see `AUDIT.md`, findings 43, 60 and 63).

---
