# Phase 50 Architecture: CSS Color 4/5 (`oklch`/`oklab`/`lab`/`lch`/`hwb`/`color()`, `color-mix()`, relative colors, `light-dark()`)

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 50 Architecture: CSS Color 4/5 (`oklch`/`oklab`/`lab`/`lch`/`hwb`/`color()`, `color-mix()`, relative colors, `light-dark()`)` section.
> The text below is the section as it stood, unchanged.


Phase 50 (AUDIT.md, finding 56) unifies color parsing, conversion, gamut
mapping, and computed-value serialization across CSS, CSSOM, and SVG into a
single engine while keeping the 4-byte RGBA `struct Color` ABI intact for the
raster pipeline.

### 1. Representation & 15 Color Spaces (`include/litebird/common/Color.hpp`, `src/common/Color.cpp`)
- `CssColor` carries `ColorSpace space`, `double c[3]`, `double alpha`,
  per-channel `bool missing[3]`, `bool missing_alpha`, and `bool legacy_srgb`.
- All 15 CSS Color 4 spaces are supported: `srgb`, `srgb-linear`,
  `display-p3`, `display-p3-linear`, `a98-rgb`, `prophoto-rgb`, `rec2020`,
  `xyz-d50`, `xyz-d65`, `lab`, `lch`, `oklab`, `oklch`, `hsl`, and `hwb`.
- Every linear RGB ↔ XYZ-D65/D50 matrix, Bradford chromatic adaptation matrix,
  and OKLab M1/M2 matrix uses the exact double-precision values derived from
  the W3C CSS Color 4 §17 rational definitions.
- `CssColor::convert_to(ColorSpace)` tracks missing components (`none`),
  powerless hues (e.g., `S == 0` in HSL, `W + B >= 100` in HWB, `C <= 1e-6` in
  LCH/OKLCH), paired all-missing propagation (`oklab`↔`oklch`, `lab`↔`lch`,
  `hsl`↔`hwb`), and analogous-component carry-forward per CSS Color 4 §12.2.
- `CssColor::to_srgb8()` maps out-of-gamut colors into 8-bit sRGB using W3C
  CSS Color 4 §13.2 OKLCH binary-search chroma reduction (`ΔE_OK ≤ 0.02`,
  `ε = 1e-4`), preserving perceptual lightness and hue rather than naively
  channel-clamping.

### 2. Parsing, CSS Values 4 Math, `color-mix()`, Relative Colors & `light-dark()`
- `CssColor::parse(std::string_view, CssColor&, const ColorResolveContext&)`
  strips CSS `/* ... */` comments, enforces `kMaxDepth = 32` against nested
  adversarial inputs, and evaluates CSS Values 4 math functions (`calc()`,
  `min()`, `max()`, `clamp()`, `round()`, `mod()`, `rem()`, trig/inverse-trig,
  `pow`/`sqrt`/`hypot`/`log`/`exp`/`abs`/`sign`, constants `pi`/`e`/`infinity`/
  `-infinity`/`NaN`, and angle units `deg`/`rad`/`grad`/`turn`) inside color
  channels and `color-mix()` percentages.
- **Relative color syntax (`from <origin> ...`)**: parses the origin color in
  the target space, binds channel keywords (`r`/`g`/`b`, `h`/`s`/`l`,
  `h`/`w`/`b`, `l`/`a`/`b`, `l`/`c`/`h`, `x`/`y`/`z`, `alpha`) as bare
  numeric variables for math expressions, clamps intermediate alpha to `[0, 1]`
  on nested relative colors, and serializes computed values per W3C §15 (using
  `color(srgb ...)` unless `none` components require preserving `hsl(...)` /
  `hwb(...)`).
- **`color-mix(in <space> [<hue-method>], <color> [<pct>], ...)`**: supports
  1..N color items, all rectangular and polar interpolation spaces, all four
  hue interpolation modifiers (`shorter`, `longer`, `increasing`, `decreasing`),
  premultiplied alpha blending, `calc(<0%)` computed clamping vs bare `<0%`
  parse-time rejection, and `0%`+`0%` equal mixing with `alpha_mult = 0`.
- **`light-dark(<light>, <dark>)`**: selects the light or dark branch from
  `ColorResolveContext::dark_color_scheme` (inherited via `color-scheme` in
  `StyleResolver`), with both branches validated at parse time.

### 3. Subsystem Integration
- **`StyleResolver`** (`include/litebird/css/StyleResolver.hpp`,
  `src/css/StyleResolver.cpp`): inherits `dark_color_scheme`, resolves
  `color-scheme` and `color` before color-consuming declarations so
  `currentColor` and `light-dark()` see the final resolved values, replaces
  the 29-color `is_color_token` list with `Color::looks_like_color`, and fixes
  parenthesis-balanced scanning in `parse_filter_list` (`drop-shadow(...)`),
  `box-shadow`, and `border`.
- **`DomStyleBindings`** (`src/js/DomStyleBindings.cpp`): deletes all five
  duplicate legacy color parsing/formatting helpers and delegates both
  `normalize_color()` and `getComputedStyle()` color serialization to
  `CssColor::serialize_computed()`.
- **`SvgRenderer`** (`src/render/SvgRenderer.cpp`): replaces hand-rolled
  `parse_svg_color()` with `Color::try_parse()` receiving `style.color` for
  `currentColor` inside CSS Color 4/5 expressions.

### 4. Verification
- `tools/color_check.cpp` + `tools/verify_color.py` (`make verify-color`,
  `make fuzz-color`, run in CI):
  - **180/180** cross-space conversions vs independent exact-rational Python
    W3C §17 oracle (`max normalized error 5.04e-08`).
  - **13/13** §13.2 OKLCH gamut-mapping assertions.
  - **133/133** WPT CSS Color 4/5 valid computed vectors & **61/61** WPT
    invalid syntax rules.
  - **5,008** adversarial grammar & mutation fuzz inputs (**0 crashes** in
    Release and ASan+UBSan).
- Live upstream WPT `css/css-color/parsing/color-computed-*.html` (6 suites):
  **4,860 / 4,860 (100.00%)**.
- `make test`: **185/185** unit suites (3 new Phase 50 suites), Release and
  ASan+UBSan clean.
