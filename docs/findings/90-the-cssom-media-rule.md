# Finding 90 — a stylesheet's `@media` block was flattened, and every query it held applied

Phase 84. The architecture is in
[`docs/phases/84-the-cssom-media-rule.md`](../phases/84-the-cssom-media-rule.md).

## 1. What was wrong

`getComputedStyle` reads a `<style>` element's text with `parse_rules` in
`src/js/DomStyleBindings.cpp`. An `@media` block did not become a rule: the
parser walked into the block, copied the style rules inside it into the sheet's
own list, and asked the query with one test —

```cpp
const bool applies = lower.compare(0, 6, "@media") == 0 && lower.find("print") == std::string::npos;
```

— so `@media print` was dropped and **everything else applied**, whatever it
said. A page in a 1024×768 light window with `@media (max-width: 300px)
{ #target { color: orange } }` got the orange, and the same page's
`matchMedia('(max-width: 300px)').matches` said `false`. Two answers to one
question, in one engine.

The object model inherited the flattening. `sheet.cssRules` listed rules that
were not written at the top of the sheet, `sheet.cssRules[i].type` was never
`CSSRule.MEDIA_RULE`, a script had no way to read a block's query, and
`sheet.insertRule('@media print { a {} }')` threw `SyntaxError` — the at-rule
was refused on sight (`trimmed.front() == '@'`).

Phase 72 named this hole and did not close it
(`docs/findings/78-cssom.md` §4, "No `CSSMediaRule`").

## 2. What changed

- **`src/js/DomStyleBindings.cpp`** — `SheetRule` is a style rule or a
  `@media` rule (`kind`, `media_text`, `children`), held by
  `std::shared_ptr` so a rule object survives its rule being deleted;
  `parse_rules` nests instead of flattening; `compute_block` walks a media
  rule's children only when `media_query_matches` says the query holds;
  `media_query_matches`, `media_alternatives`, `media_terms`,
  `media_feature_matches` and `media_length` are the new matcher;
  `CSSMediaRule` and `MediaList` are new interfaces, with `insertRule`,
  `deleteRule`, `cssText`, `parentRule`, indexed reads, `appendMedium` and
  `deleteMedium`; `parse_inserted_rule` and `number_inserted_rules` take a
  block and number its children.
- **`tests/test_suite.cpp`** — `cssom_media_rules_and_the_cascade_inside_them`
  (62 assertions). The Phase 72 suite's one `@media` assertion said
  `insertRule('@media print { a {} }')` is a `SyntaxError`; it now asserts
  that the block goes in at the index it was given and comes back out with
  `deleteRule`, because that is what the engine does.
- **`README.md`** — 238 suites → 239.

## 3. How it is verified

- `make test` → **239 / 239** suites, including
  `cssom_media_rules_and_the_cascade_inside_them`.
- The query is asked the way `matchMedia` asks it: the same page's
  `screen and (min-width: 600px)` block applies (green) in a 1024×768 window
  and its `(max-width: 300px)`, `print` and `prefers-color-scheme: dark`
  blocks do not; `media.mediaText = '(max-width: 300px)'` turns the green off
  and back on.
- `make dom-conformance` 678 / 678, `make js-conformance` 1128 / 1152,
  `make url-conformance` 1188 / 1188, `make xml-conformance` 1718 / 1718,
  `make wpt` 9567 / 9824 with 0 unlisted failures, `make conformance`
  1967 / 1973, `make diff-trees` 0 differing trees, `make diff-xml` 0
  differing documents, `make deep` 1332 runs / 0 failed: unchanged.
- `make asan` → 0 sanitizer reports.

## 4. What is still open

- **The matcher is the media query language's smallest useful subset.** Media
  type, `width`/`height` and their `min-`/`max-` forms, `orientation`,
  `resolution`, `prefers-color-scheme`, `prefers-reduced-motion`,
  `prefers-contrast`, `hover`, `pointer`, `scripting`, `(color)`, `not`,
  `only` and commas. Range syntax, `aspect-ratio`, `@container` and the new
  boolean grouping are not parsed, and an unknown feature does not match.
- **A `<link media>` / `<style media>` attribute is still a substring test**
  for `print`, at the sheet level; only the blocks inside a sheet are exact.
- **No `CSSImportRule`/`CSSKeyframesRule`/`CSSSupportsRule`**, no
  `new CSSStyleSheet()`, no `document.adoptedStyleSheets`, `style.parentRule`
  still `null`.
- **The layout engine is not told.** Painting reads its own sheets
  (`CssParser`/`StyleResolver`, which already evaluates media queries at the
  viewport); the CSSOM a script writes still styles `getComputedStyle` only,
  as it has since Phase 72.
