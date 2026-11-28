# Finding 498 — Collapsed table border conflicts lacked a pixel oracle

Phase 498. Status: partially measured; full collapsed-border behavior remains open.

## What was wrong

`tests/render/reftests.json` exercised table backgrounds and separated-border
spacing, but no case asserted the rendered shared edge of adjacent cells in a
`border-collapse: collapse` table. A regression in the width-conflict
resolution or in carrying the winner's color into paint could pass the pixel
suite unnoticed.

## What changed

Phase 498 adds `collapsed_table_wider_cell_border_wins_shared_edge`: a left
cell's 6px blue border competes with the right cell's 2px yellow border. CSS
2.1 §17.6.2.1 says the wider border wins. The test asserts the full 6px blue
strip and the neighboring cell/background boundaries. It adds coverage only;
no engine implementation was changed.

## How it was verified

- `python3 tools/conformance/render_check.py --binary build/litebird --filter collapsed_table_wider_cell_border_wins_shared_edge --verbose` — **1/1 page matches**.
- `make render-conformance` — **67/68** main reftests match, with the existing AVIF limitation recorded as the sole known failure; body-origin **41/41** and canvas `toDataURL` **45016/45016**.

## Still open

This one case covers the wider of two solid cell borders only. Equal-width and
border-style tie-breaks, precedence from row/column/table borders, spans and
other collapsed-edge cases need separate measurements. The broader pixel
oracle gaps listed in `AGENTS.md` remain open as well.
