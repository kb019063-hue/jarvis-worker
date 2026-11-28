# 497. `contrast-color()` was not a color at all

`contrast-color()` — CSS Color 5 §8, Baseline newly available since April
2026 (Chrome 147, Firefox 146, Safari 26.0) — was absent from this engine.
Not unsupported in the renderer: unparseable, so `color: contrast-color(#ffd700)`
was dropped at parse time like any other invalid declaration, and the whole
use case it exists for — text color that follows a runtime-set custom
property — was impossible to express.

The function takes one `<color>` and returns `white` or `black`, whichever
produces the maximum contrast for text when the input is used as a solid
background; a tie returns `white`. The algorithm is UA-defined; every
shipping engine uses the WCAG 2.1 relative-luminance ratio, and so does this
implementation (phase [497](../phases/497-contrast-color.md)):

- `contrast-color(white)` → `rgb(0, 0, 0)`, `contrast-color(black)` →
  `rgb(255, 255, 255)`, `contrast-color(#ffd700)` → black,
  `contrast-color(navy)` → white, and the tie boundary on 8-bit grays is
  pinned on both sides (117 → white, 118 → black; no integer gray lands
  exactly at Y ≈ 0.1791);
- arguments of any color space work; alpha is ignored by documented choice.

Verified by `tests/suites/497-css-color-contrast-color.cpp` (red without
the parser change: the first assert fails) and by `make verify-color`
(152/152 valid vectors, 67/67 invalid rules, 180/180 exact-rational space
matrix, 3009-input parser fuzz, 0 crashes). See
[Phase 497](../phases/497-contrast-color.md).

Still open, inherited from ROADMAP row 16: the wide-gamut raster buffer
(to which this comparison's 8-bit sRGB quantization belongs),
`@color-profile` ICC profiles, `device-cmyk()`, and the CSS Color 6
design (candidate lists, `wcag2()` targets, APCA).
