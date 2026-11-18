# Finding 78 — there was no CSS Object Model

Phase 72. The architecture is in
[`docs/phases/72-cssom.md`](../phases/72-cssom.md).

## 1. What was wrong

`<style>.sheet` was `null`, and `document.styleSheets` answered a plain object
per `<style>` element — `{ownerNode, disabled, type}`, made fresh on every
read, with no `cssRules`. Not that `cssRules` was empty: the property did not
exist. `CSSStyleSheet`, `CSSRule` and `CSSRuleList` were not globals.

The engine had the rules all along. `getComputedStyle` has read `<style>` text
into `ParsedSheet` since Phase 27; what was missing was an object model over it,
so a script could not read a rule, write one, or add one, and two WPT support
scripts (`css/support/parsing-testcommon.js`,
`css/support/computed-testcommon.js`) built their fixtures with `insertRule` and
could not run.

`document.fonts` was missing too, which is the same kind of hole seen through a
different hole: two grid test files call `document.fonts.ready.then(…)` at
their top level, so the harness reported one `(harness)` row for each and ran
**none** of the ~80 subtests already written in them.

## 2. What changed

- **`src/js/DomStyleBindings.cpp`** — the model itself. `ParsedSheet` gained a
  `generation` (bumped by every CSSOM write) and rule ids; `SheetState`,
  `RuleState`, `RuleListState` and `RuleObjectsPart` are the script-visible
  state; `StyleState` gained the "a rule's block" mode; `document_style_sheets`,
  `style_sheet` and `document_fonts` are the entry points. The cascade honours
  `disabled` and reads a sheet a script may have written to.
- **`src/js/DomHtmlBindings.cpp`** — `HTMLStyleElement.sheet` answers the sheet
  object instead of `null`.
- **`src/js/DomDocumentBindings.cpp`** — `document.styleSheets` answers the
  real sheet objects, and `document.fonts` exists.
- **`include/litebird/js/DomBindings.hpp`** — the three declarations.
- **`tests/test_suite.cpp`** — `cssom_sheets_rules_insert_amounts_and_fonts`.
- **`tests/wpt/expected-failures.txt`** — the two `document-fonts` rows are
  gone; the 65 failures of those two files are listed as `grid-resolved-tracks`.
- **`Makefile`** — `WPT_FLOOR` 9538 → 9567.

## 3. How it is verified

- `make test` → **219 / 219** suites (`cssom_sheets_rules_insert_amounts_and_fonts`
  asserts 60 facts, all of them compared in the page).
- `make wpt` → **9567 / 9824** in 190 files, 0 unlisted failures (was
  9550 / 9744).
- `make dom-conformance` 678 / 678, `make js-conformance` 1126 / 1152,
  `make url-conformance` 1188 / 1188, `make xml-conformance` 1718 / 1718.

## 4. What is still open

- `@media` rules are skipped when a sheet is read: no `CSSMediaRule`, and
  `insertRule('@media …')` is a `SyntaxError`.
- No `new CSSStyleSheet()`, no `document.adoptedStyleSheets`, no
  `CSSImportRule`/`CSSKeyframesRule`, and `style.parentRule` is still `null`.
- No `FontFace` and no `@font-face`: the set is always empty and always
  `"loaded"`.
- The *resolved* grid track list is still the specified one; the 185
  `grid-resolved-tracks` rows are unaffected by this phase beyond becoming
  runnable.
