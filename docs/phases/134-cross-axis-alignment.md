# Phase 134 — a line is as tall as its container

Task row: **Cross-axis alignment is not implemented** (`align-items` ignored in
flex, grid items not stretched to their area). Finding 139.

Phase 122 put a pixel oracle against flexbox and grid and found four bugs. One
of them was this: `align-items` was ignored, so an item with no height of its
own was 0 tall and painted nothing, and `center` left an item at the top of a
100 px container. Three `known_failure` pages recorded it. This phase fixes it
and the three pages lose their reasons.

## 1. What was wrong

`align-items` was **parsed correctly** — `StyleResolver.cpp` maps `stretch`,
`flex-start`/`start`, `flex-end`/`end` and `center` onto `AlignItems` — and it
was consulted in exactly one place, the row-direction line loop of
`LayoutBox::layout_flex`, against `cur_line_max_h`: the tallest item's margin
box on that line.

That reference size is degenerate for both of the things the property is for:

| `align-items` | What the code computed | Why it moved nothing |
| :--- | :--- | :--- |
| `center` | `(tallest item - item) / 2` | for a lone tall item that is `(20 - 20) / 2 = 0`; the line is the item |
| `flex-end` | `tallest item - item` | same zero |
| `stretch` (the default) | `0` unless some *other* item was taller | an empty item is the tallest item on a line of empty items |

So the switch was live and every branch of it was a no-op. The item's own
height was never the question; a flex line's cross size is the *container's*
inner cross size when the container has one and the line is alone in it
(CSS Flexbox §9.4, with `align-content: stretch` as the default) — the tallest
item is only a floor.

The other two paths had no cross-axis step at all:

- a **column** container laid each item out at the full content width and left
  it at `x = 0`, whatever `align-items` said;
- **grid** placed each item in the right cell and never gave it the cell's
  height, so a grid of empty items rendered as correctly positioned nothing.

## 2. What changed

`src/layout/LayoutBox.cpp` only. No new property, no new file, no header
change: the styles already carried the answer.

1. **Row containers.** A definite height on the container becomes
   `container_cross` (its content height, so `box-sizing: border-box`
   subtracts the vertical border and padding). A line that is alone in the
   container then takes `line_cross = max(tallest item, container_cross)`, and
   all four `align-items` values are measured against `line_cross`, as is the
   line advance for the next line. Several lines still only get their own
   heights: `align-content` is what shares a container's height between lines,
   and it is not implemented (see §4).
2. **Column containers.** The cross axis is the width. `stretch` fills an auto
   width — a block item already did that through `layout_block`, an
   `inline-block` item did not — and `center`/`flex-end` offset the item across
   the container's content width.
3. **Grid containers.** After the row tracks have settled (an item's height is
   what made its row that tall in the first place), each item learns the height
   of its area — the rows it spans plus the gaps between them — and
   `align-items` applies in the block axis: `stretch` gives an auto-height item
   the area, `center` and `flex-end` place it inside it.

## 3. What it does

`tests/render/reftests.json`: **34 pages, 121 assertions** (was 26 and 100).

- The three pages Phase 122 recorded are green and their `known_failure`
  entries are gone: `flex_items_stretch_to_the_container_height` (a 40 px
  container makes a height-less item 40 tall),
  `flex_align_items_center_centres_a_shorter_item` (`(100-20)/2 = 40`) and
  `grid_items_stretch_to_their_area` (60×30 and 40×30 cells, items filling
  them).
- Six new pages cover what the three did not: `flex-end` on a row
  (`y = 80`), a stated height surviving `stretch` (the guard),
  `center` and `flex-end` across a **column** container (`x = 30`, `x = 60`),
  `center` in a grid row (`y = 10`), and the arithmetic of the stretch itself —
  a 3 px border and 5 px padding come out of the 40 px before the content box
  gets its 32.

Five of those six were red on the old binary, and the sixth
(`flex_align_items_stretch_leaves_a_definite_height_alone`) was green on
purpose: `stretch` must not overwrite a cross size that is already definite,
and a fix that does is the next bug. Measured by putting the old
`LayoutBox.cpp` back:

```
$ git checkout src/layout/LayoutBox.cpp && make -j && make render-conformance
FAIL flex_items_stretch_to_the_container_height
FAIL flex_align_items_center_centres_a_shorter_item
FAIL grid_items_stretch_to_their_area
FAIL flex_align_items_flex_end_puts_the_item_at_the_cross_end
FAIL flex_column_align_items_center_centres_the_item_across
FAIL flex_column_align_items_flex_end_pushes_the_item_to_the_right
FAIL grid_align_items_center_centres_an_item_in_its_row
FAIL flex_stretch_subtracts_the_padding_and_the_border
Render reftests: 21/32 pages match their reference pixels; 3 more fail as
recorded, with a reason in the corpus (24 of 32 accounted for)
$ ...with the fix back in place...
Render reftests: 29/32 pages match their reference pixels ...
```

The same statements are asserted in the unit suite, where they are numbers
rather than pixels: `cross_axis_alignment_places_flex_and_grid_items`, nine
blocks, **296 suites** (was 295).

## 4. What is still open

Two more gaps turned up while measuring this one, and neither was fixed here:
a bug found outside the task becomes a row of `AGENTS.md`, and both are now
rows with a `known_failure` page behind them so the next phase starts with an
oracle rather than an anecdote.

- **`justify-content` is ignored in a column container.** Rows distribute on
  the main axis; the column branch has no main-axis step and stacks from the
  top. `flex_column_justify_content_flex_end` records it.
- **`align-self` and `align-content` are not read.** A container's
  `align-items` is the only cross-axis input. `flex_align_self_overrides_align_items`
  records the first; `align-content` has no page yet, and with it the
  multi-line sharing of a container's height noted in §2.
- **A stretched item's children are not stretched by it.** The stretch sets the
  item's content height; its own subtree was laid out before that and keeps its
  positions. A block item with `height: auto` that fills its container would, in
  a browser, hand that height to a percentage-height descendant; here it does
  not.
- `justify-items` is unimplemented and unmeasured, and the inline axis of a
  grid container is not asserted anywhere.
- Everything Phase 122 listed as unmeasured is still unmeasured: inline
  formatting, table cell backgrounds, SVG, canvas, glyph shape, WPT reftests,
  a two-build pixel diff.
