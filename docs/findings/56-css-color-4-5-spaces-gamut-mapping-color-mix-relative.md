# 56. ✅ CSS Color 4/5 spaces, gamut mapping, `color-mix()`, relative colors, and `light-dark()` (Phase 50)

> Moved out of `AUDIT.md` — written there as a `## 56. ✅ CSS Color 4/5 spaces, gamut mapping, `color-mix()`, relative colors, and `light-dark()` (Phase 50)` section.
> The text below is the section as it stood, unchanged.


**The hole.** Litebird had three separate hand-rolled color parsers
(`Color::from_string` in `Color.hpp`, `parse_svg_color` in `SvgRenderer.cpp`,
and five legacy parsing helpers in `DomStyleBindings.cpp`), none of which
understood CSS Color 4 or 5: `lab()`, `lch()`, `oklab()`, `oklch()`, `hwb()`,
`color(...)` predefined spaces, `color-mix()`, relative colors (`from ...`),
`light-dark()`, CSS Color 4 space-separated `rgb()`/`hsl()` with `/ alpha` and
`none` channels, or `calc()`/`sin()`/`cos()` inside color channels. Meanwhile
`StyleResolver::is_color_token` only recognized 29 hardcoded named colors plus
`rgb`/`hsl` prefixes, so modern color tokens in `border`, `box-shadow`, and
`text-shadow` shorthands were silently dropped, and `parse_filter_list` used a
naive `s.find(')', open_p)` that truncated `drop-shadow(1px 2px oklch(...))`
at the inner closing parenthesis.

**The fix.**
- Added `src/common/Color.cpp` and expanded `include/litebird/common/Color.hpp`
  (keeping the 4-byte `struct Color` ABI intact) with a single unified
  `CssColor` / `Color::try_parse` engine:
  - All 15 CSS Color 4 spaces (`srgb`, `srgb-linear`, `display-p3`,
    `display-p3-linear`, `a98-rgb`, `prophoto-rgb`, `rec2020`, `xyz-d50`,
    `xyz-d65`, `lab`, `lch`, `oklab`, `oklch`, `hsl`, `hwb`), exact W3C CSS
    Color 4 §17 rational-derived transfer functions & 3×3 matrices, and
    Bradford D65↔D50 chromatic adaptation.
  - Per-channel missing-component (`none`) tracking, powerless-hue handling,
    paired all-missing propagation (`oklab`↔`oklch`, `lab`↔`lch`, `hsl`↔`hwb`),
    analogous-component carry-forward, and W3C §13.2 OKLCH binary-search
    chroma-reduction gamut mapping (`ΔE_OK ≤ 0.02`) into 8-bit sRGB.
  - Full CSS Values 4 math evaluator inside color channels (`calc()`, `min()`,
    `max()`, `clamp()`, `round()`, `mod()`, `rem()`, `sin`/`cos`/`tan`/
    `asin`/`acos`/`atan`/`atan2`, `pow`/`sqrt`/`hypot`/`log`/`exp`/`abs`/
    `sign`, `pi`/`e`/`infinity`/`-infinity`/`NaN`, `deg`/`rad`/`grad`/`turn`,
    and channel keyword bindings for relative colors), plus CSS `/* ... */`
    comment stripping and recursion depth limits (`kMaxDepth = 32`).
  - CSS Color 5 `color-mix()` (1..N arguments, all rectangular and polar hue
    modifiers `shorter`/`longer`/`increasing`/`decreasing`, premultiplied
    alpha, `0%`+`0%` and `calc(<0%)` clamping per 2025/2026 WPT), relative
    color syntax (`rgb/hsl/hwb/lab/lch/oklab/oklch/color(from <origin> ...)`),
    `light-dark(<light>, <dark>)`, and CSS Color 4 §15 computed serialization
    (legacy `rgb()`/`rgba()` for sRGB, preserving `none` in `hsl()`/`hwb()`/`color(srgb ...)`).
- Wired `StyleResolver` (`dark_color_scheme` inheritance, `color-scheme` and
  `color` resolved before color-consuming properties so `currentColor` and
  `light-dark()` resolve accurately; `Color::looks_like_color` replacing the
  29-color list; balanced-paren parsing for `drop-shadow(...)`, `box-shadow`,
  and `border`), `DomStyleBindings` (removed all 5 duplicate legacy helpers;
  `getComputedStyle` emits `CssColor::serialize_computed()`), and `SvgRenderer`
  (replaced `parse_svg_color` with `Color::try_parse` passing `style.color`).
- Added standalone verification harness `tools/color_check.cpp` and oracle/fuzzer
  `tools/verify_color.py`, wired into `Makefile` (`verify-color`, `fuzz-color`,
  `fuzz`, `asan-core`) and `.github/workflows/ci.yml`.

**Verification.**
- `make verify-color`: **180/180** cross-space conversions verified against an
  independent exact-rational Python W3C §17 oracle (`max normalized error
  5.04e-08`), **13/13** §13.2 OKLCH gamut-mapping cases, **133/133** WPT CSS
  Color 4/5 valid computed vectors, and **61/61** WPT invalid syntax rules.
- `make fuzz-color`: **5,008** adversarial grammar and mutation inputs (deep
  nesting, `NaN`/`infinity`, huge angles, malformed comments/parens), **0
  crashes** in both Release and ASan+UBSan.
- Live upstream WPT `css/css-color/parsing/` computed suites checked against
  `build/color_check`: **4,860 / 4,860 (100.00%)** (`color-computed-rgb.html`
  74/74, `color-computed-hsl.html` 3,735/3,735, `color-computed-hwb.html`
  64/64, `color-computed-lab.html` 120/120, `color-computed-color-mix-function.html`
  431/431, `color-computed-relative-color.html` 436/436).
- `make test`: **185/185** engine unit suites (3 new Phase 50 suites covering
  spaces/gamut/serialization, `color-mix()`/relative/`light-dark()`, and
  `StyleResolver` + `DomStyleBindings` + `SvgRenderer` integration), passing in
  both Release and ASan+UBSan (`build-asan/test_suite`, `build-asan/color_check`,
  `style.dat` 33/33 under ASan+UBSan).
- Full regression suites: `make conformance` **1,792/1,792**, `make dom-conformance`
  **644/644**, `make js-conformance` **926/940**, `make url-conformance`
  **1,188/1,188**, `make wpt` **7,998/8,009** across 103 files (0 unlisted failures).

**Still open.** The raster framebuffer (`struct Color` / `BitmapRenderer`) is
still 8-bit sRGB, so wide-gamut colors (`display-p3`, `rec2020`, `oklch`) are
gamut-mapped via §13.2 OKLCH chroma reduction at paint time rather than output
to a 16-bit/float wide-gamut buffer. CSS Color 5 `contrast-color()`,
`device-cmyk()`, custom `@color-profile` ICC profiles, and `color-mix()`
interpolation of `currentColor` as a deferred computed value (rather than
resolving against the element's computed `color` during style resolution) are
not implemented.
