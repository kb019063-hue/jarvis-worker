# Finding 215 — Table-cell backgrounds and inline formatting had no pixel oracle beyond three replaced-image cases

Phase: [`docs/phases/211-table-and-inline-pixel-oracle.md`](../phases/211-table-and-inline-pixel-oracle.md)

## What was missing

Before Phase 211, `tests/render/reftests.json` held 48 pages: **zero** table or table-cell background pages, and only **three** inline formatting pages (Phase 193's replaced-`<img>` `<br>`, soft-wrap, and `white-space: nowrap` tests). Both `AGENTS.md` and `ROADMAP.md` explicitly listed table-cell backgrounds and the remainder of inline formatting / line breaking as unmeasured.

## What the new oracle uncovered

Adding 15 CSS 2.1 spec-derived, font-independent pixel reftests (9 table pages + 6 inline pages, 65 assertions) confirmed 8 passing behaviors and immediately surfaced **7 concrete rendering defects** that are now pinned as `known_failure` entries (so `render_check.py` will print `FIXED` and fail CI the moment any future change resolves one, forcing the ledger entry to be promoted to a permanent passing regression test):

### Table-background and row-height defects (3)

1. **`tbody` (`display: table-row-group`) backgrounds are not painted** (`table_layers_cell_over_row_over_tbody_over_table`): under CSS 2.1 §17.5.1, `tbody` sits between `table` and `tr`. A transparent cell in an unstyled `tr` over a green `tbody` and red `table` paints red (`#ff0000`) instead of green (`#00ff00`), even though `tr` over `table` and `td` over `tr` paint in the right order.
2. **`<col>` (`display: table-column`) backgrounds are not painted** (`table_colgroup_col_background_shows_through_transparent_cells`): under CSS 2.1 §17.5.1, column backgrounds sit above the table background and below row/cell backgrounds. Transparent cells in a column styled with `background: #00ff00` paint the red table background across `[0..50, 0..80]` instead of green.
3. **Shorter table cells do not stretch their background to the row height** (`table_cell_background_fills_row_height_of_taller_sibling`): under CSS 2.1 §17.5.3, all cells in a row share the row's height. A cell with 20px content beside a 60px sibling stops painting its red background at `y=20`, leaving `[0..50, 20..60]` white.

### Inline formatting and line-breaking defects (4)

4. **`display: inline-block` elements stack vertically like block boxes** (`inline_block_boxes_flow_horizontally_and_wrap`): three 20×20 `inline-block` boxes in a 50px line paint at `(0, 0)`, `(0, 20)`, and `(0, 40)` instead of placing the first two side by side on line 1 (`x=0..20`, `x=20..40`) and wrapping only the third to `(0, 20)`.
5. **`vertical-align: top` and `vertical-align: bottom` are ignored on inline replaced elements** (`inline_vertical_align_top_and_bottom_on_replaced_elements`): inside a 40px line box established by a 40px image, both a `vertical-align: top` 10px image and a `vertical-align: bottom` 10px image paint vertically centered at `y=15..25` instead of `y=0..10` and `y=30..40`.
6. **`white-space: pre` does not break lines on source newlines between inline replaced elements** (`inline_white_space_pre_breaks_on_source_newlines`): two 20×20 images separated by a literal `\n` paint side by side at `x=0..40, y=0..20` instead of breaking onto a second line at `y=20`.
7. **Inline `<span>` horizontal margin and padding do not offset a child replaced element** (`inline_span_horizontal_margin_and_padding_offset_child`): `<span style="margin-left:10px; padding-left:10px">` around a 20×20 image paints the image starting at `x=0` and stretched to `x=0..30` instead of shifting it to `x=20..40`.

## Still unmeasured

These pixel reftests remain intentionally font-independent (`font-size: 0` and solid 1×1 PNGs / CSS boxes). Text-glyph line breaking (word wrapping, hyphenation, bidi inline runs), `border-collapse: collapse` border conflict resolution, `empty-cells: hide`, SVG visual reftests, AVIF/AV1 decoded picture output, audio/video, and WebGL remain separate oracle rounds.
