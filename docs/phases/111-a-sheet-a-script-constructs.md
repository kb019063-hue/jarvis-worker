# Phase 111 — a sheet a script constructs

Task row: **What the DOM corpus cannot ask** — the CSSOM part: a sheet has no
`new CSSStyleSheet()` constructor. Finding 117.

## 1. What was wrong

`new CSSStyleSheet()` is legal CSSOM, and the engine answered **"Illegal
constructor"**. `register_style_interfaces` registered the interface with a null
`construct`, which in `DomBindings.cpp` is exactly the flag for "cannot be
constructed by script":

```js
new CSSStyleSheet()
// TypeError: Illegal constructor (the standard: a sheet with no rules)
```

The infrastructure underneath was already there and already expected this case:
`SheetState`'s `owner` is documented as "Null for a sheet no element owns
(`new CSSStyleSheet()`)", `own_sheet` exists for it, and `insertRule`,
`deleteRule`, `cssRules`, `CSSMediaRule`, `CSSRuleList` and `MediaList` all work
on any `SheetState`. Only the constructor was missing — so every one of those was
reachable from script **only** through a `<style>` element the page already had.

Two things fell out of adding it:

- **`replaceSync`/`replace` did not exist**, without which a constructed sheet
  can only be filled one `insertRule` at a time.
- **A `RuleObjectsPart` entry was keyed by rule id alone.** Ids restart at 1 on
  a replace, so after `replaceSync` the sheet handed back the *old* rule object
  for id 1 — the length was right and the rule at index 0 was the previous
  text. Found by the test, not by reading.

## 2. What changed

- **`src/js/DomStyleBindings.cpp`**
  - `construct_style_sheet`: a `SheetState` with a fresh `ParsedSheet`, no
    owner, `own_sheet = true`, not applied; registered as `CSSStyleSheet`'s
    `construct`, so `new CSSStyleSheet()` and a subclass of it both work. A
    non-object argument is the `TypeError` CSSOM has for `CSSStyleSheetInit`.
  - `replaceSync`: parses its argument as a sheet, takes the rules and the id
    counter, bumps the generation so style objects read again.
  - `make_rule`: the cached object is returned only when it still wraps *this*
    rule, not merely a rule with the same id in the same sheet.
- **`tests/test_suite.cpp`** — `cssom_a_sheet_a_script_constructs`, a new
  suite of 61 assertions.

## 3. How it is verified

- **`make test`** → **268 / 268** suites; the new one is
  `cssom_a_sheet_a_script_constructs`.
- **`make js-conformance`** 1141 / 1158, **`make dom-conformance`** 678 / 678,
  **`make wpt`** 9567 / 9824 with 0 unlisted, **URL** 1188 / 1188, **XML**
  1718 / 1718 — none moved.
- With `src/js/DomStyleBindings.cpp` reverted and the test kept, the suite
  fails at its first line: `Uncaught TypeError: Illegal constructor`.
- The suite asserts, against a sheet a script builds: the constructor and its
  brand, `insertRule` at an index and at the end, the order of the rules,
  `deleteRule`, an `@media` rule with its query and the rule inside it, the
  `SyntaxError` and `IndexSizeError` CSSOM defines, `replaceSync` replacing and
  emptying, a subclass of `CSSStyleSheet`, and that a constructed sheet is
  **not** in the cascade while a `<style>` element's sheet is — measured with
  `getComputedStyle`.

## 4. What is still open

All three named in the same `AGENTS.md` row and named again there now:

- **`CSSImportRule`** — there is no `@import` rule object, and a sheet's
  `@import` is dropped when the text is read (a `<style>` with `@import url(a.css); a{…}`
  lists one rule, not two). A rule object needs a fetch and an ordered
  `styleSheet` to point at, which is a change of its own.
- **`CSSKeyframesRule`/`CSSKeyframeRule`** — `@keyframes` *is* parsed, into
  `ParsedSheet::keyframes`, and the animations use it; but it is not a rule of
  `cssRules`, so `insertRule('@keyframes …')` is a `SyntaxError` and
  `cssRules` does not list it. Making it a rule means the sheet's rule list and
  the cascade's keyframe list become one list, which is the same shape of change
  Phase 98 made for array holes and is not this one.
- **`replace`** — only `replaceSync` is here. `replace` returns a promise that
  waits for any `@import`, and since no `@import` is ever followed, the promise
  is already settled; adding it is a line once `CSSImportRule` exists, and a
  promise that resolves with a list of rules that cannot exist yet would be a
  worse answer than not having it.
