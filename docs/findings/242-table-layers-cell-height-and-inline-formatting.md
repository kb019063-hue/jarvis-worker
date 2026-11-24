# Finding 242 — Six CSS 2.1 table and inline formatting failures lived in four small gaps outside `LayoutTree.cpp`

Phase: [`docs/phases/238-css-tables-and-inline-formatting.md`](../phases/238-css-tables-and-inline-formatting.md)

## What failed before Phase 238

Phase 193 and Phase 211 recorded seven CSS 2.1 table and inline pixel failures in `tests/render/reftests.json`. Inspecting the layout and paint pipeline exposed six distinct, self-contained defects outside `src/layout/LayoutTree.cpp`:

1. **`<tbody>` row-group boxes were never given dimensions (`src/layout/FloatTableLayout.cpp`):** `layout_table()` flattened row groups into `grid` and assigned dimensions to every `<tr>`, but left every `<tbody>`/`<thead>`/`<tfoot>` `LayoutBox` at `0×0`, so `emit_chrome` painted nothing for row groups.
2. **`rowspan="1"` table cells never stretched to their row height (`src/layout/FloatTableLayout.cpp:645`):** the height-expansion block was guarded by `if (entry.rowspan > 1)`, leaving shorter sibling cells with unstretched backgrounds.
3. **`white-space` and `vertical-align` were not parsed (`include/litebird/css/StyleResolver.hpp`, `src/css/StyleResolver.cpp`):** neither property existed in `ComputedStyle`, so `white-space: nowrap` still soft-wrapped replaced elements and `vertical-align: top`/`bottom` fell through to hard-coded line centering in `LayoutBox::layout_inline_formatting_context`.
4. **`display: inline-block` was excluded from IFCs (`src/layout/LayoutBox.cpp:515`, `src/render/PaintCommand.cpp`):** `establishes_ifc()` rejected every `BoxType::BlockNode` child even when `style->is_inline_block()` was true, forcing horizontal `inline-block` runs onto the vertical block path.
5. **Inline horizontal `margin`/`padding` stretched the first child (`src/layout/LayoutBox.cpp:1720`):** `collect_items()` ignored inline `margin` and added `padding.left + border.left` onto `items[initial_count].width`, widening a child `<img>` from `20px` to `30px` starting at `x=0` instead of offsetting it to `x=20..40`.

## What passes now

- **`make render-conformance`:** `tests/render/reftests.json` moves from **56/66 pass, 10 known** to **62/66 pass, 4 known**, and `tests/render/body-origin.json` moves from **35/41 pass, 6 known** to **36/41 pass, 5 known** (`inline_body_honors_its_default_horizontal_margin` cleared once inline horizontal margins advance the cursor and word-separating spaces paint their enclosing inline box's background continuously). All 7 cleared pages tripped `FIXED` before promotion, with 0 regressions across all 107 pages / 395 assertions.
- **`make test`:** `tests/suites/238-css-tables-and-inline-formatting.cpp` verifies `<thead>`/`<tbody>` geometry & CSS 2.1 §17.5.1 paint layering, `rowspan="1"` cell height stretching, `white-space` inheritance vs non-inherited `vertical-align`, `inline-block` horizontal flow/wrap/nested child shifting, and inline `margin`/`padding` cursor offset.

## Still open

- **`table_colgroup_col_background_shows_through_transparent_cells`** (`<col>` / `<colgroup>` are `display: none` in the UA sheet and skipped by `LayoutTree.cpp`) and **`inline_white_space_pre_breaks_on_source_newlines`** (`LayoutTree::build_boxes` collapses/drops whitespace-only text nodes including `\n` before `LayoutBox::collect_items` sees them): both require edits to `src/layout/LayoutTree.cpp` (claimed by PR #297) and stay recorded as `known_failure` in `tests/render/reftests.json`.
- **Inline baseline metrics:** `vertical-align: baseline`/`sub`/`super` still share the existing line-box centering fallback; only `top`/`text-top` and `bottom`/`text-bottom` are aligned to the line-box edges.
- **Mixed block + `inline-block` siblings and `inline-block` inside `<span>`:** `is_block_level_box()` in `src/layout/LayoutTree.cpp:56` still classifies `BoxType::BlockNode` with `is_inline_block()` as block-level, so `normalize_block_children` wraps adjacent inline siblings and `split_inline_box_around_blocks` lifts an `inline-block` out of an enclosing `<span>`; fixing `is_block_level_box` belongs in the follow-up `LayoutTree.cpp` pass once PR #297 lands.
