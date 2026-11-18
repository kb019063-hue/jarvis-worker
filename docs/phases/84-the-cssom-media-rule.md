# Phase 84 — the `@media` block is a rule of the sheet

Task row: **What the DOM corpus cannot ask** — "a sheet has no `CSSMediaRule`
(an `@media` block is skipped when the sheet is read, so
`insertRule('@media …')` is a `SyntaxError`)". Finding 90.

## 1. What was wrong

Phase 72 gave the CSSOM a sheet a script can read and write, and left one kind
of rule out of it. `parse_rules` — the parser `getComputedStyle` has used since
Phase 27 — walked into an `@media` block and copied its style rules into the
sheet's own list:

```js
var sheet = document.querySelector('style').sheet;
// <style>@media screen and (min-width: 600px) { p { color: green } }</style>
sheet.cssRules.length          // 1, and it is the `p` rule
sheet.cssRules[0].type         // 1 (CSSStyleRule), never 4 (CSSMediaRule)
sheet.insertRule('@media print { a {} }')   // SyntaxError
```

Three things were wrong at once, and they had one cause:

1. **The block was not a rule.** `cssRules` listed the rules *inside* every
   `@media` block as if they had been written at the top of the sheet, so a
   `@media` rule's position — which is its place in the cascade — was lost. A
   script that deletes `cssRules[3]` was deleting a rule written five lines
   further down.
2. **A query that did not match still applied.** The parser's whole test was
   "does the text contain `print`". So `@media (max-width: 300px)` styled a
   1024-pixel window, `@media (prefers-color-scheme: dark)` applied its
   dark-mode rules to a light page, and `@media screen and (min-width: 600px)
   and (orientation: portrait)` applied while both of its features were false.
   `matchMedia` and `innerWidth` already answered those questions correctly, so
   the two disagreed on the same page.
3. **`insertRule` refused it.** The rule a stylesheet is mostly made of, in
   pages written this decade, could not be inserted: `trimmed.front() == '@'`
   was the test for "not a rule".

## 2. What changed, piece by piece

**A rule is a style rule or a `@media` rule.** `SheetRule` gained a `kind`, a
`media_text` and a `children` list, and `ParsedSheet.rules` is now
`std::vector<std::shared_ptr<SheetRule>>` — the same shape a browser has:
`sheet.cssRules` is the rules written in the sheet, and a `@media` rule's
`cssRules` is the rules written inside it.

**The parse nests instead of flattening.** `parse_rules` reads an `@media`
block into the rule's own `children`. `@supports` and `@layer` hold their rules
unconditionally as far as this sheet is concerned, so they keep the flattening
they had; every other at-rule is skipped as before. Rule ids come from the
sheet's one counter, so `cssRules[0] === cssRules.item(0)` still holds, and an
id names a rule at any depth.

**A rule object holds the rule.** `RuleState` carries the
`std::shared_ptr<SheetRule>` instead of an id to look up, which is what makes a
removed rule still readable: `deleteRule` drops the sheet's reference, the
object keeps the rule, and `rule.cssText`, `rule.media.mediaText` and
`rule.cssRules` go on answering with what the rule was — as a `CSSStyleRule`'s
declarations already did. `parentRule` is the rule a rule is inside, and
`parentStyleSheet` is the sheet object of the sheet it belongs to: `make_sheet`
now records the object it makes for a `<style>` element, so the two reads are
the same object.

**The cascade asks the query, not the parser.** `compute_block` walks a sheet's
rules and, at a `@media` rule, asks `media_query_matches` before walking the
rules inside it. That function answers with the window the page is in
(`document.viewport_width()`/`height()`, 1024×768 unless something set them),
which is what `innerWidth`, `innerHeight` and `matchMedia` already answer with.
It knows:

- the media type: `all` and `screen` match, `print` and anything else do not;
- `width`, `height`, `min-width`, `max-width`, `min-height`, `max-height`, in
  `px` (a bare number), `em`/`rem` at 16 px, `pt` and `in`;
- `orientation: portrait|landscape`;
- `resolution`, `min-resolution`, `max-resolution` in `dpi`, `dpcm`, `dppx`,
  the window being a 96 dpi screen, so `1dppx`;
- `prefers-color-scheme: light`, `prefers-reduced-motion: no-preference`,
  `prefers-contrast: no-preference`, `hover`/`any-hover: hover`,
  `pointer`/`any-pointer: fine`, `scripting: enabled`, and `(color)`;
- `not` and `only` in front of an alternative, and `,` between alternatives;
- and a feature it does not know **does not match** — the standard's answer,
  and the reason a `@media (min-resolution: 2dppx)` block stops applying to a
  1dppx screen.

**The CSSOM over it.** `CSSMediaRule` (`type` 4, `media`, `cssRules`,
`insertRule`, `deleteRule`, `cssText`, `rules` as an alias) and `MediaList`
(`mediaText`, `length`, `item`, `appendMedium`, `deleteMedium`, and indexed
reads) are new interfaces in `src/js/DomStyleBindings.cpp`.
`CSSMediaRule.insertRule` takes a style rule *or* another `@media` rule, so a
nested block is a rule inside a rule; a writer sets `media.mediaText` and the
next `getComputedStyle` is decided by the new query, because the query the
cascade asks is the string in the rule.

**What is not a rule is still a `SyntaxError`.** `@media { … }` (no query),
`@media screen` (no block), a block followed by another, and an at-rule this
engine has no interface for (`@supports`) are all refused, at the sheet and at
the media rule.

## 3. How it is verified

- `make test` → **242 / 242** suites. The new one is
  `cssom_media_rules_and_the_cascade_inside_them`: 62 assertions, all compared
  inside the page, over a sheet with six top-level rules of which four are
  `@media` blocks — the brand, `type`, `parentRule`, `parentStyleSheet` and
  `media` of a block; `mediaText`, `length`, `item(i)` and the indexed getter;
  that the matching block decides `getComputedStyle(target).color` (`green`)
  and the ones that do not (`max-width: 300px`, `print`,
  `prefers-color-scheme: dark`) change nothing; `cssText` round-tripping;
  `insertRule` of a block at an index, and its effect on the page; a rule
  inserted *inside* a block, and deleted again; `appendMedium`/`deleteMedium`
  and a `mediaText` written by hand changing whether the block applies; a
  removed block still answering its query and its rules; a nested `@media`;
  the four `SyntaxError`s and the `IndexSizeError`; and a `<style>` element's
  text re-read into a block.
- `make dom-conformance` → **678 / 678**, unchanged. jsdom implements the
  CSSOM but no cascade, so the corpus cannot ask which block applies; the
  query is asserted in the unit suite instead.
- `make js-conformance` **1128 / 1152**, `make url-conformance`
  **1188 / 1188**, `make xml-conformance` **1718 / 1718**, `make wpt`
  **9567 / 9824** in 190 files with **0** unlisted failures, `make conformance`
  **1967 / 1973**, `make diff-trees` **0** different trees in 4,500 random
  pages, `make diff-xml` **0** differing documents, `make deep` **1332 runs
  over 465 pages, 0 failed**: all unchanged.
- `make asan` → the 242 suites under AddressSanitizer and
  UndefinedBehaviorSanitizer, 0 reports.

## 4. What is still open

- **The blocks are matched against the window only.** There is no
  `window.matchMedia('(min-width: 600px)').matches` change event when the
  viewport changes, no `MediaQueryList` re-evaluation, and the features listed
  above are the whole list: `aspect-ratio`, `min-aspect-ratio`, range syntax
  (`(width > 100px)`), `and`/`or`/`not` in the new grouping syntax, and
  `@container` are not parsed.
- **`@media` in a `<link media="…">` is still a substring test.** The sheet
  level filter (`media.find("print")`) is what decides whether a `<style>`
  element's sheet is read at all; the cascade inside it is now exact, but the
  attribute is not.
- **No `CSSImportRule`, `CSSKeyframesRule`, `CSSSupportsRule`, or
  `new CSSStyleSheet()`**, and `document.adoptedStyleSheets` does not exist.
  `@keyframes` and `@import` are still not rules a script can read.
- **`style.parentRule` is still `null`** for a rule's declaration block, where
  it should be the rule.
