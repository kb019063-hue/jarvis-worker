# Phase 32 Architecture: The CSS Color Keywords, Once

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 32 Architecture: The CSS Color Keywords, Once` section.
> The text below is the section as it stood, unchanged.


Phase 27 found that a red box and a `getComputedStyle(el).color ===
'rgb(255, 0, 0)'` did not agree. The bindings knew the answer; the renderer
had a separate table of its own. There is now one table.

### 1. One source of truth

`Color::named_color_value(lower, out)` in
`include/litebird/common/Color.hpp` is the 148 CSS Color 4 keyword table that
used to live as `named_color_value()` in `DomStyleBindings.cpp`. Both sides of
the engine call it:

- `Color::parse()` converts `red`, `green`, `tomato`, `rebeccapurple`, the
  `grey`/`gray` aliases and the rest of the keywords to the specification's
  bytes; `transparent` returns alpha 0.
- The computed style returns the same bytes as `rgb(...)` text, and the
  declaration normalizer accepts the same 148 names before storage.

There is no longer a Bootstrap-looking 19-name table with a different red, and
no copies of the 148-name list in JavaScript DOM bindings and the common
color helper. The common table is the one both callers use.

### 2. What did not change

An unknown keyword still parses to opaque black; Phase 32 is about giving a
name the wrong answer is not, not about rejecting unknown CSS. `currentcolor`
is still context-dependent (handled by the computed style path, not by a
standalone renderer color), and `rgb()`, hex parsing and percentage or
parenthesis-separated `rgba()` values keep their existing behavior.

### 3. Verification

`css_color_keywords_match_the_css_specification` checks every one of the 148
keywords through `Color::named_color_value` and `Color::parse`, plus case-folded
and whitespace-surrounded input, several aliased spellings
(`aqua`/`cyan`, `fuchsia`/`magenta` and the `gray`/`grey` family), `transparent`, two of the
formerly missing names (`tomato`, `silver`) and an unknown name. The old
`color_parsing` expectation for `blue` was corrected to CSS's
`rgb(0, 0, 255)`. `make test` prints `All 114 engine test suites passed
successfully!`. The first DOM conformance rerun caught a 571/572 regression:
the declaration normalizer had accepted `transparent` and `currentcolor`, which
the raw 148-entry value table does not contain, and the shared table alone
would have dropped both. The normalizer now stores either keyword explicitly;
the rerun is 572/572. `make conformance` is 1781/1792,
`make js-conformance` is 840/845, `make dom-conformance` is 572/572,
`make url-conformance` is 1188/1188, `make diff-trees` has 0 differing pages,
and `make asan` reports no sanitizer finding (ASan JS corpus 839/845; its
ASan deep run covers 842 executions). The HTML and JS floors are
unchanged.

### 4. What is still approximate

- `Color::parse()` does not implement `rgba()`, `hsl()`/`hsla()`, or CSS Color
  4's `hwb()`/`lab()` syntax. It is a keyword, hex and integer-`rgb()` parser;
  that was its scope before this phase and remains the row of the known gaps.
- `Color::parse("currentcolor")` is opaque black outside the computed-style
  cascade, because it has no inherited color to resolve against. Canvas and
  SVG strings that literally say `currentcolor` do not get a useful color.
- The legacy `dark_gray()` and `light_gray()` helper functions retain their
  historical call-site values; `Color::parse()` no longer uses them.
- A malformed `#` value with fewer than three hexadecimal digits returns black,
  as before.
---
