# Phase 498 — A collapsed table border is measured

Finding: [`docs/findings/498-collapsed-table-border-was-unmeasured.md`](../findings/498-collapsed-table-border-was-unmeasured.md)

## Summary

Added `collapsed_table_wider_cell_border_wins_shared_edge` to
`tests/render/reftests.json`. Two adjacent collapsed-border table cells present
a 6px blue border and a 2px yellow border on the same edge. CSS 2.1
§17.6.2.1 requires the wider border to win; the reftest checks the exact
6px blue shared edge, both cell backgrounds, and the unpainted pixels beyond
the cells. The case uses only solid colors and fixed box dimensions, so it
needs no font metrics or pixel tolerance.

No engine source changed. This is a visual measurement of the existing
cell-to-cell width-conflict path, not an implementation of the full collapsed
border algorithm.

## What was wrong

The render corpus covered table-cell backgrounds and separated-border spacing,
but did not assert the pixel result of `border-collapse: collapse` when
adjacent cell borders conflict. The table layout already resolves a shared
cell edge by width and carries the winning color to the painter, but that
behavior had no pixel-oracle coverage.

## What changed

The new page gives the left cell a 6px blue right border and the right cell a
2px yellow left border. It asserts that the first cell ends at x=30, the
winning blue strip occupies x=30..36, the second cell starts at x=36, and the
pixels to the right and below the row are white. If the narrower border won,
or if the winner's color or width were lost, the exact rectangle assertion
would fail.

## Verification

- `python3 tools/conformance/render_check.py --binary build/litebird --filter collapsed_table_wider_cell_border_wins_shared_edge --verbose` — **1/1 page matches**.
- `make render-conformance` — **67/68** `tests/render/reftests.json` pages match; the sole known failure remains the recorded AVIF picture-decoder limitation. `tests/render/body-origin.json`: **41/41**; canvas `toDataURL`: **45016/45016**.

## Still open

This measures only one solid-border width conflict between adjacent cells. Equal-width conflicts, border-style and cell/row/column/table precedence, and other collapsed-table edge cases remain unmeasured or unsupported. The pixel corpus also still lacks glyph-shape comparisons, broader bidi and line-break coverage, and comprehensive SVG/canvas visual comparisons.
