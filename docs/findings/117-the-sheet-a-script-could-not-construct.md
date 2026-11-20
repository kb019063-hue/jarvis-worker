# Finding 117 — the sheet a script could not construct

Phase 111. The architecture is in
[`docs/phases/111-a-sheet-a-script-constructs.md`](../phases/111-a-sheet-a-script-constructs.md).

## 1. What was wrong

`new CSSStyleSheet()` is how CSSOM lets a page write rules without an element to
put them in. The engine answered:

```
new CSSStyleSheet()
TypeError: Illegal constructor
```

`register_style_interfaces` passed a null `construct` for the interface, and in
`DomBindings.cpp` that null *is* the flag for "a script may not construct this".
So the only sheets a script could reach were the ones a `<style>` element
already owned, and everything the CSSOM offers on a sheet — `insertRule`,
`deleteRule`, `cssRules`, `CSSMediaRule`, `CSSRuleList`, `MediaList` — was
reachable only through one.

Nothing underneath was missing. `SheetState`'s `owner` says "Null for a sheet no
element owns (`new CSSStyleSheet()`)", `own_sheet` exists for exactly that case,
and every method works on any `SheetState`. The row had been written up as
"no `new CSSStyleSheet()` constructor" and it was one registration away.

## 2. What changed

- **A constructor** (`construct_style_sheet`): a fresh `ParsedSheet`, no owner,
  not in the cascade. Registered as the interface's `construct`, so a subclass
  works too — `class Mine extends CSSStyleSheet {}` gets a working sheet.
- **`replaceSync`**, which is how a constructed sheet takes a whole text at
  once; without it the only way to fill one is `insertRule` per rule.
- **A cache keyed by the wrong thing.** `make_rule` kept one object per rule id
  in a per-realm map and answered an id hit as long as the sheet matched. Ids
  restart at 1 in every `replaceSync`, so after a replace the sheet returned the
  object it had made for the *previous* rule with that number: `cssRules.length`
  said 1 and `cssRules[0].selectorText` said `"a"` when the text was
  `c { color: green }`. The entry is now checked against the rule itself.

The third was found by the test and not by reading the code, which is the
argument for writing the test first.

## 3. How it is verified

- `make test` → **268 / 268**; the new suite is
  `cssom_a_sheet_a_script_constructs`, 61 assertions.
- With `src/js/DomStyleBindings.cpp` reverted and the test kept, the suite fails
  at its first line, `Uncaught TypeError: Illegal constructor`.
- `make js-conformance` 1141/1158, `make dom-conformance` 678/678,
  `make wpt` 9567/9824 with 0 unlisted, URL 1188/1188, XML 1718/1718: unchanged.
- The suite checks the rule order and the errors (`SyntaxError` for a rule that
  is not one, `IndexSizeError` for an index past the end), that `cssRules` is
  live, that one object per rule holds, that an `@media` rule inserted by a
  script carries its query and its child rule, that `replaceSync` replaces and
  empties, and — with `getComputedStyle` on a real `<p>` — that a constructed
  sheet styles nothing while a `<style>` element's sheet styles the page.

## 4. What is not here

- **`CSSImportRule`.** There is no `@import` rule object, and a sheet's
  `@import` is dropped when the text is read: `<style>@import url(a.css); a{…}</style>`
  lists one rule where there should be two. A rule object would need a fetch and
  an ordered `styleSheet`, so it is a change of its own.
- **`CSSKeyframesRule`.** `@keyframes` is parsed, into `ParsedSheet::keyframes`,
  and the animations read it — but it is not a rule of `cssRules`, so
  `insertRule('@keyframes …')` throws and a sheet with only a keyframes block
  lists no rules. Making it a rule means one list serving both the CSSOM and the
  cascade.
- **`replace`.** Only `replaceSync`. `replace`'s promise waits on `@import`,
  which does not exist yet; a promise resolving with an empty rule list would be
  a wrong answer rather than an absent one.
