# Phase 244 — a function is a grammar too

Task: `contain`, `filter`, `mask` and `clip-path` took any string, and
`will-change: transform, opacity` was refused (Phase 235's open list). Finding 248.

## 1. What was wrong

```js
el.style.setProperty('filter', 'totallybogusvalue');   // stored, echoed back
el.style.setProperty('clip-path', 'bogus(1px)');       // stored, echoed back
el.style.setProperty('will-change', 'transform, opacity'); // refused — a browser keeps it
```

Phase 235 gave thirty-five keyword properties a `ValueKind::Keyword` rule and
deliberately left four properties as free text, because their grammars are
function lists and a keyword-only rule would refuse `filter: blur(2px)`.
It also left `will-change` as a keyword rule that checked only the first
keyword, so the one comma-list property of the set refused a valid
declaration. A browser drops the first two declarations and keeps the third;
this engine had it backwards on all three.

## 2. What changed

`normalize_value` now special-cases the five properties before the rule
lookup, after the global-keyword and `var()` passes (those behave as
everywhere else). No new `ValueKind`: each grammar is its own validator,
next to the ones it replaces.

- **`contain`** — `none | strict | content | [ size || inline-size || layout || style || paint ]`.
  The three standalone keywords may not be mixed with the list and no
  list component may appear twice, as the grammar's `||` requires.
- **`filter`** — `none | <filter-function>+`, whitespace-separated tokens, a
  function per token: `blur()` (one non-negative length, a percentage is not
  one), `hue-rotate()` (one angle: a number with `deg`/`rad`/`grad`/`turn`, or
  unitless zero), `brightness()`/`contrast()`/`grayscale()`/`invert()`/`opacity()`/
  `saturate()`/`sepia()` (one `<number-percentage>`), `drop-shadow()` (2–3
  lengths, the third a non-negative length that is not a percentage, plus at
  most one color token anywhere), and `url()` (a reference to an SVG filter).
- **`mask`** — `none | <mask-layer>#`, top-level comma-separated layers. Each
  layer must carry a `<mask-reference>` (a `url()` or an image function —
  the `*-gradient(…)`s and `image(-set)(…)` — or `none`), and may carry
  position/size lengths, the repeat/mode/attachment/box keywords and the `/`
  between position and size. A layer without a reference is refused.
- **`clip-path`** — `none | url(...) | [ <basic-shape> || <geometry-box> ]`.
  At most one of `inset()`, `circle()`, `ellipse()`, `polygon()`, `path()`,
  `xywh()`, `shape()`, and at most one geometry box
  (`margin-box`/`border-box`/`padding-box`/`content-box`/`fill-box`/`stroke-box`/
  `view-box`); `border-box` alone is valid. A `url()` stands alone. The
  shapes are validated, not just named: `inset()`/`xywh()` (exactly four)
  take 1–4 length-percentages plus an optional `round` and 1–4 shape radii
  (`closest-side`/`farthest-side` count); `circle()` (one radius) and
  `ellipse()` (two or none) take an optional `at <position>`; `polygon()`
  splits its points on top-level commas, drops a leading `nonzero`/`evenodd`,
  and requires each point to be a pair of length-percentages; `path()`
  requires one quoted string; `shape()` (css-shapes-2) is checked for
  structure only — balanced parens and a small safe charset — deliberately,
  so a valid `shape()` is never refused by an incompletely modelled grammar.
- **`will-change`** — `auto | <custom-ident>#`, no longer a keyword rule.
  `auto` stands alone; each comma-separated feature must be a custom ident
  (any property name — the engine would refuse valid ones with a whitelist,
  which is the worse bug — plus `scroll-position` and `contents`). A
  CSS-wide keyword inside a list is refused; `will-change: transform,`
  (a trailing comma) is refused.

`var()`/`calc()`/`min()`/`max()`/`clamp()`/`env()` are taken on trust inside
arguments, as elsewhere in the file. The `<style>` sheet caveat is
**unchanged**: declarations there go through the CSS parser and the cascade,
which has its own idea of valid — a separate finding, as Phase 235 left it.

## 3. What is still open

- The `<style>` sheet path, as above.
- `cursor: url(a.cur), pointer` is still refused: its grammar is a
  comma-separated url-list plus a keyword, and the keyword rule does not
  know it. Same shape of bug, different property; not this phase's.
- `touch-action: pan-x pan-y` (a space-separated list) and
  `background-repeat: repeat-x repeat-y` are refused by the single-keyword
  rule; same story.
- Gradient argument contents are not validated (the function name and
  balanced parens are); neither are `mask-*` longhands, which stay free
  text by design.

## 4. How it was verified

```
make test               All 384 engine test suites passed successfully!
make js-conformance     JS conformance, full corpus: 1149/1158 — unchanged
Everyday probe          90/90
make check-numbers      0 failure(s)
make check-docs         0 failure(s)
```

The new suite is `tests/suites/244-a-function-is-a-grammar-too.cpp`
(`a_function_or_list_value_outside_the_grammar_is_not_a_declaration`); about
60 assertions covering all five grammars on both sides of the line, including
the row's own example, the two-position `circle(50% at center)` and one-token
`at center` form, and the `will-change` trailing comma. Phase 235's suite
(`tests/suites/235-css-keyword-values.cpp`) was updated in place, as the
suites README allows: `will-change` left the keyword table because its
grammar is no longer a keyword set; its accept side is asserted there
(`will-change: transform`) and the list grammar is held to by 244's suite.
