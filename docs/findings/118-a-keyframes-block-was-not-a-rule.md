# Finding 118 — a `@keyframes` block was not a rule of the sheet

Phase 112. The architecture and the measurements are in
[`docs/phases/112-keyframes-in-the-cssom.md`](../phases/112-keyframes-in-the-cssom.md).

## 1. What was wrong

Two models of the same style sheet live in this engine. `css::StyleSheet` is
what the cascade and the animator read; `ParsedSheet` in
`src/js/DomStyleBindings.cpp` is what `CSSStyleSheet.cssRules` reads, and since
Phase 72 it is also what the cascade matches elements against. The first has
known about `@keyframes` from the start. The second knew `@media` (Phase 84)
and flattened `@supports`/`@layer`, and its `parse_rules` skipped every other
at-rule with its whole block.

The consequences were all off-by-one and all observable:

```css
@keyframes slide { 0% { left: 0px } }
p { color: red }
```

- `sheet.cssRules.length` was `1`, not `2`;
- `sheet.cssRules[0]` was the `p` rule, so a page that read rule *n* read
  rule *n+1* whenever a keyframes block came before it;
- `sheet.insertRule('@keyframes spin { … }')` threw `SyntaxError`, because
  `parse_inserted_rule` accepted only a style rule or `@media`;
- `CSSKeyframesRule` and `CSSKeyframeRule` did not exist as interfaces, so
  `rule instanceof CSSKeyframesRule` could not be asked at all.

## 2. What changed

`SheetRule` — the one rule type in the CSSOM, used by `cssRules`, by
`insertRule` and by the cascade — gained two kinds, `Keyframes` and
`Keyframe`, with the name a block declares and the selector a frame was
written with. Because a block's frames are its `children`, the existing
`CSSRuleList`, rule numbering, object identity and `parentRule` work on them
without a second mechanism.

- `parse_rules` reads `@keyframes`/`@-webkit-keyframes` blocks; the name is
  whatever follows the at-keyword, quotes stripped; `parse_keyframe_blocks`
  reads the frames.
- `CSSKeyframesRule`: `type` 7, `name`, `cssRules`/`rules`, `appendRule`,
  `deleteRule`, `findRule`, `cssText`.
- `CSSKeyframeRule`: `type` 8, `keyText`, `style`, `cssText`.
- `insertRule` takes a block at sheet level and inside `@media`; a block with
  no name is a `SyntaxError`.
- The cascade skips the two new kinds explicitly. They matched nothing before,
  by accident; now they are not offered to the matcher at all.

## 3. Verification

- `make test` → **268 / 268**, with a new suite of some sixty in-page
  assertions (`keyframes_are_rules_of_the_cssom`).
- The DOM corpus, the JS corpus, the HTML tree corpus, URLs and
  web-platform-tests are all unchanged: 678/678, 1141/1158, 1973/1973,
  1188/1188 and 9567/9824 with the same 257 listed failures.
- No engine file outside `src/js/DomStyleBindings.cpp` changed.

## 4. What is still open

- **The two models are still two.** A frame a script appends is visible to
  `cssRules` and invisible to `animation`: the animator reads
  `css::StyleSheet::keyframes`, which the CSSOM never writes. That is the
  honest state of this phase, and the next one.
- `CSSImportRule`, script-visible `CSSFontFaceRule`, `new CSSStyleSheet()`,
  `replace`/`replaceSync`, nested `@media`, and used values for
  `getComputedStyle(el).width` are all still missing.
- jsdom implements neither `CSSMediaRule` nor `CSSKeyframesRule`, so the DOM
  corpus cannot record any of this; the assertions live in the unit suite.
