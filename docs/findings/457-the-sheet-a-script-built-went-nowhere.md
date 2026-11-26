# Finding 457 — the sheet a script built went nowhere

Phase 457; agent Arena.

## What was wrong

`new CSSStyleSheet()` and `replaceSync` existed since Phase 111, but
`document.adoptedStyleSheets` did not. On a script-free page the property was
`undefined`; on a page that assigned to it, the assignment made an `Array`
own-property expando — `hasOwnProperty` true, not on the prototype, gone
after `delete`. A constructed sheet could be filled with rules and then
styled nothing: no cascade read it, in either the CSSOM's or the layout's.
Phase 111's assertion that a constructed sheet was "not in the cascade"
passed vacuously, because there was no cascade entry to be absent from.

## What changed

The adopted list lives on `Document` as CSS text plus a generation, because
the two cascades do not share a sheet type: `ParsedSheet` is private to
`src/js/DomStyleBindings.cpp` and the layout model is `litebird::StyleSheet`.
The CSSOM side serialises each adopted sheet back to text on every edit
(`touch_sheet` → `refresh_adopted_record`); `getComputedStyle`'s cascade and
`DocumentStyles::build_resolver` each parse that text after the document's
own sheets. `document.adoptedStyleSheets` is a live `StyleSheetList`; the
setter throws `TypeError` for a non-sheet and `NotAllowedError` for an
element-owned sheet, and bumps the style generation so computed style
re-reads.

## Verification

`make test` reports **501/501 engine suites**. The new suite
(`tests/suites/457-adopted-constructable-stylesheets.cpp`) asserts the
adopted sheet styles the page, wins over the document's sheet at equal
specificity, tracks `insertRule`/`deleteRule` after adoption, refuses an
element-owned sheet with `NotAllowedError`, and lays out the adopted height.

## Still open

`CSSStyleSheet` construction from a base URL, `replace()` (the promise, which
waits on `@import`), and `@import` inside an adopted sheet are not
implemented; an adopted sheet's text is parsed as a plain sheet, so an
`@import` in it is dropped the same way it is in a `<style>`'s.
