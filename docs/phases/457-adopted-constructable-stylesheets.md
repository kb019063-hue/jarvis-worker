# Phase 457 — adopted constructable stylesheets

Task: row 34's item, "a stylesheet a script builds is not in the cascade".
Finding 457; agent Arena.

## What was wrong

The row understated the defect. `document.adoptedStyleSheets` did not exist:
on a page with no script, `typeof document.adoptedStyleSheets` was
`undefined`, and a page that assigned to it created a plain JS expando — an
`Array` own property, not on the prototype, gone after `delete`. `new
CSSStyleSheet()` worked and `replaceSync` filled the sheet, but nothing ever
read it, so a constructed sheet styled nothing. Phase 111 asserted a
constructed sheet was "not in the cascade" and passed vacuously: there was no
cascade entry to be absent from.

Measured on a script-free page before the fix: `typeof
document.adoptedStyleSheets` → `undefined`, `new CSSStyleSheet()` →
`function`, `cssRules` after `replaceSync` → 1, and `getComputedStyle` still
answered the unstyled colour.

## What changed

Adopted style sheets are implemented whole: the list, the setter, and both
cascades.

- `Document` holds the adopted list as `AdoptedStyleSheet{text, generation}`
  records (`include/litebird/dom/Document.hpp`). The record carries CSS text
  rather than a sheet object because the two cascades do not share a sheet
  type: the CSSOM's `ParsedSheet` is private to `src/js/DomStyleBindings.cpp`
  and the layout model is `litebird::StyleSheet`, and there is no converter
  between them. Text is the bridge — the CSSOM side writes it back on every
  edit, and each cascade parses it.
- `document.adoptedStyleSheets` is a live `StyleSheetList` (`length`,
  `item(i)`, index access) over the document's records, handed out once per
  document so the list a script holds and the list it reads back are the same
  object. The setter validates each item: a non-sheet is a `TypeError`, and a
  sheet owned by an element (a `<style>`'s sheet) is a `NotAllowedError`.
  Adopting bumps the style generation so `getComputedStyle` re-reads.
- Every CSSOM edit (`insertRule`, `deleteRule`, `replaceSync`, …) rewrites the
  record's text through `touch_sheet` → `refresh_adopted_record`, so an edit
  after adoption reaches the cascade.
- `getComputedStyle`'s cascade collects the adopted sheets after the
  document's own, which is the order CSSOM gives them: an adopted rule beats
  the document's at equal specificity, and a later adopted sheet beats an
  earlier one. Parsed sheets are cached per record and rebuilt only when the
  record's generation moves.
- The layout cascade (`DocumentStyles::build_resolver`) parses each adopted
  record's text after the document's sheets, under the same `kMaxTotalSheets`
  cap, so the painted page and what script is told agree.

## Verification

- `make test`: **501/501 engine suites**, including the new
  `tests/suites/457-adopted-constructable-stylesheets.cpp` (3 tests): an
  adopted sheet styles the page and beats the document's sheet at equal
  specificity; `insertRule`/`deleteRule` after adoption reach the cascade; a
  second adopted sheet wins over the first; clearing the list takes the
  sheets back out; the refusals are `NotAllowedError` for an element-owned
  sheet and `TypeError` otherwise; and a page whose own script adopts a sheet
  lays out 77px tall, matching what `getComputedStyle` answers.
- Probes against the built binary: `typeof document.adoptedStyleSheets` →
  `object`, `length`/`item(0)`/index identity all hold, computed colour
  follows adoption, insertion and deletion, and `--dump-layout` shows the
  adopted height in the box tree.
