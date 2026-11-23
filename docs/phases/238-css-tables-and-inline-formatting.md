# Phase 238 — CSS 2.1 Table Layers, Row-Height Stretching, and Inline Formatting

**Date:** 2026-11-23  
**Finding:** [`docs/findings/242-table-layers-cell-height-and-inline-formatting.md`](../findings/242-table-layers-cell-height-and-inline-formatting.md)  
**Oracles:** `make render-conformance` (`tests/render/reftests.json`: **62/66 pass, 4 known** — up from **56/66, 10 known**; `tests/render/body-origin.json`: **36/41 pass, 5 known** — up from **35/41, 6 known**), `make test` (`tests/suites/238-css-tables-and-inline-formatting.cpp`)

## Goal

Clear six `known_failure` CSS 2.1 pixel reftests in `tests/render/reftests.json` recorded in Phases 193 and 211 across table layout/painting and inline formatting — plus the seventh `known_failure` reftest in `tests/render/body-origin.json` that inline horizontal margins clear directly — without touching `src/layout/LayoutTree.cpp`:

1. `table_layers_cell_over_row_over_tbody_over_table`
2. `table_cell_background_fills_row_height_of_taller_sibling`
3. `inline_nowrap_keeps_replaced_elements_on_one_line`
4. `inline_block_boxes_flow_horizontally_and_wrap`
5. `inline_vertical_align_top_and_bottom_on_replaced_elements`
6. `inline_span_horizontal_margin_and_padding_offset_child`
7. `inline_body_honors_its_default_horizontal_margin` (`tests/render/body-origin.json`)

## Root causes and fixes

### 1. Table row-group boxes and cell row-height stretching (`src/layout/FloatTableLayout.cpp`)

- **Row-group boxes (`<thead>`, `<tbody>`, `<tfoot>`):** `LayoutBox::layout_table` flattened row-group children into `grid` and sized every `<tr>` in `row_boxes`, but never assigned `dimensions` to the row-group `LayoutBox` itself — leaving every `<tbody>` at `0×0` so its background never painted. `layout_table` now records `[box, first_row, last_row]` for each row group and sizes its content box across its spanned rows so Appendix E step 3 paints `table` → row-group → `tr` → `td` in CSS 2.1 §17.5.1 order.
- **Cell row-height stretching:** `layout_table` step 7 guarded cell height expansion behind `if (entry.rowspan > 1)`, leaving `rowspan="1"` cells shorter than a taller sibling in the same row. Removing that guard stretches every cell (`rowspan >= 1`) to cover the full height of its row (or spanned rows) per CSS 2.1 §17.5.3.

### 2. `white-space` and `vertical-align` in `ComputedStyle` (`include/litebird/css/StyleResolver.hpp`, `src/css/StyleResolver.cpp`)

- Added `WhiteSpace` (`normal`, `nowrap`, `pre`, `pre-wrap`, `pre-line`, `break-spaces`) and `VerticalAlign` (`baseline`, `top`, `bottom`, `middle`, `text-top`, `text-bottom`, `sub`, `super`) enums and fields packed into the two existing padding bytes after `text_decoration` in `ComputedStyle` (zero struct size growth).
- Included both fields in `ComputedStyle::hash()` (equality is compiler-defaulted).
- Inherited `white_space` in `StyleResolver::resolve_style` and `ComputedStylePool::inherited_only` (CSS 2.1 §16.6); kept `vertical_align` non-inherited (CSS 2.1 §10.8.1); handled `initial`, `inherit`, and `unset`.

### 3. Inline formatting context & atomic `inline-block` painting (`include/litebird/layout/LayoutBox.hpp`, `src/layout/LayoutBox.cpp`, `src/render/PaintCommand.cpp`)

- **`display: inline-block`:** `LayoutBox::establishes_ifc` treated `BoxType::BlockNode` children as block-level even when `style->is_inline_block()` was true, forcing containers of `inline-block` elements onto the vertical block layout path. `establishes_ifc` now keeps `inline-block` children inside the IFC; `collect_items` lays each `inline-block` out against `available_width` (with shrink-to-fit auto width and zero auto horizontal margins per CSS 2.1 §10.3.9) and emits an inline item sized to its `margin_box()`; step 4 shifts the placed `inline-block` (and its subtree) via `shift_position`; and `PaintCommand.cpp` (`emit_contents`) paints it atomically via `paint_atom(frag.box, list, top)` per CSS 2.1 Appendix E step 7.2.
- **`white-space: nowrap` & `pre`:** `layout_inline_formatting_context` now suppresses soft line wrapping when the item's `white_space` is `Nowrap` or `Pre`, and `collect_items` respects preserved spaces and explicit newlines (`preserve_space` on `InlineFragment` prevents stripping preserved spaces at line boundaries).
- **`vertical-align`:** `layout_inline_formatting_context` step 4 now honors `VerticalAlign::Top`/`TextTop` (align to line top) and `VerticalAlign::Bottom`/`TextBottom` (align to line bottom) instead of unconditionally vertically centering every fragment shorter than the line box.
- **Inline horizontal `margin`/`padding`/`border` & continuous inline backgrounds:** `collect_items` previously ignored inline `margin` and added `padding.left + border.left` onto `items[initial_count].width`, stretching the first child `<img>` instead of offsetting it. It now emits non-collapsible horizontal advance spacers (`preserve_space = true`, empty `text`) before and after the inline box's children (painting only padding/border when the inline box has a background/border and never painting margin). In `PaintCommand.cpp`, word-separating text spaces (`frag.is_space && !frag.text.empty()`) inside an inline box with a non-transparent background now paint that background continuously across words instead of leaving a transparent gap between words, while empty margin spacers still paint nothing.

## Verification

- `make render-conformance`: all 7 cleared pages printed **`FIXED`** before removing `known_failure`; after promotion: **`62/66` pass, `4` known** in `tests/render/reftests.json` (only `<col>`, `white-space: pre` newline preservation in `LayoutTree.cpp`, AVIF, and WebGL remain known), **`36/41` pass, `5` known** in `tests/render/body-origin.json` (clearing `inline_body_honors_its_default_horizontal_margin`), and **`45016/45016`** `Canvas toDataURL` checks.
- `make test`: **376/376** engine test suites pass (including new `tests/suites/238-css-tables-and-inline-formatting.cpp`) + **114/114** cross-origin checks.
- `make verify-media-webgl-codecs`, `check_numbers.py`, `check_docs.py`, `check_suites.py`, `check_ci_parts.py`: 0 failures.

## Still open

- `<col>` / `<colgroup>` column backgrounds (`table_colgroup_col_background_shows_through_transparent_cells`) and preserving literal `\n` text nodes under `white-space: pre` (`inline_white_space_pre_breaks_on_source_newlines`): both require `src/layout/LayoutTree.cpp` (currently claimed by PR #297) and remain tracked in `tests/render/reftests.json`.
