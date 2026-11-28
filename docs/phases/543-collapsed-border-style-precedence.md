# Phase 543 — equal-width collapsed borders use style precedence

**Finding:** [`docs/findings/543-collapsed-border-style-precedence.md`](../findings/543-collapsed-border-style-precedence.md)

CSS 2.1 §17.6.2.1 resolves collapsed-border conflicts by width first, then
style. The cell-border resolver already propagated the winning width and
colour to the two cells sharing an edge, but broke equal-width ties solely by
iteration order. The painting command then used each cell's own declared
style, so a style winner could not be visible on both halves of the shared
edge.

## What changed

- The cell-edge resolver now compares width, then CSS 2.1 style precedence:
  `hidden` > `double` > `solid` > `dashed` > `dotted` > `ridge` > `outset` >
  `groove` > `inset` > `none`. For equal priority and width, traversal order
  remains the final tie-break.
- A resolved per-edge `EdgeStyles` value travels with the already-resolved
  width and colour in `LayoutBox::border_paint_override`.
- Border command construction uses that override, so both cells paint their
  half with the winning style, colour and width.
- `tests/suites/543-collapsed-border-style-precedence.cpp` covers each
  adjacent style-priority transition at equal declared width, checks that the
  winning style and width reach both cells, and checks rendered ink for a
  dashed-versus-solid tie.

## Scope

This is the cell-border, equal-width style tie-break. The existing resolver
still does not model border-origin precedence (cell over row over table),
full `hidden` suppression against wider borders, or table/row borders as
conflict candidates. Same-width, same-style colour ties retain traversal
order. Those are separate parts of the CSS table-border algorithm.
