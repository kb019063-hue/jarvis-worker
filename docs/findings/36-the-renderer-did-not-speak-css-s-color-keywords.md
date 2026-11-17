# 36. ✅ The renderer did not speak CSS's color keywords

> Moved out of `AUDIT.md` — written there as a `## 36. ✅ The renderer did not speak CSS's color keywords` section.
> The text below is the section as it stood, unchanged.


**The hole.** The renderer's common color parser had its own table of 19
names, 15 of them not the CSS Color 4 values (`red` was the Bootstrap-looking
`rgb(220, 53, 69)`, `green` was `rgb(40, 167, 69)`), and every other CSS
keyword fell through to opaque black. The DOM side knew the standard's 148
keyword values, so `getComputedStyle()` answered `rgb(255, 0, 0)` for a `red`
that canvas, SVG and layout painted as a different red.

**The fix.** `Color::named_color_value(lower, out)` is now the single 148-name
table (`include/litebird/common/Color.hpp`). `Color::parse()` uses it for
`red`, `green`, `tomato`, `rebeccapurple`, the gray/grey aliases and the rest;
`transparent` returns alpha 0; `Color::parse()` with an unknown keyword still
returns opaque black. The computed-style color path and the declaration
normalizer use the same table instead of keeping their own copies, so the
bytes computed for `getComputedStyle()` and the bytes painted by canvas, SVG
and layout cannot drift again.

**How verified.** The unit suite `css_color_keywords_match_the_css_specification`
checks all 148 keywords through both `Color::named_color_value()` and
`Color::parse()` plus case-folded/whitespace-surrounded names, several aliases,
`transparent`, `tomato`, `silver` and an unknown keyword. `color_parsing` now
expects CSS's `blue` (`rgb(0, 0, 255)`). `make test` prints `All 114 engine
test suites passed successfully!`. The refactor initially made the DOM corpus
571/572 because the declaration normalizer no longer accepted `transparent` and
`currentcolor`; both values are accepted explicitly in the declaration block and
572/572 was restored. The rerun is 572/572. `make conformance` is 1781/1792,
`make js-conformance` is 840/845, `make url-conformance` is 1,188/1,188,
`make diff-trees` has 0 differing pages, `make fuzz` is 0/800 findings,
`make deep` is 0/1,292 failures, and `make asan` reports no sanitizer finding
(ASan JS corpus 839/845; ASan deep 842 executions). The HTML and JS floors are
unchanged; the parser was not touched.

**What is still approximate.** `Color::parse()` remains a keyword, hex and
integer-`rgb()` parser: no `rgba()`, `hsl()`/`hsla()`, `hwb()` or laboratory
color syntax; `currentcolor` outside the computed-style cascade has no inherited
color to resolve against and remains opaque black; and the legacy
`dark_gray()`/`light_gray()` helpers keep their previous call-site constants.
