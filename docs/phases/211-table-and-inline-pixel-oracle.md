# Phase 211 — Table-cell backgrounds and inline formatting in the pixel oracle

Finding: [`docs/findings/215-table-backgrounds-and-inline-formatting-unmeasured.md`](../findings/215-table-backgrounds-and-inline-formatting-unmeasured.md)

## Summary

Extends `tests/render/reftests.json` by **15 pages and 65 assertions** (from 48 pages / 190 assertions to **63 pages / 255 assertions**; **104 pages / 386 assertions** combined with `tests/render/body-origin.json`), measuring two areas listed as unmeasured in `AGENTS.md` and `ROADMAP.md`:

- **Table-cell backgrounds and CSS table painting (9 pages, 39 assertions, CSS 2.1 §17.5.1 / §17.5.3 / §17.6):** 6 pass immediately and 3 record concrete `known_failure` defects.
- **Inline formatting and line breaking (6 pages, 26 assertions, CSS 2.1 §8.3 / §8.4 / §9.4.2 / §10.8 / §16.2 / §16.6):** 2 pass immediately and 4 record concrete `known_failure` defects.

No engine source files are changed in this phase. Every expected rectangle is derived strictly from CSS 2.1 box geometry using solid CSS backgrounds and 1×1 PNG data URIs at `font-size: 0`, with zero host-font metric dependency and zero pixel tolerance.

## New reftests

### Table-cell backgrounds and CSS tables (6 pass, 3 `known_failure`)

| Page | Status | What it measures |
| --- | --- | --- |
| `table_cell_backgrounds_fill_their_border_box` | PASS | 2×2 HTML `<table>` cells paint their 50×40 border boxes cleanly, white outside. |
| `table_border_spacing_exposes_table_background_between_cells` | PASS | Separated-borders `border-spacing: 10px` gutters around and between cells expose the `<table>` background (CSS 2.1 §17.6.1). |
| `table_cell_padding_and_border_paint_around_content` | PASS | Separated cell `border: 5px`, `padding: 10px` (filled with cell background), and 40×20 content under `box-sizing: content-box`. |
| `table_colspan_cell_background_spans_two_columns` | PASS | `colspan="2"` cell background covers both 50px columns (`[0..100, 0..30]`). |
| `table_rowspan_cell_background_spans_two_rows` | PASS | `rowspan="2"` cell background spans continuously across both 30px rows (`[0..40, 0..60]`). |
| `css_display_table_cell_backgrounds_match_html_table` | PASS | Pure CSS `display: table` / `table-row` / `table-cell` on `<div>` elements paint identical cell backgrounds. |
| `table_layers_cell_over_row_over_tbody_over_table` | `KNOWN` | CSS 2.1 §17.5.1 six-layer stack: `tbody` (`display: table-row-group`) background is not painted; transparent cell `(1, 0)` shows the red `<table>` background instead of green `<tbody>` (while `tr` over `table` and `td` over `tr` work). |
| `table_colgroup_col_background_shows_through_transparent_cells` | `KNOWN` | CSS 2.1 §17.5.1 column layer: `<col>` background is not painted; transparent cells in column 0 show the red `<table>` background instead of green `<col>`. |
| `table_cell_background_fills_row_height_of_taller_sibling` | `KNOWN` | CSS 2.1 §17.5.3 row height equalization: a shorter cell (20px content beside a 60px sibling) stops painting red at `y=20`, leaving `[0..50, 20..60]` white instead of stretching to 60px. |

### Inline formatting and line breaking (2 pass, 4 `known_failure`)

| Page | Status | What it measures |
| --- | --- | --- |
| `inline_text_align_center_and_right_position_replaced_elements` | PASS | CSS 2.1 §16.2: `text-align: center` places a 20px inline image at `x=40..60` in a 100px line; `text-align: right` places it at `x=80..100`. |
| `inline_line_height_expands_line_box_and_container_height` | PASS | CSS 2.1 §10.8: two 10px inline images separated by `<br>` in a `line-height: 30px` container establish two 30px line boxes (60px total `#eeeeee` height, `#ffffff` below `y=60`). |
| `inline_block_boxes_flow_horizontally_and_wrap` | `KNOWN` | CSS 2.1 §9.2.4 / §9.4.2: `display: inline-block` elements stack vertically at `x=0` (`y=0..20`, `20..40`, `40..60`) like block boxes instead of flowing horizontally and wrapping at 50px. |
| `inline_vertical_align_top_and_bottom_on_replaced_elements` | `KNOWN` | CSS 2.1 §10.8.1: `vertical-align: top` and `vertical-align: bottom` on 10px inline images inside a 40px line box are ignored — both are vertically centered at `y=15..25` instead of `y=0..10` and `y=30..40`. |
| `inline_white_space_pre_breaks_on_source_newlines` | `KNOWN` | CSS 2.1 §16.6.1: `white-space: pre` does not force a break on a literal `\n` between inline replaced elements — both 20px images paint side by side at `x=0..40, y=0..20`. |
| `inline_span_horizontal_margin_and_padding_offset_child` | `KNOWN` | CSS 2.1 §8.3 / §8.4 / §9.4.2: `margin-left: 10px; padding-left: 10px` on an inline `<span>` does not shift its 20px child `<img>` to `x=20..40` — the image paints at `x=0..30` (starting at 0 and widened by 10px). |

## Verification

- `make render-conformance`:
  - `tests/render/reftests.json`: **55/63 pages match**, 8 fail as recorded (`1` existing `inline_nowrap` + `7` new), `63 of 63 accounted for`.
  - `tests/render/body-origin.json`: **35/41 pages match**, 6 fail as recorded (`41 of 41 accounted for`).
  - `canvas_to_data_url.py`: **45016/45016 checks agree**.
- Negative controls: mutating one expected colour in `table_cell_backgrounds_fill_their_border_box`, shifting `x` by 1px in `inline_text_align_center_and_right_position_replaced_elements`, and removing `known_failure` from all 7 newly recorded failures caused `render_check.py` to exit `1` and report `FAIL` on **9/9** mutated cases.
- `make diff-render BEFORE=build/litebird AFTER=build/litebird DIFF_RENDER_ARGS="--filter table_"` (9 pages, 0 differ) and `--filter inline_` (10 pages, 0 differ) run cleanly.
- `python3 tools/ci/check_numbers.py` and `python3 tools/ci/check_docs.py`: 0 failures.
