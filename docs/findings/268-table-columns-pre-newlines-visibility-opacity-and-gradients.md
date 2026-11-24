# Finding 268 — Six CSS layout and paint defects cleared across both render pixel oracles

Phase: [`docs/phases/264-css-table-columns-pre-visibility-opacity-and-gradients.md`](../phases/264-css-table-columns-pre-visibility-opacity-and-gradients.md)

## What was missing before Phase 264

Six Chromium-verified pixel assertions remained marked `known_failure` across the two render corpora (`tests/render/reftests.json` and `tests/render/body-origin.json`):

1. `col, colgroup { display: none; }` in the UA stylesheet discarded all `<col>` and `<colgroup>` boxes, so transparent table cells never exposed column backgrounds (`table_colgroup_col_background_shows_through_transparent_cells`).
2. `LayoutTree::build_boxes` dropped whitespace-only text nodes under block parents and collapsed whitespace unconditionally, stripping literal newlines before `LayoutBox::collect_items` could honor `white-space: pre` (`inline_white_space_pre_breaks_on_source_newlines`).
3. `visibility` was neither stored on `ComputedStyle` nor consulted by the painter (`hidden_body_children_do_not_paint_but_canvas_does`).
4. Inline formatting flattened each fragment to a single background color, dropping an enclosing inline ancestor's background beneath a translucent child (`inline_body_under_a_translucent_inline_child`) and even letting an outer `<span>` overwrite an inner opaque `<span>`'s background.
5. `ComputedStyle::opacity` was parsed and hashed but never emitted into the display list or composited (`root_opacity_applies_to_the_document_paint`).
6. `background-image` was neither represented on `ComputedStyle` nor painted, so a root `linear-gradient(...)` failed to paint the canvas and allowed body background propagation (`root_background_image_blocks_body_propagation`).

## What works now

- `tests/render/reftests.json` advances from **62/66 (4 known)** to **64/66 (2 known — AVIF picture decode and WebGL only)**, clearing 100% of CSS/layout/paint known failures in that corpus.
- `tests/render/body-origin.json` advances from **36/41 (5 known)** to **40/41 (1 known — empty-block/sibling margin collapse only)**.
- `tests/suites/264-css-table-columns-pre-visibility-opacity-and-gradients.cpp` verifies `<colgroup>`/`<col>` layer precedence, `span`, DOM-order independence, `white-space: pre`/`pre-line`/`normal`, `visibility: hidden` with `visibility: visible` descendants, nested inline background compositing, offscreen opacity group isolation (no red bleed-through under an overlapping opaque blue child), and `linear-gradient(...)` angles/color-stops/root-canvas propagation.
