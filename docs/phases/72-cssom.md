# Phase 72 — the CSS Object Model: a sheet a script can read and write

Task row: **What the DOM corpus cannot ask** — "style sheet objects
(`sheet.cssRules`, `insertRule`)". Finding 78.

## 1. What was wrong

`HTMLStyleElement.sheet` answered `null`:

```js
var style = document.createElement('style');
document.head.append(style);
style.sheet          // null
style.sheet = null   // and there was nothing to insert a rule into
```

`document.styleSheets` did answer something, and it was worse than nothing: a
plain `{ownerNode, disabled, type}` object per `<style>` element, made fresh on
every read, with no `cssRules`, no `cssText`, and no relationship to the rules
the page was actually styled by. A `window.CSSStyleSheet`, `CSSRule` or
`CSSRuleList` did not exist at all.

The rules were there. `getComputedStyle` has parsed `<style>` text into
`SheetRule{SelectorList, RawDeclaration}` lists since Phase 27 (`ParsedSheet`
in `src/js/DomStyleBindings.cpp`), and that parser is a good one: it skips
`@media` blocks that do not apply, understands `@supports`/`@layer`, and
rejects a selector it cannot parse. What was missing was the object model over
it — and, because of that, every WPT test that needs one:

- `tests/wpt/css/support/computed-testcommon.js` calls
  `sheet.insertRule("#target" + pseudo + "{}")` in
  `test_pseudo_computed_value`;
- `tests/wpt/css/support/parsing-testcommon.js` builds a sheet with
  `insertRule`, reads `cssRules[0].selectorText`, sets it on another and
  compares — the whole *serialization* half of the grid and flexbox parsing
  suites;
- two grid tests (`grid-columns-rows-get-set-multiple.html`,
  `grid-content-sized-columns-resolution.html`) gate ~80 already-written
  subtests behind `document.fonts.ready.then(...)`, a global that did not
  exist either.

So the scoreboards had a hole in them that looked like a smaller hole: those
two files reported one `(harness)` row each and ran none of their subtests.

## 2. What changed, piece by piece

**The model is the cascade's, not a copy of it.** `ParsedSheet` is what
`compute_block` reads, so `CSSStyleSheet.cssRules` is a view over that same
object: a rule inserted from script is a rule the next `getComputedStyle`
applies, with no invalidation step between them. `StyleState` (the
`CSSStyleDeclaration` behind `element.style`) gained a third mode beside
"inline" and "computed": *a rule's block*, where `load()` reads the rule's
`RawDeclaration`s into a `Block` and `store()` writes them back. `rule.style`
is that mode, so the shorthand machinery (expanding `margin: 0 auto` into four
longhands, canonicalizing them, putting them back together) works for a rule
exactly as it does for `style="…"`, and `rule.cssText` is serialized by it —
`margin: 0 auto` comes back as `margin: 0px auto;`, as browsers print it.

**A rule object is a number, not a position.** `SheetRule` carries an `id`
and a rule object holds the sheet and that id, so:

- `cssRules[0] === cssRules.item(0)` — the object is made once per id and kept
  on the realm (`RuleObjectsPart`);
- a rule object survives the list moving under it (`insertRule('b{}', 0)`
  makes the old `cssRules[0]` be `cssRules[1]`, and the object follows it);
- a deleted rule keeps its declarations (`detached`), so a script holding it
  can still read `cssText`.

**The element's text is the sheet's rules.** A `<style>` element's `sheet` is
one object for as long as the element has one. When the element's text changes
(`style.textContent = '…'`), the sheet is reparsed *in place*: the number and
identity of the sheet stay, the rules that came from the old text go, and the
rules a script inserted stay (at the end). That is why the object a page holds
does not go stale, and why `insertRule` followed by a text change does not
throw the insertion away.

**`disabled`.** `sheet.disabled = true` takes a sheet out of the cascade and
leaves it in `document.styleSheets` and in `cssRules`. It is stored per sheet
in the realm (`StylePart::disabled_sheets`) rather than on the element, because
what was disabled is the object the script wrote to.

**`sheet` is null out of the document.** As the HTML standard has it:
`style.sheet` is null while the element is not in a document, and the object
outlives it for a script that kept a reference (`held = sheet;
style.remove(); held.cssRules.length` still answers). Putting the element back
hands out the same object.

**The interfaces.** `CSSStyleSheet` (`cssRules`/`rules`, `ownerNode`,
`ownerRule`, `href`, `type`, `title`, `disabled`, `insertRule`, `deleteRule`,
`removeRule`, `addRule`), `CSSRuleList` (live length, `item`, indexed access),
`CSSRule` (all fourteen `type` constants, on the subclass too, and
`parentRule`/`parentStyleSheet`), `CSSStyleRule` (`type`, `selectorText` with
the `SyntaxError` the standard names, `style`, `cssText`).

`insertRule` refuses with the errors the standard names: `IndexSizeError` past
the end, `SyntaxError` for text that is not one style rule (no closing brace,
an at-rule, two blocks, a selector that does not parse).

**`document.fonts`.** A `FontFaceSet` with `status` (`"loaded"`), `ready` (a
promise resolved with the set), `size`, `add`/`delete`/`has`/`clear`/`forEach`.
There is no `FontFace` and no `@font-face`; every face the engine draws with
was found on the host already, so there is never anything to wait for and
`ready` is resolved when it is asked for.

## 3. How it is verified

- **`make test`** → **211 / 211** suites. The new one,
  `cssom_sheets_rules_insert_amounts_and_fonts`, drives a page and asserts
  **60** facts, every one of them a comparison made in the page: the objects
  and their brands, that `insertRule` styles the page, which rule wins by
  specificity and order, `rule.style` writing through, `selectorText`,
  `deleteRule`, the seven error cases, a rule object following its rule,
  `disabled`, `sheet` out of the document, a text change, and `document.fonts`.
- **`make wpt`** → **9567 / 9824** in 190 files, up from 9550 / 9744, with
  **0** failures that are not listed. The `document-fonts` reason is gone from
  `tests/wpt/expected-failures.txt`: the two files it stood for now run, and
  their failures are listed one by one as `grid-resolved-tracks` — 65 new rows,
  and **17** subtests that pass where none ran at all.
- **`make dom-conformance`** → 669 / 669, unchanged (jsdom implements none of
  this: `compare_with_jsdom.py` would take the first of it as a difference, so
  the CSSOM is asserted in the unit suite instead).
- **`make js-conformance`** 1124 / 1150, **`make url-conformance`** 1188 / 1188,
  **`make xml-conformance`** 1718 / 1718: unchanged.
- `Makefile`: `WPT_FLOOR` 9538 → **9567**.

## 4. What is still open

- **No `CSSMediaRule`.** An `@media` block is skipped when a sheet is read (it
  is a rule that may not apply), so a sheet's `cssRules` is the style rules
  that applied and `insertRule('@media …')` is a `SyntaxError`. A browser has
  the rule and matches it lazily.
- **No constructors.** `new CSSStyleSheet()`, `new CSSStyleSheet({...})` and
  `document.adoptedStyleSheets` do not exist, so a sheet only ever comes from
  an element.
- **No `CSSImportRule`, `CSSKeyframesRule`, `CSSStyleDeclaration.parentRule`.**
  `rule.parentStyleSheet` answers a sheet object; `style.parentRule` still
  answers `null` for a rule's block.
- **The resolved grid track list is still the specified one** — that is what
  the 185 `grid-resolved-tracks` rows are about, and the row for it in
  `AGENTS.md` is unchanged. This phase made them *runnable*, not passing.
