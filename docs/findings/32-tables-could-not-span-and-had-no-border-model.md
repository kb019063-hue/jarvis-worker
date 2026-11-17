# 32. ✅ Tables could not span, and had no border model

> Moved out of `AUDIT.md` — written there as a `## 32. ✅ Tables could not span, and had no border model` section.
> The text below is the section as it stood, unchanged.


**The hole.** Phase 16's table algorithm placed one cell per column of the
row it stood in. `colspan` and `rowspan` were parsed into the DOM and read
by nothing, so a header cell meant to cover two columns covered the first
and left the second empty; `rowspan="0"` was an ordinary rowspan of one; a
spanning cell demanded a column wide enough for itself instead of spreading
its requirement; and `border-collapse` / `border-spacing` were unknown
properties, so every cell painted its own full border and adjacent borders
doubled. Two latent faults surfaced with it: table cells and floats were
laid out by calling `layout_block()` directly, and only `layout()` copies
margin/padding/border from the computed style, so a cell with a border lost
it and a float with padding measured as if it had none; and a percentage
width inside a cell resolved against the column it happened to receive
(25% of 75px is 18.75px) instead of the table it belonged to.

**The fix.** Cells are placed into an occupancy grid before sizing, so a
spanning cell reserves its slots and the following cells skip them. A
spanning cell distributes its width requirement over the columns it covers.
Row heights come from single-row cells first; a rowspan cell that needs
more room grows the last row it covers and is itself stretched to fill it.
`border-collapse: collapse` makes each cell paint half of a shared border
(never by mutating the style, so relayout cannot halve twice) and forces
`border-spacing` to zero; `border-spacing` insets and separates cells.
Percentage lengths inside a cell resolve against the table width
(`width_basis_override`). The edge copy lives in `LayoutBox::apply_style_edges()`,
shared by `layout()` and the float path. The full account is in
ARCHITECTURE.md, Phase 29.

**What it still does wrong, on purpose and documented.** Collapsed borders
are halved rather than resolved per edge by CSS 2.1's "widest wins" rule; the
table's outer border is not merged with edge cells; `empty-cells`,
`caption-side` and `table-layout: fixed` are unimplemented; a rowspan's
extra height lands on the last covered row; `rowspan="0"` spans to the end
of the table rather than the row group.

**How verified.** `make test` prints `All 112 engine test suites passed
successfully!` — two of them new (`table_colspan_and_rowspan_geometry`,
`table_border_collapse_and_spacing`). `make conformance` is unchanged at
1781 of 1792: the parser was not touched. The work landed originally as PR
#16 (8 November) on a tree that was subsequently rebuilt through Phases
23–27, and was re-landed by hand for that reason.

---
