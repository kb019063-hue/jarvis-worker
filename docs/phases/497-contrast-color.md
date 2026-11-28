# Phase 497 — `contrast-color()` returns the better-contrasting of white or black

## What was wrong

`contrast-color()` is CSS Color 5 §8, the `<contrast-color()>` component of
the `<color>` grammar: `contrast-color(<color>)` resolves to either `white`
or `black`, whichever produces the maximum color contrast for text when the
input color is used as a solid background; if both produce the same
contrast, it resolves to `white`. The precise algorithm is UA-defined at
this level. Chrome 147, Firefox 146 and Safari 26 ship it, and since April
2026 it is Baseline newly available.

This engine had no `contrast-color` anywhere: not in `Color.cpp`'s function
dispatch, not in `is_color_function_name`, not in the computed-value path,
not in the verifier's corpus. `color: contrast-color(#ffd700)` was an
invalid declaration, so a page using the function (to pick text colors
against a runtime-set custom property is its intended use) lost the
declaration at parse time, exactly as it loses `color: not-a-real-color`.

## What changed

One function in `src/common/Color.cpp`, next to `light-dark()` and registered
in the same two places (`parse_color_impl`, `is_color_function_name`):

- exactly one top-level `<color>` argument; anything else (`contrast-color()`,
  `contrast-color(red, blue)`) is invalid, like the other color functions;
- the argument is parsed with the full parser (nested functions,
  `light-dark()`, `color-mix()`, relative colors — they all work, including
  `contrast-color(contrast-color(yellow))`);
- it is resolved to in-gamut sRGB through the existing `to_srgb8()` (OKLCH
  chroma-reduction gamut mapping, §13.2), read as **opaque** — Alpha is
  ignored, because the algorithm is defined on colors used as a solid
  background, and the argument is what is painted behind the text — and
  measured with the **WCAG 2.1 relative-luminance** ratio
  `((Y_light + 0.05) / (Y_dark + 0.05))` with the D65 sRGB coefficients
  (0.2126 / 0.7152 / 0.0722), which is what every shipping engine uses for
  the UA-defined choice:
  - `contrast_black = (Y + 0.05) / 0.05`
  - `contrast_white = 1.05 / (Y + 0.05)`
  - whichever is larger wins; a tie returns `white` (`contrast_white >=
    contrast_black`), per spec and MDN.

The function resolves at **parse time**, so everything downstream — the
specified-value normalizer, `getComputedStyle`, `StyleResolver`, the SVG
renderer, `color-mix()` arguments, serialization — sees a plain black or
white `CssColor`. There is nothing to defer: unlike `light-dark()` it does
not depend on `color-scheme`.

`currentcolor` as the argument resolves at parse time when the caller has a
current-color context (the computed-value path builds one when the text
contains `currentcolor`); with no context it fails, exactly like
`color-mix(in srgb, currentcolor ...)` does. `CssColor::is_valid_syntax`
provides a dummy context, so the value is preserved for computed-value time,
also like `color-mix`.

## Evidence

`tests/suites/497-css-color-contrast-color.cpp` pins:

- `white` → `rgb(0, 0, 0)`, `black` → `rgb(255, 255, 255)`, and the anchors
  `red` → black (Y = 0.2126), `yellow` → black (Y = 0.9278), `navy` → white
  (Y = 0.0156), `rebeccapurple` → white, `#ffd700` → black;
- the tie boundary on 8-bit grays: 117 → white, 118 → black (no integer
  gray sits exactly at the equal-contrast luminance Y ≈ 0.1791, so the
  white-wins tie branch is not reachable from a quantized channel; it is
  implemented as specified);
- non-sRGB arguments (`hsl`, `oklch`, `lab`) and alpha ignored
  (`rgb(255 0 0 / 0.5)` answers as opaque red; `transparent` answers as the
  black channels it carries);
- nested use: inside `color-mix()`, inside another `contrast-color()`, and
  around `light-dark()` (both schemes);
- `contrast-color(currentcolor)`: invalid without a context, computed
  against `red` → black and `navy` → white with one;
- argument-count/validity rules, and the end-to-end `d.style.color =
  'contrast-color(#ffd700)'` → `getComputedStyle` reads `rgb(0, 0, 0)`.

`python3 tools/verify_color.py --binary build/color_check`:

```
CSS Color 4/5 space matrix:  180/180 passed (max normalized error 5.04e-08)
CSS Color 4 §13.2 gamut map: 13/13 passed
WPT Color 4/5 valid vectors: 152/152 passed
WPT Color 4/5 invalid rules: 67/67 passed
```

(the valid vectors gained 18 `contrast-color()` cases, the invalid rules
6, including `contrast-color(currentcolor)` with no context and a 50-deep
nesting case in the fuzzer). Before the change the suite fails at its first
assertion (`CssColor::parse("contrast-color(white)", c)` is false); with it,
`make test` passes (all 535 engine suites, the new one included).
`verify_color.py --fuzz 3009`: 0 crashes (692 valid, 2317 rejected).

Red/green: `git stash` of `src/common/Color.cpp` alone rebuilt and the suite
fails with `Assertion 'CssColor::parse("contrast-color(white)", c)' failed`;
`git stash pop` restored it and the suite passes again.

## Still open (from ROADMAP row 16, unchanged)

- Wide-gamut (>8-bit sRGB) raster buffer — the engine gamut-maps to 8-bit
  sRGB at paint time, and the comparison above inherits that quantization
  (deliberate: WCAG's formula is defined on 8-bit sRGB channels);
- `@color-profile` and custom ICC profiles, `device-cmyk()`;
- the CSS Color 6 design (candidate color lists, `wcag2()` targets, APCA),
  which this engine does not attempt;
- the returned named color serializes as `rgb(0, 0, 0)` / `rgb(255, 255,
  255)`, not as the keywords `black`/`white` (the engine's computed-value
  serialization everywhere, like `light-dark()`);
- the alpha-is-ignored rule is this engine's documented choice (the algorithm
  is defined on colors painted as a solid background). The spec only
  requires the result to depend on the argument and the tie to return
  white, and leaves the algorithm to the UA.
