# Phase 202 — the ring nobody drew

Task: a box's `outline`, which the CSSOM reports and the painter never drew.
Finding 206.

## 1. What was wrong

```html
<div style="width:100px;height:40px;background:#f00;outline:5px solid #0f0"></div>
```

`getComputedStyle(el).outlineWidth` answered `5px`, `outlineStyle` `solid`,
`outlineColor` `rgb(0, 255, 0)`, and the display list was this:

```
  DrawRect: [0, 0, 200, 80] Color: #ff0000
```

Nothing around it, at any offset, in any style. The CSSOM's answer was right —
it is computed from the declaration block — which is exactly what made the bug
quiet: the one interface a page checks said the outline existed.

`ComputedStyle` had no outline field at all. `outline` was in the table of
shorthands (`outline` → `outline-width`, `outline-style`, `outline-color`) and
in the table of initial values, so `getComputedStyle` could put an answer
together out of the declaration text and the initial values; nothing in the
cascade ever stored it, and the painter had nothing to read. An outline was a
report, not a box.

## 2. What an outline is, and why the box model has no room for it

CSS 2.1 §16.3: "The outline is drawn 'over' the box, i.e. the outline is always
on top, and does not influence the position or size of the box, or of any other
box, and is therefore not taken into account when computing overflow." Overflow
does not clip it and no layout has a place for it — which is why it could not be
a fourth `EdgeSizes` beside `border_width`. It is one ring of one width around
the border box, at an offset that may be negative.

| declaration | border box | ring |
| :--- | :--- | :--- |
| `outline: 5px solid` (offset 0) | 100 × 40 | 5px starting at the edge, **outside** it |
| `outline: 5px solid; outline-offset: 10px` | 100 × 40 | 10px of gap, then 5px |
| `outline: 5px solid; outline-offset: -3px` | 100 × 40 | starts 3px *inside* the edge |
| `outline: 5px solid; outline-style: none` | 100 × 40 | nothing, whatever the width says |

The first row is the trap. An outline at offset 0 is *not* "a border that takes
no room": a border is inside the border box, and the outline is outside it, so
the ring band is only visible beyond the edge. `outline-offset: -3px` is how a
page pulls it back over the box.

## 3. What changed

**`ComputedStyle` has the outline**: `outline_width`, `outline_offset`,
`outline_drawn` (the style, boiled to "is there a ring") and `outline_color`. It
is in `hash()` too, for the same reason `font-kerning` is — two boxes that
differ only in their outline paint different ink, so they must not share a
cached style.

**The cascade stores it.** `apply_longhand` handles the shorthand and the four
longhands. `thin`/`medium`/`thick` are 1/3/5px as in CSS 2.1; `currentcolor` and
`initial` take `color`; `outline-style: none` clears the drawn flag but keeps
the width, which is what the CSSOM reports (`outline: none 5px` reads back
`outlineWidth: 0px` because an undrawn outline has no width — a browser does the
same). The shorthand parses its three parts in any order, parenthesis-aware so
`oklch(…)`/`rgb(…)` stay whole, the same way `border` does.

**`emit_chrome` paints the ring** as a `DrawBorderCommand` one ring wide over
the border box grown by `outline_offset + outline_width`. The ring is drawn
after the border, which is the "always on top" of §16.3, and after the
background so a negative offset covers the padding instead of being covered by
it.

## 4. Result

The display list for three boxes, before and after (`--dump-paint`):

| declaration | before | after |
| :--- | :--- | :--- |
| `outline: 5px solid #0f0` | *(nothing)* | `DrawBorder: [-5, -5, 110, 50] #00ff00` |
| `+ outline-offset: 10px` | *(nothing)* | `DrawBorder: [-15, -15, 130, 70] #00ff00` |
| `+ outline-offset: -3px` | *(nothing)* | `DrawBorder: [-2, 88, 104, 44] #00ff00` |
| `outline-style: none` | *(nothing)* | *(nothing, and `outlineWidth: 0px`)* |

`getComputedStyle` is unchanged for every case that was already right —
`4px` of `outline: thin solid` still reads `1px`, `rgb(18, 52, 86)` still reads
as itself — because the reporter is now reading the cascade instead of the
declaration text, and the two agree.

## 5. How it was verified

```
make test               # All 347 engine test suites passed successfully!
make js-conformance     # JS conformance, full corpus: 1146/1158 (99.0%)
make dom-conformance    # DOM conformance, full corpus: 678/678 (100.0%)
make wpt                # Web Platform Tests: 9567/9824 subtests pass in 190 files
make render-conformance # Canvas toDataURL: 45016/45016 checks agree
make check-docs         # check_docs: 0 failure(s)
make check-numbers      # check_numbers: 0 failure(s)
```

The new suite is `the_painter_draws_the_outline_the_layout_reserved_no_room_for`.
It renders to a `BitmapRenderer` and reads pixels: the box keeps its declared
size (the ring takes no room), the ring is where the offset puts it (green at
the edge at offset 0, red fill and a white gap and then green at offset 10, no
ring at all for `outline-style: none`), and the same four declarations read
back through `getComputedStyle` as the values that were painted. It aborts on
the engine before this phase at its first ring assertion and passes here.

## 6. What is still open

- Only the solid ring is drawn. `dashed`, `dotted`, `double`, `groove`,
  `ridge`, `inset` and `outset` are accepted and remembered, and paint as a
  solid ring — as a border would, since the border painter has the same limit.
- `outline-offset` is honoured by the geometry, but an outline that is clipped
  by the viewport is clipped, as it is for any other ink; §16.3's "not taken
  into account when computing overflow" is about layout, and it is.
- The ring is drawn outside any `border-radius` rounding: a rounded box gets a
  square ring. `DrawBorderCommand` takes a `border_radius` and the outline
  passes none, because the ring's radius would have to be the border box's plus
  the offset.
