# Phase 29 Architecture: Table Spans and Collapsed Borders

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 29 Architecture: Table Spans and Collapsed Borders` section.
> The text below is the section as it stood, unchanged.


Phase 16's table algorithm knew one fact per cell: its column index. A cell
that covered two columns or two rows had no representation, so neither did
`colspan`, `rowspan`, `border-collapse` or `border-spacing`. This phase finishes
the model. It re-lands the implementation written in PR #16 (8 November, on top
of a tree that was subsequently rebuilt through Phases 23–27) on current `main`.

### 1. Spans: placement before sizing

Every cell is placed into an **occupancy grid** before any measurement
happens. Row `r`, column `c` is reserved for a cell with
`colspan`/`rowspan` by marking the slots it covers, so the next cell on the
row skips them. `rowspan="0"` spans to the end of the rows the table has;
`colspan` is clamped to 1000, so a hostile `colspan="99999999"` cannot
allocate a grid. Occupied slots are sized by `std::vector<bool>`, so the
reservation is one byte per cell.

### 2. Column widths with spanning cells

A spanning cell distributes its width requirement over the columns it covers
(requirement minus the width the covered columns already have, spread evenly,
clamped at zero) instead of demanding one wide column. Percentage cell widths
resolve against the *table's* width exactly as before, but a percentage
spanning cell now distributes over its covered columns the same way, and a
cell marks itself with `width_basis_override = <table width>` so percentage
lengths inside it resolve against the table, not against the column it happens
to sit in (CSS 2.1 §17.5.2).

### 3. Row heights

Single-row cells set their row's height first. Then each rowspan cell that
needs more room than the rows it covers already provide grows the *last* row
it covers. The spanning cell is itself grown to fill the covered rows, which
is why a `rowspan="2"` cell with a background paints as one tall box instead of
a box with a gap under it.

### 4. The two border models

- `border-collapse: separate` (the default) keeps per-cell borders and
  honours `border-spacing: <h> <v>`: the cells are inset by `h` from the
  table's left edge and each cell is offset `h` from the previous, and the
  same vertically with `v`. One value means horizontal and vertical are equal.
- `border-collapse: collapse` makes each cell paint **half** of a shared edge
  (4px + 4px renders as one 4px line, not 8px) and forces `border-spacing`
  to zero. The implementation is `LayoutBox::border_scale`, applied when the
  used border is computed from the style — never by mutating the style, so a
  relayout that scaled twice would otherwise erode borders to nothing. Odd
  borders are rounded *up* (`std::ceil`), so a 3px border paints 2px + 2px
  rather than vanishing.

### 5. Two latent bugs this surfaced, and how they were fixed here

1. **Floats and table cells lost their style edges.** `layout_float` and the
   table code called `layout_block`/`layout_table_cell` directly, and only
   `layout()` copied margin/padding/border from the computed style onto the
   box — a float with padding measured as if it had none, a cell with a
   border lost it. Both now go through a shared `LayoutBox::apply_style_edges()`.
   Floats bypass `layout()` because a floated inline keeps
   `BoxType::InlineNode`; routing them through `layout()` would have sent a
   floated `<span style="float:left">` down the inline path. Floats now
   resolve percentages against their containing block too.
2. **Cells laid out through `layout()` directly.** Table cells are
   `BoxType::TableCellNode`, so they now go through the normal dispatch and
   keep their edges via the same path as everything else.

### 6. Verification

```
make test              # 112 engine test suites, all passing (2 new:
                       # table_colspan_and_rowspan_geometry,
                       # table_border_collapse_and_spacing)
make conformance       # html5lib tree-construction: 1781 / 1792 (unchanged;
                       # the parser was not touched)
```

### 7. What is still approximate (unchanged from the PR, restated)

- Collapsed borders are halved rather than resolved per edge by the CSS 2.1
  "widest wins" rule; the table's outer border is not merged with edge cells.
- `empty-cells`, `caption-side` and `table-layout: fixed` are unimplemented.
- Extra height demanded by a rowspan cell lands entirely on the last covered
  row.
- `rowspan="0"` spans to the end of the table, not to the end of the row
  *group* the cell is in.
- A spanned cell's extra width is spread evenly over the covered columns;
  CSS 2.1 does not mandate a distribution algorithm for auto layout.

---
