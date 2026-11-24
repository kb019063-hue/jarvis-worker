# Phase 264 — CSS table columns, `white-space: pre` newlines, `visibility`, inline background layers, opacity groups, and `linear-gradient` backgrounds

Finding: [`docs/findings/268-table-columns-pre-newlines-visibility-opacity-and-gradients.md`](../findings/268-table-columns-pre-newlines-visibility-opacity-and-gradients.md)

## Scope

Clears all **6** remaining CSS/layout/paint `known_failure` reftests across `tests/render/reftests.json` (2 of 2 CSS/layout items — leaving only AVIF picture decode and WebGL) and `tests/render/body-origin.json` (4 of 5 items — leaving only empty-block/sibling margin collapse):

1. **Table `<colgroup>` / `<col>` backgrounds (CSS 2.1 §17.2, §17.5.1):**
   - Added `Display::TableColumn` and `Display::TableColumnGroup` (`include/litebird/css/StyleResolver.hpp`, `src/css/StyleResolver.cpp`), replacing `col, colgroup { display: none; }` in the UA stylesheet with `colgroup { display: table-column-group; }` and `col { display: table-column; }`.
   - Preserved `<colgroup>` and `<col>` in `normalize_table_children` (`src/layout/LayoutTree.cpp`), resolved HTML `span` attributes, explicit `<col>` width hints, and column strip geometry in `LayoutBox::layout_table` (`src/layout/FloatTableLayout.cpp`), and painted column-group (layer 2) and column (layer 3) backgrounds ahead of row groups, rows, and cells in `paint_block_backgrounds` (`src/render/PaintCommand.cpp`) regardless of DOM order.
2. **`white-space: pre` / `pre-wrap` / `pre-line` / `break-spaces` source newlines (CSS 2.1 §16.6.1, CSS Text 3 §4.1.1):**
   - Updated `LayoutTree::build_boxes`, `normalize_block_children`, and `split_inline_box_around_blocks` (`src/layout/LayoutTree.cpp`) to preserve source newlines (and spaces for `pre`/`pre-wrap`/`break-spaces`) instead of discarding whitespace-only text nodes or collapsing them before they reach `LayoutBox::collect_items`.
3. **`visibility: visible | hidden | collapse` (CSS 2.1 §11.2):**
   - Added inherited `Visibility` to `ComputedStyle` (`include/litebird/css/StyleResolver.hpp`, `src/css/StyleResolver.cpp`).
   - Updated `emit_chrome` and `emit_contents` (`src/render/PaintCommand.cpp`) to suppress painting for non-visible boxes and fragments while continuing to descend so `visibility: visible` descendants still paint, layout geometry is preserved, and propagated canvas backgrounds remain painted.
4. **Multi-layer inline ancestor backgrounds (CSS 2.1 §14.2):**
   - Added `InlineFragment::ancestor_backgrounds` (`include/litebird/layout/LayoutBox.hpp`, `src/layout/LayoutBox.cpp`, `src/render/PaintCommand.cpp`) so translucent descendant inline boxes composite over enclosing inline ancestor backgrounds (outermost to innermost) instead of losing intermediate layers, and opaque inner inline backgrounds are never overwritten by outer inline ancestors.
5. **Isolated opacity compositing groups (CSS Color 4 §3.2):**
   - Added `PushOpacityCommand` and `PopOpacityCommand` (`include/litebird/render/PaintCommand.hpp`, `src/render/PaintCommand.cpp`), wrapping both `<html>` root document paint (including the propagated canvas) and elements with `opacity < 1.0` (which also establish stacking contexts).
   - Implemented offscreen straight-alpha group compositing with zero-opacity subtree skipping in `BitmapRenderer` (`include/litebird/render/BitmapRenderer.hpp`, `src/render/BitmapRenderer.cpp`) and zero-opacity subtree skipping in `TerminalRenderer` (`include/litebird/render/TerminalRenderer.hpp`, `src/render/TerminalRenderer.cpp`).
6. **`background-image: linear-gradient(...)` & root canvas propagation (CSS Backgrounds 3 §2.11.2, CSS Images 3 §3.4):**
   - Added `GradientStop`, `LinearGradient`, `has_background_image`, and `background_gradient` to `ComputedStyle`, parsed `linear-gradient(...)` (angles `deg`/`rad`/`grad`/`turn`, `to <side-or-corner>`, color stops with CSS Images 3 §3.4.3 fixup) in both `background-image` and the `background` shorthand (`src/css/StyleResolver.cpp`).
   - Updated `LayoutTree::build` so a non-`none` root `background-image` supplies the canvas and blocks body propagation, and added linear-gradient rasterization in `BitmapRenderer` and `TerminalRenderer`.

## Verification

- `make render-conformance`:
  - `tests/render/reftests.json`: **64/66** pass, **2** known (`a_data_uri_avif_image_scales_to_its_box`, `webgl_clear_paints_canvas_green`) — 0 CSS/layout/paint known failures remaining.
  - `tests/render/body-origin.json`: **40/41** pass, **1** known (`an_empty_first_block_collapses_with_the_following_margin`).
- `make test`: all engine unit suites pass including `tests/suites/264-css-table-columns-pre-visibility-opacity-and-gradients.cpp`.
- `make verify-terminal-clipping`, `check_numbers.py`, `check_docs.py`, `check_suites.py`, `check_ci_parts.py`, `check_rows.py`: all 0 failures.
