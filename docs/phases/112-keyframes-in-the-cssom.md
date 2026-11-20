# Phase 112 — a `@keyframes` block is a rule of the sheet

Task row: **What the DOM corpus cannot ask** — the CSSOM half. Finding 118.

## 1. What was wrong

The engine has parsed `@keyframes` since the cascade could animate with it, but
the CSS Object Model never learned about it. `src/js/DomStyleBindings.cpp` has
its own parse of a sheet's text — the one `cssRules` reads and, since Phase 72,
the one the cascade reads too — and its at-rule branch knew two at-rules:

- `@media`, which Phase 84 made a `CSSMediaRule` with its own rule list;
- `@supports` and `@layer`, whose blocks are flattened into the sheet.

Everything else was skipped, block and all. So a sheet holding

```css
@keyframes slide { 0% { left: 0px } 100% { left: 10px } }
p { color: red }
```

answered `cssRules.length === 1`, and `cssRules[0]` was the `p` rule: every
rule after a keyframes block was off by one index, `insertRule('@keyframes …')`
was a `SyntaxError`, and there was no `CSSKeyframesRule` and no
`CSSKeyframeRule` at all.

## 2. What changed

`src/js/DomStyleBindings.cpp`, and only that file:

- **`SheetRule` has four kinds now** — `Style`, `Media`, `Keyframes`,
  `Keyframe` — with `keyframes_name` on a block and `key_text` (`0%`,
  `50%, 100%`, `from`, `to`) on a frame. A block's frames are its `children`,
  the same vector a `@media` rule keeps its rules in, so the existing
  `CSSRuleList`, numbering and identity machinery works on them unchanged.
- **`parse_rules` reads a `@keyframes` or `@-webkit-keyframes` block** into
  such a rule, and `parse_keyframe_blocks` reads the frames inside it. The name
  is whatever follows the at-keyword, with quotes stripped; a block with no
  name is not a rule, and is dropped. The at-rule is recognised before the
  `@media` test, and the ordering of `cssRules` is now the source order of
  every rule kind.
- **`CSSKeyframesRule`** — `type` 7, `name` (readable and writable),
  `cssRules` (aliased as `rules`), `appendRule(text)`, `deleteRule(select)`,
  `findRule(select)` and `cssText`. `appendRule` parses one frame and is a
  `SyntaxError` for anything else; `deleteRule` on a selector that is not there
  does nothing, as the API has it; `findRule` answers `null`.
- **`CSSKeyframeRule`** — `type` 8, `keyText` (readable and writable),
  `style` (a `CSSStyleDeclaration` over the frame's declarations) and
  `cssText`.
- **`insertRule`** takes a `@keyframes` rule on a sheet or on a `@media` rule,
  and `deleteRule` drops it like any other.
- **The cascade was told to leave the new kinds alone.** The walk in
  `compute_block` treated everything that was not a `@media` rule as a style
  rule; a keyframes block has no selector, so it matched nothing by accident.
  It now skips a rule that is neither a style rule nor a `@media` rule
  explicitly (`is_style_or_media()`), which is the difference between "matches
  nothing today" and "is not for matching".

## 3. How it is verified

- `make test` → **268 / 268** suites, the new one being
  `keyframes_are_rules_of_the_cssom`: 60-odd assertions compared inside the
  page, over the rules of a sheet that holds three keyframes blocks (one
  prefixed), a keyframes block inside `@media`, and a style rule — their order,
  their brands, `type` and the constants, `name`, frames and their `keyText`,
  frame `style` (`left`, and `opacity: .5` read back as `0.5`), `cssText`,
  `parentRule`, `parentStyleSheet`, object identity for a frame
  (`cssRules.item(0) === cssRules[0]`), `findRule`/`appendRule`/`deleteRule`,
  the `SyntaxError`s (a block with no name, `appendRule` of nonsense or of two
  frames), `insertRule`/`deleteRule` of a block, a block inside `@media`, and
  two assertions that a keyframes block colours nothing.
- `make dom-conformance` **678 / 678**, `make conformance` **1973 / 1973**,
  `make js-conformance` **1141 / 1158** (98.5%), `make url-conformance`
  **1188 / 1188**, `make wpt` **9567 / 9824** with 257 listed failures and none
  unlisted — all unchanged.
- `make security-fetch` 3/3, `make security-https` 20/20, `make security-worker`
  17/17, `make metrics` and `check_docs.py` → 0 failures.
- Ubuntu CI runs the suite normally and under ASan/UBSan.

## 4. What is still open

- **The CSSOM's keyframes are not what the animation engine reads.** The
  cascade and the animator read `css::StyleSheet::keyframes`, filled by
  `CssParser`; the CSSOM reads its own model of the same text. A rule a script
  appends through `CSSKeyframesRule` is therefore visible to script and not to
  `animation`, which no test asserts and no browser would do. Joining the two
  models is the next step, and it is the same shape as the problem Phase 72
  solved for style rules.
- **`@import` is still skipped**, so there is no `CSSImportRule`, and a
  `@font-face` block is still not a script-visible `CSSFontFaceRule`.
- **`new CSSStyleSheet()` does not exist**, so a constructed sheet cannot be
  adopted; `CSSStyleSheet.replace`/`replaceSync` are missing with it.
- **`@media` rules do not nest**, which the CSSOM mirrors: a `@media` rule
  inside a `@media` rule is not something this engine writes.
- **The DOM corpus cannot record any of it**: jsdom does not implement
  `CSSMediaRule` or `CSSKeyframesRule` either, so the CSSOM stays asserted in
  `tests/test_suite.cpp`.
