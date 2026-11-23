# Finding 219 — the icon that had no box

**Severity:** high for real pages, low for the spec — an element whose only
content is a `::before`-supplied glyph computed a **zero box**, painted
nothing, and could not be hit-tested. Every icon-font button, link and menu
item on the web is that element.

**Found by:** taking the row's end-to-end oracle and running it. The row said
the Rust Book's "Next chapter" anchors "compute a zero box, paint nothing and
are not hit-testable"; `holdout-01` recorded them as **760×0 and 0×0**.

## What was wrong

Three separate gaps, each of which alone would have stopped the box existing:

1. **The selector never matched.** `\(::before\)` set `has_pseudo_element`, and
   `match_selector_impl` returns false at once for any selector with it
   ("pseudo-elements are rejected in simple-selector matching"), so a
   `::before` rule styled nothing. There was also no record of *which*
   pseudo-element a selector named — just a bool.
2. **There was no `content` property.** `apply_declaration` had no branch for
   it, and `ComputedStyle` had no member to hold a value.
3. **Nothing generated a box.** Layout had no notion of a pseudo-element, so
   even a resolved `content` would have gone nowhere.

## Measured before

```
$ ./build/litebird --dump-layout icon.html     # .fa-angle-right:before{content:"\f105"}
BlockBox [x=0 y=0 w=800 h=52]
  BlockBox [x=0 y=8 w=800 h=0]          <- the anchor: no height at all
    InlineBox [x=0 y=8 w=0 h=0]
  BlockBox #plain [x=0 y=24 ...]        <- an anchor with real text: 103.95 wide
```

`800×0` against `103.95` — the holdout's "760×0 and 0×0", reproduced.

## The fix

- **`Selector::pseudo_element`** records which one (1 `::before`, 2 `::after`);
  the parser writes it through a member rather than a fourth out-parameter.
- **`resolve_pseudo_element(element, which, base)`** computes the
  pseudo-element's style: it inherits everything from the element, starts as an
  inline box, and takes only the declarations of selectors that name that
  pseudo-element. `matches_selector_for_pseudo` does the matching by copying
  the selector, clearing the pseudo-element on the last part **and removing its
  `before`/`after` pseudo-class entry** — that entry, not only the bool, is what
  `matches_simple_selector` rejects by name.
- **`content`** is a `ComputedStyle` member and a parser branch. Only a string
  is represented: `counter()`, `attr()` and `url()` generate **nothing**, which
  is the honest answer for a value the engine cannot mean, rather than the
  literal text of the declaration.
- **`build_boxes`** appends the `::before` run, then the children, then the
  `::after` run — the DOM order the pseudo-elements take — as ordinary
  `TextBox`es with no node of their own. Nothing is inserted into the document.

## Measured after

```
BlockBox [x=0 y=8 w=800 h=20] IFC lines=1 first_line=(0,8 w=9.60156 ...)
  InlineBox [x=0 y=8 w=0 h=0]
    TextBox [x=0 y=8 w=0 h=0] text="\uf105"    <- U+F105, from the CSS escape
```

The anchor has height, a line, and a text box. `#a::before{content:"XXX"}` puts
`XXX` before `mid` and `YYYY` after `mid2`, and the line width grows 30.2 → 63.1.

**The DOM is untouched**, which is the promise a pseudo-element makes: the
anchor's `textContent` is still empty and `#b`'s is still exactly `"mid"`.

| | before | after |
|---|---|---|
| icon-only anchor's box | `800×0` | `800×20` with a line |
| generated text in the box tree | none | `TextBox` per pseudo-element |
| engine suites | 354 | **355** |
| render reftests | 48/48, 41/41 | 48/48, 41/41 |
| tree-construction · JS · DOM | — | 1973/1973 · 1146/1158 · 678/678 |

## What is not claimed

- **The glyph itself is not drawn.** U+F105 is a Font Awesome codepoint and
  the fallback font has no glyph for it, so the run measures 9.6px of what it
  can draw. The row is explicit that "glyph-level correctness matters less than
  the box existing and being hit-testable" — the box is what this phase
  delivers, and a real icon font would draw the glyph.
- **Only a string-valued `content`.** No `counter()`, `attr()`, `url()`,
  `open-quote`, or the `content` list form; a value outside a string generates
  nothing rather than something wrong.
- **`::first-line` and `::first-letter` still match nothing.** They set
  `has_pseudo_element` and leave `pseudo_element` 0, so no box is made.
- **`::marker`, `::placeholder`, `::selection`** are not addressed.
- **Hit-testing was not exercised through the DOM API** in this phase: the
  claim rests on the box having height and a line, which is what the holdout
  found missing. A click driver against the real page was not run.
- **The holdout itself was not re-run** end to end; this phase reproduces its
  zero-box symptom on a minimal page and fixes that, not the four holdout tasks.
