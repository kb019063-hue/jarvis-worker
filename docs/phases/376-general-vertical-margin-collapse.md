# Phase 376 — General Vertical Margin Collapse (CSS 2.1 §8.3.1)

**Finding:** [`docs/findings/376-margins-did-not-collapse.md`](../findings/376-margins-did-not-collapse.md)

## Problem

`LayoutBox::layout_block()` stacked every in-flow block child sequentially with `y_cursor += child->dimensions.margin_box().height`, performing no vertical margin collapse at all. Adjacent siblings summed their bottom and top margins instead of collapsing them, self-collapsing empty blocks pushed following siblings down by every intermediate margin, and parent-bottom / last-child-bottom margins never collapsed.

Phase 152 had added a post-hoc `top_chain` loop inside `place_document_body()` in `src/layout/LayoutTree.cpp` to handle nonempty first-child top-margin collapse under `<body>`, leaving the last `known_failure` in `tests/render/body-origin.json` (`an_empty_first_block_collapses_with_the_following_margin`: Chromium painted at `y = 30`, Litebird at `y = 58`) as well as all sibling, empty-block, bottom, and clearance margin collapse unimplemented.

## Change

1. **`MarginStrut` (`include/litebird/layout/LayoutBox.hpp`)**
   - Added `struct MarginStrut { float positive{0.0f}; float negative{0.0f}; }` with `append(float)`, `append(const MarginStrut&)`, and `value() = positive + negative`. Separating the largest positive and most negative margins makes repeated collapsing across siblings, parents, and empty-block chains associative and exact for mixed-sign margins (CSS 2.1 §8.3.1).
   - Stored `collapsed_margin_top`, `collapsed_margin_bottom`, and three packed flags (`has_collapsed_margins`, `margin_self_collapsing`, `bottom_margin_has_clearance`) on `LayoutBox`, reclaiming alignment padding holes so `sizeof(LayoutBox)` stays under the 400-byte static assertion in `tests/test_suite.cpp`.

2. **General block vertical margin collapsing (`src/layout/LayoutBox.cpp`)**
   - Zeroed `auto` vertical margins on in-flow boxes in `apply_style_edges()` per CSS 2.1 §10.6.3 while preserving out-of-flow `auto` margins for `resolve_out_of_flow_positions()`.
   - Populated default `collapsed_margin_top`/`collapsed_margin_bottom` struts after `switch (box_type)` in `LayoutBox::layout()` so non-block containers (`FlexNode`, `GridNode`, `TableNode`) still participate in outer sibling/parent collapse without exposing inner children.
   - Implemented CSS 2.1 §8.3.1 & §9.5.2 vertical margin collapsing inside `LayoutBox::layout_block()`:
     - **Formatting-context barriers:** blocks internal collapse for out-of-flow, floated, inline-block, non-`visible` overflow, layout/paint containment, flex/grid/table containers, and flex/grid items (`parent->box_type` is `FlexNode` or `GridNode`). `<body>` (`!parent`) is not treated as a barrier unless its own style establishes a BFC.
     - **Adjacent siblings & self-collapsing blocks:** maintains a running `pending_margin` strut across siblings and chains of empty blocks (including blocks with empty inline children producing zero line boxes). On self-collapsing boxes, `collapsed_margin_top` stores the top-only strut (for CSS 2.1 §8.3.1 hypothetical non-zero bottom border placement) and `collapsed_margin_bottom` stores the full self-collapsed strut.
     - **Parent-top / first-child collapse:** collapses leading margins upward into `collapsed_margin_top` whenever `border.top == 0 && padding.top == 0` and no BFC/clearance interrupts it; definite `height` and `min-height` do not block top collapse.
     - **Parent-bottom / last-child collapse:** collapses trailing margins into `collapsed_margin_bottom` only when `border.bottom == 0 && padding.bottom == 0`, `height` is `auto`, `min-height` is zero, `max-height` does not clamp, and there is no trailing clearance; auto height follows CSS 2.1 §10.6.3 so `min-height` does not inflate content height by the child's bottom margin.
     - **Floats & clearance (CSS 2.1 §9.5.1–9.5.2):** places floats at the hypothetical collapsed margin position and computes clearance against the hypothetical top border edge; for self-collapsing clearing boxes, places clearance above the clearing element's top margin and prevents its bottom margin from collapsing with its parent's bottom margin.

3. **Simplified `place_document_body()` (`src/layout/LayoutTree.cpp`)**
   - Removed the Phase 152 nonempty-first-child `top_chain` post-hoc patch and read `box.collapsed_margin_top.value()` directly, keeping the `<html>` root margin offset and positioned/fixed handling intact.

4. **Tests**
   - Removed `known_failure` from `an_empty_first_block_collapses_with_the_following_margin` in `tests/render/body-origin.json`, bringing the 41-page Chromium-verified body-origin corpus to **41/41 passing (0 known failures)**.
   - Added standalone suite `tests/suites/376-css-vertical-margin-collapse.cpp` covering positive/negative/mixed sibling collapse, empty-block chains and border-top placement, nested empty blocks and empty IFC blocks, parent-top and parent-bottom barriers (`height`, `min-height`, `max-height`, `padding`, `border`, `overflow: hidden`), floats and clearance (non-empty and self-collapsing), flex/grid item boundaries, and end-to-end rendered pixel checks.

## Verification

- `make render-conformance`:
  - `tests/render/reftests.json`: **65/66** pass, 1 known (`a_data_uri_avif_image_scales_to_its_box`).
  - `tests/render/body-origin.json`: **41/41** pass, **0 known** (up from 40/41 + 1 known).
  - Canvas `toDataURL`: **45016/45016** checks agree.
- `make test`: **442/442** engine test suites pass (including `css_vertical_margin_collapse_general` and all 30 Chromium-derived cases in `document_body_margins_place_boxes_from_the_viewport`); **114/114** cross-origin checks pass.
- `make dom-conformance`: **678/678** (100.0%).
- `make wpt`: **9570/9824** subtests pass (0 unlisted failures).
- ASan + UBSan (`build-asan/litebird`): `reftests.json` **65/66** + 1 known, `body-origin.json` **41/41**, zero sanitizer errors.
