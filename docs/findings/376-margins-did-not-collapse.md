# Finding 376 — Vertical Margins Did Not Collapse Across Siblings, Empty Blocks, or Parent Bottoms

**Phase:** [`docs/phases/376-general-vertical-margin-collapse.md`](../phases/376-general-vertical-margin-collapse.md)

## What was wrong

`LayoutBox::layout_block()` placed every in-flow block child by advancing `y_cursor += child->dimensions.margin_box().height`. Outside the narrow nonempty first-child top-margin walk added to `place_document_body()` in Phase 152, no vertical margins collapsed anywhere in the engine:

- Adjacent siblings with `margin-bottom: 20px` and `margin-top: 35px` were separated by `55px` instead of `35px`; negative and mixed-sign sibling margins summed instead of taking `max(positives) + min(negatives)`.
- Empty self-collapsing blocks (`<div></div>`, chains of empty blocks, or `<div><span></span></div>`) did not collapse their own top and bottom margins or collapse through to adjacent margins. In `tests/render/body-origin.json`, `an_empty_first_block_collapses_with_the_following_margin` painted the red block at `y = 58` (`8 + 20 + 30`) where Chromium paints it at `y = 30` (`max(8, 20, 30)`).
- Last-child bottom margins never collapsed with an `auto`-height parent's bottom margin, and parent-top collapse did not run inside nested containers away from `<body>`'s first-child spine.
- Clearance did not interact with collapsing margins or self-collapsing clearing blocks per CSS 2.1 §9.5.2.

## Root cause

Margin collapsing was bolted onto `place_document_body()` as a post-hoc top-edge shift after `layout_block()` had already baked every child's vertical margins into `y_cursor` and ancestor heights. Once margins were accumulated unconditionally inside `layout_block()`, empty boxes, sibling pairs, bottom edges, and clearance could not be recovered without redoing block formatting.

## Fix and proof

Vertical margin collapse is now computed directly inside `LayoutBox::layout_block()` using a two-component `MarginStrut { positive, negative }` carried on `LayoutBox` (`collapsed_margin_top`, `collapsed_margin_bottom`, `margin_self_collapsing`), and `place_document_body()` simply reads `box.collapsed_margin_top.value()`.

- `tests/render/body-origin.json` now passes **41/41** (removing the final `known_failure` entry on `an_empty_first_block_collapses_with_the_following_margin`), matching Chromium's reference pixels across all 131 assertions.
- `tests/suites/376-css-vertical-margin-collapse.cpp` verifies sibling, empty-chain, nested top/bottom, barrier (`height`, `min-height`, `max-height`, `padding`, `border`, BFC, flex/grid), clearance, and bitmap pixel cases alongside all 30 existing Chromium-derived body-origin geometry cases (`442/442` engine suites passing, clean under ASan+UBSan).
