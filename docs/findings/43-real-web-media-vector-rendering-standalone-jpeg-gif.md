# 43. ✅ Real-Web Media & Vector Rendering: standalone JPEG/GIF decoders, upgraded SVG & Canvas 2D

> Moved out of `AUDIT.md` — written there as a `## 43. ✅ Real-Web Media & Vector Rendering: standalone JPEG/GIF decoders, upgraded SVG & Canvas 2D` section.
> The text below is the section as it stood, unchanged.


**What was missing.**
- **JPEG & GIF:** `ImageDecoder` had no GIF decoder at all, and its JPEG path
  (`ImageDecoder.cpp`, owned in parallel by PR #39) was a minimal stub that
  could not decode real-world Baseline/Extended/Progressive JPEGs, chroma
  subsampling (4:2:0 / 4:2:2 / 4:4:0), restart markers (`DRI`/`RST0`–`RST7`),
  or CMYK/YCCK color spaces.
- **SVG (`SvgRenderer`):** `<path>` ignored curves (`C/c`, `S/s`, `Q/q`, `T/t`)
  and elliptical arcs (`A/a`), `<defs>` children were painted directly,
  `transform` matrices, `preserveAspectRatio`, inline `style` declarations,
  `opacity`/`fill-opacity`/`stroke-opacity`, `fill-rule="evenodd"`,
  `currentColor`, `display:none`/`visibility:hidden`, `<linearGradient>` /
  `<radialGradient>`, `<use>` / `<symbol>`, `<clipPath>`, and rounded
  `<rect rx ry>` were missing.
- **Canvas 2D (`CanvasRenderingContext2D`):** No Current Transformation Matrix
  (`save`/`restore`/`translate`/`rotate`/`scale`/`transform`/`setTransform`),
  no `clip()`, no `globalAlpha`, no `NonZero`/`EvenOdd` winding rules or
  anti-aliased scanline fill, thin Bresenham-only stroking (no real stroke
  width, caps, joins, dashes, or `strokeGradient`), no `roundRect`/`ellipse`/
  `arcTo`/`isPointInPath`, and no `getImageData`/`putImageData`.

**What changed (Phase 38, strictly disjoint from open PRs #36–#40).**
- **Standalone ITU-T T.81 JPEG decoder (`JpegDecoder.hpp/.cpp`):** Zero-dependency
  C++20 decoder for Baseline (`SOF0`), Extended Sequential (`SOF1`), and
  Progressive (`SOF2`) DCT JPEGs. Supports 8/16-bit `DQT`, 9-bit fast-lookup +
  16-bit canonical `DHT`, `DRI`/`RST0`–`RST7`, interleaved and non-interleaved
  scans, progressive DC/AC spectral selection + successive approximation + EOB
  runs, separable double-precision 8×8 IDCT with DC-only fast paths, centered
  triangle `[3/4, 1/4]` chroma upsampling (4:4:4, 4:2:2, 4:2:0, 4:4:0, odd
  dimensions), Grayscale / YCbCr / RGB / CMYK / Adobe YCCK, and strict
  `kMaxDecodedPixels` / `kMaxScans` / `RasterBudget` bounds.
- **Standalone GIF87a/GIF89a decoder (`GifDecoder.hpp/.cpp`):** Zero-dependency
  single-frame and animated multi-frame decoder. Supports 2..8-bit LZW with
  deferred clear-code handling, Global/Local Color Tables, 4-pass interlacing,
  Graphic Control Extension transparency, delays, all 4 disposal methods
  (`None`, `DoNotDispose`, `RestoreBackground`, `RestorePrevious`), `NETSCAPE2.0`
  loop counts, sub-rectangle frame compositing, and strict frame/pixel bounds.
- **Upgraded Canvas 2D (`Canvas2D.hpp/.cpp`):** Full 2D CTM stack (`save`/
  `restore`, `translate`/`rotate`/`scale`/`transform`/`setTransform`/
  `resetTransform`), 4× vertical + analytical horizontal anti-aliased scanline
  rasterizer with `NonZero` and `EvenOdd` winding rules, CW-wound polygon-ribbon
  thick stroking with `butt`/`round`/`square` caps, `miter`/`bevel`/`round`
  joins, `setLineDash`/`getLineDash`/`lineDashOffset`, `stroke_gradient`,
  inverse-CTM linear/radial gradients with `Pad`/`Reflect`/`Repeat` spread modes,
  `globalAlpha` + alpha clip mask (`clip()`), `roundRect`/`ellipse`/`arcTo`/
  `rect`/`isPointInPath`, `getImageData`/`putImageData`/`createImageData`,
  CTM-aware `drawImage`, and JS property accessors via `JsValue::define_accessor`.
- **Upgraded SVG renderer (`SvgRenderer.hpp/.cpp`):** Iterative ID index (no
  native recursion on deep DOMs), `<defs>` skipping, full SVG path grammar
  including W3C F.6.5/F.6.6 `A/a` elliptical arcs and compact flag syntax,
  `viewBox` + `preserveAspectRatio` (`meet`/`slice`/`none`), `transform` stack,
  presentation attributes + inline `style` + `#RGBA`/`#RRGGBBAA`/`rgba()` +
  `currentColor` + `opacity`/`fill-opacity`/`stroke-opacity` + `display:none`/
  `visibility:hidden`, `<linearGradient>`/`<radialGradient>` with `href`
  inheritance / `objectBoundingBox` / `userSpaceOnUse` / `gradientTransform` /
  `spreadMethod`, `<use>` with depth/cycle guard + `<symbol>`, `<clipPath>`,
  rounded `<rect rx ry>`, nested `<svg>`, and recursion/budget caps.
- **Verification & fuzzing tools:** `tools/media_check.cpp`,
  `tools/conformance/verify_media_render.py`, `tools/fuzz/fuzz_media.py`, and
  `Makefile` targets `verify-media` and `fuzz-media` (wired into CI and `asan-core`).

**How verified.**
- `make test`: **139/139** engine unit test suites pass (4 new suites:
  `jpeg_decoder_baseline_progressive_and_guards`,
  `gif_decoder_87a_89a_interlaced_and_animation`,
  `canvas2d_ctm_scanline_thick_stroke_clip_and_js_bindings`,
  `svg_real_world_paths_arcs_gradients_use_clip_and_guards`), both in Release
  (`-O2`) and under strict ASan+UBSan (`-fsanitize=address,undefined -fno-sanitize-recover=all`).
- `make verify-media`: **35/35** checks pass (both Release and ASan+UBSan+LSan
  `detect_leaks=1`): 21 JPEG variants vs ImageMagick/`libjpeg` achieve
  **52.06–99.00 dB PSNR** (`MAE 0.000–0.389`, `max_diff 0–3`), 9 GIF variants
  (including 4-pass interlaced and animated Dispose None/Background/Previous/Optimize)
  achieve **bit-for-bit exact `max_diff = 0`** against ImageMagick `-coalesce`,
  and 5 SVG feature suites pass deterministic pixel assertions.
- `make fuzz-media`: **1,800** mutated JPEG/GIF/SVG cases in Release and
  **4,500** cases under ASan+UBSan+LSan (`detect_leaks=1`) — 0 crashes, 0 UB,
  0 leaks, 0 timeouts.
- Deep-page stress checks (`tools/fuzz/deep_pages.py --only svg/canvas/images`):
  39/39 Release runs at depth 200,000 (8 MB stack) and 36/36 ASan+UBSan runs
  at depth 10,000 (2 MB stack) pass with 0 failures.
- Existing conformance corpora unchanged: `make conformance` 1781/1792,
  `make js-conformance` 840/845, `make dom-conformance` 629/629,
  `make url-conformance` 1188/1188, `make wpt` 4640/5192, `make bench-css` 20/20,
  `make security-fetch` 3/3.

**What is still open.**
- **`ImageDecoder::decode()` dispatch wiring:** `include/litebird/render/ImageDecoder.hpp`
  and `src/render/ImageDecoder.cpp` were left untouched because PR #39 owns them
  in parallel; once PR #39 merges, wire `JpegDecoder::decode()` and
  `GifDecoder::decode()` into `ImageDecoder::decode()` (and optionally expose
  animated GIF frames to the shell).
- **Other image formats:** WebP, AVIF, and animated APNG/ICO are not implemented.
  JPEG arithmetic coding (`SOF9`/`SOF10`), lossless JPEG (`SOF3`), and 12-bit
  precision (`P=12`) are rejected cleanly as unsupported.
- **SVG features still missing:** `<text>`/`<tspan>`, `<style>` CSS stylesheet
  rules targeting SVG elements, `<pattern>`, `<mask>`, `<filter>` (`feGaussianBlur`,
  etc.), `<marker>`, `<image>` inside `<svg>`, and SMIL/CSS animations on SVG.
- **Canvas 2D features still missing:** shadows (`shadowBlur`/`shadowColor`),
  `createPattern`, `Path2D`,
  `drawImage(HTMLImageElement)` (only `ImageData` / canvas-to-canvas is wired),
  and remaining Porter-Duff / blend modes beyond the existing set.
  (`fillText`/`strokeText`/`measureText` are implemented — Phase 46.)

---
