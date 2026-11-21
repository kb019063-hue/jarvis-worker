# Finding 138 — the line was as tall as its tallest item

Phase 133. Status: **found and fixed** for `align-items` in flex rows, flex
columns and grid; found and **left open** for `justify-content` on a column's
main axis and for `align-self`/`align-content`, which are now rows of
`AGENTS.md` with a recorded page each.

## 1. What was wrong

A property can be parsed, stored, and still wrong, if the quantity it is
measured against means nothing. `align-items` was in that state.

```
$ grep -n 'align-items' src/css/StyleResolver.cpp
1876:    } else if (prop == "align-items") {
1877:        if (val == "stretch") style.align_items = AlignItems::Stretch;
1878:        else if (val == "flex-start" || val == "start") style.align_items = AlignItems::FlexStart;
1879:        else if (val == "flex-end" || val == "end") style.align_items = AlignItems::FlexEnd;
1880:        else if (val == "center") style.align_items = AlignItems::Center;
```

Four values, all reaching `ComputedStyle`, all hashed into the style cache so a
change invalidates layout. And one reader: the row-direction loop of
`layout_flex`, which measured the item against `cur_line_max_h` — the tallest
item's margin box on the line.

For `center` that computes `(tallest - item) / 2`, which is 0 whenever the item
*is* the tallest or is alone on the line. For `stretch` the code read

```cpp
if (cs.height < 0.0f && cur_line_max_h > 0.0f) { ... }
```

so an item with no height, alone on a line, needed some *other* item to be
taller before it could be stretched — and if every item on the line has no
height, the line is 0 tall and nothing is stretched. The default value of the
property made empty flex rows and empty grid cells paint nothing at all:

```html
<div style="display: flex; width: 100px; height: 40px; background: #eeeeee">
  <div style="width: 40px; background: #ff0000"></div>
</div>
```

correct in a browser (a 40×40 red square on the left of a 40 px band), and a
blank band here until this phase. `grid` was the same story with a different
cause: the item was laid out before its row's height was known and never told
about it afterwards, so a grid rendered as correctly placed nothing.

### 1.1 Why nothing had caught it

Because no textual scoreboard asks what a pixel is. The tree is right, the
computed style is right, `getComputedStyle` returns `align-items: center`, and
the DOM corpus and WPT's script tests are all satisfied. A layout that paints
nothing throws nothing, and the page still has the nodes it should have.

It was caught by Phase 122's pixel oracle, which is the only thing in the tree
that asks *what colour is this point*. It is the same lesson as finding 121
(there were no pixels measured at all) arriving one level down: measuring the
box model and the main axis does not measure the cross axis, and the recorded
failure was the only reason anyone knew.

### 1.2 Measuring against the wrong size is not a small bug

The line's cross size has two contributors, and the code had one:

- the tallest item on the line — a **floor**, because a line must be tall
  enough to hold what is in it;
- the container's inner cross size — a **target**, because a single line in a
  container of definite height fills it (`align-content: stretch`, CSS Flexbox
  §9.4).

With only the floor, `stretch` had no room to grow into and `center` had no
room to move in: both were identity operations. The fix is one line of
arithmetic (`line_cross = max(tallest, container_cross)` for a lone line) and
it is what makes the other three branches mean something.

## 2. What was fixed

`src/layout/LayoutBox.cpp`:

- **Row:** `container_cross` from a definite height (minus the vertical border
  and padding under `box-sizing: border-box`); a lone line takes the larger of
  it and its tallest item; `stretch`, `flex-end` and `center` are measured
  against that, and so is the advance to the next line.
- **Column:** the cross axis is the width — `stretch` fills an auto width,
  `center` and `flex-end` offset the item across the container.
- **Grid:** each item gets the height of the area it spans, after the tracks
  settle, and `align-items` is applied in the block axis.

Three recorded pages lost their `known_failure` entries; six new pages assert
what the three did not (including a 3 px border and 5 px padding coming out of
the stretch); five of the six were red on the old binary. Details and the
before/after runs are in `docs/phases/133-cross-axis-alignment.md`.

## 3. What is still wrong, and is now written down

Measuring this one found two neighbouring bugs, in the same file:

- **`justify-content` in a column container.** The column branch stacks items
  from `y = 0` and never asks for the main-axis distribution. A `flex-end`
  column container puts its item at the top. Page:
  `flex_column_justify_content_flex_end`.
- **`align-self` and `align-content`.** The container's `align-items` is the
  only cross-axis input; an item cannot override it, and a container cannot
  share its height between lines. Page: `flex_align_self_overrides_align_items`.

Both are `known_failure` pages and rows of `AGENTS.md`. They were left alone on
purpose: a bug found outside the task is a row, not a detour, and the row is
worth more than the fix if it arrives with a page that fails until it is taken.
