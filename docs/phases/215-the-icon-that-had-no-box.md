# Phase 215 — the icon that had no box

Task: the row that began "`::before` and `::after` parse but never generate".
Finding 219 (`docs/findings/219-the-icon-that-had-no-box.md`).

## 1. The oracle was in the row

The row ends with what to test against: the Rust Book's "Next chapter" anchors,
whose only content is `<i class="fa fa-angle-right"></i>` and a
`.fa-angle-right:before{content:"\f105"}` rule. `holdout-01` had recorded them
as **760×0 and 0×0**. Two `<div>`s reproduce it exactly:

```
BlockBox [x=0 y=0 w=800 h=52]
  BlockBox [x=0 y=8 w=800 h=0]          <- icon-only anchor, 800 wide and no height
    InlineBox [x=0 y=8 w=0 h=0]
  BlockBox #plain [x=0 y=24 ...]        <- text anchor, 103.95 wide
```

## 2. Three gaps, measured one at a time

**The selector never matched.** `matches_selector_impl` begins
`if (selector.parts.empty() || selector.has_pseudo_element || !element) return
false;` — every `::` selector is refused, which is right when asking what an
*element*'s style is and wrong when asking whether the element owns a
`::before`. And `Selector` recorded only `has_pseudo_element`, a bool with no
answer to "which one".

**There was no `content`.** No branch in `apply_declaration`, no member in
`ComputedStyle`. `content` was matched in the `@supports` keyword whitelist, so
a feature query claimed support the property did not have.

**Nothing generated.** Layout had no notion of a pseudo-element at all.

## 3. The fix, and the part that took two attempts

`Selector::pseudo_element` carries 1 for `::before` and 2 for `::after`;
`resolve_pseudo_element` computes the pseudo-element's own style from the
element's, taking only selectors that name it; `ComputedStyle::content` holds a
string; `build_boxes` appends the before run, the children, the after run.

The part that took two attempts is worth recording, because the first version
looked right and resolved nothing. `matches_selector_for_pseudo` copies the
selector, clears `has_pseudo_element`, and matches — and still returned false.
The reason: the parser records the pseudo-element **twice**, once as the
selector's flag and once as a pseudo-class of the last simple selector, and it
is the pseudo-class entry that `matches_simple_selector` rejects:

```cpp
} else if (name == "before" || name == "after" || name == "first-line" || name == "first-letter") {
    return false;
}
```

A debug print (since removed) showed `pe=1 matches=0` for a selector whose id
matched, which is what pointed at the second record. The fix removes exactly
that entry, from the last part only.

## 4. Generation is layout-only

The run is an ordinary `TextBox` with **no node**. Nothing is inserted into the
document, so `textContent`, `childNodes` and every live collection stay as the
page made them — the anchor's `textContent` is still empty and `#b`'s is still
`"mid"`. The suite asserts both.

## 5. Measured after

```
BlockBox [x=0 y=8 w=800 h=20] IFC lines=1 first_line=(0,8 w=9.60156 ...)
  InlineBox [x=0 y=8 w=0 h=0]
    TextBox [x=0 y=8 w=0 h=0] text="\uf105"
```

The anchor has height and a line. `#a::before{content:"XXX"}` / `#b::after
{content:'YYY'}` put `XXX` before `mid` and `YYY` after `mid2`; the line grows
30.2 → 63.1.

```
engine suites        355/355 (the new suite among them)
render reftests      48/48 and 41/41 accounted
tree-construction    1973/1973
JS conformance       1146/1158 (99.0%)
DOM conformance      678/678
everyday probes      90/90 and 68/68
```

## 6. What is not claimed

- **The glyph is not drawn.** The fallback font has no U+F105, so the run
  measures the 9.6px it can. The row says the box matters more than the glyph;
  a real icon font would draw it.
- **Strings only.** `counter()`, `attr()`, `url()` and the content list form
  generate nothing — deliberately, rather than the literal declaration text.
- **`::first-line`, `::first-letter`, `::marker`, `::placeholder`,
  `::selection`** are untouched.
- **No click driver was run.** The geometry claim is the box and its line; the
  holdout's own driver was not re-run end to end.

## Files

- `include/litebird/css/StyleSheet.hpp` — `Selector::pseudo_element`
- `include/litebird/css/CssParser.hpp`, `src/css/CssParser.cpp` — which one
- `include/litebird/css/StyleResolver.hpp`, `src/css/StyleResolver.cpp` —
  `ComputedStyle::content`, `resolve_pseudo_element`,
  `matches_selector_for_pseudo`, `parse_content_string`
- `src/layout/LayoutTree.cpp` — the generated runs, in DOM order
- `tests/test_suite.cpp` — `pseudo_elements_generate_a_box_that_can_be_hit`
- `AGENTS.md`, `AUDIT.md` — the row and the finding
