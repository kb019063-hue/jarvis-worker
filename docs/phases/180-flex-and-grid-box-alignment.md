# Phase 180 — `align-self`, `align-content`, `justify-items`/`justify-self`, and column `justify-content`

Task row: **`align-self` and `align-content` are not read, and `justify-content` is ignored in a column container** (finding 139). Finding 184.

Phase 134 made `align-items` work in single-line row flex, column flex, and grid, and left two `known_failure` pages in `tests/render/reftests.json` — the last two remaining in the original render corpus (`flex_column_justify_content_flex_end` and `flex_align_self_overrides_align_items`). Phase 180 finishes CSS Box Alignment Level 3 across flex and grid and clears both entries, bringing the original pixel reftest corpus to **45/45 pages, 179 assertions, 0 known failures**.

## 1. What was missing

1. **No parsed storage for item-level or inline-axis alignment.** `ComputedStyle` had `justify_content`, `align_items`, and `align_content` (with 3 unused padding bytes before `float gap`), but no `align_self`, `justify_items`, or `justify_self`. `StyleResolver.cpp` did not parse `align-self`, `justify-items`, `justify-self`, or the `place-items` / `place-self` / `place-content` shorthands. In addition, `ComputedStyle::hash()` only hashed alignment fields when `display == Display::Flex`, so grid containers and flex/grid items differing only in alignment could hash identically during style interning.
2. **Row flex ignored `align-self` and `align-content`.** Every item consulted `s.align_items` directly; multi-line wrapped containers never distributed free cross space across lines (`align-content: stretch`, `flex-start`, `flex-end`, `center`, `space-between`, `space-around`, `space-evenly`); and `flex-grow` used `s.gap` instead of `col_gap`.
3. **Column flex had no main-axis distribution, no wrapping, and no `align-self`.** `layout_flex`'s column branch stacked items linearly from `y = 0`, ignoring `justify-content`, `flex-grow`, `flex-wrap`, `align-content`, `column-reverse`, and `align-self`.
4. **Grid ignored `align-self`, `justify-items`, `justify-self`, `justify-content`, and `align-content`.** Items only checked `s.align_items` on the block axis and always sat at `x = col_x[cs]` on the inline axis; column and row tracks never distributed leftover container space.

## 2. What changed

- **`include/litebird/css/StyleResolver.hpp`**: added `enum class AlignSelf : uint8_t { Auto, Stretch, FlexStart, FlexEnd, Center }` and three fields (`align_self`, `justify_items`, `justify_self`) in the 3 padding bytes after `align_content` — zero bytes added to `sizeof(ComputedStyle)`.
- **`src/css/StyleResolver.cpp`**:
  - Extended `ComputedStyle::hash()` to include `display == Display::Grid`, `align_self`, `justify_items`, and `justify_self`.
  - Parsed `align-self`, `justify-items`, `justify-self`, `place-items`, `place-self`, `place-content`, `-webkit-*` alignment aliases, `normal`/`left`/`right`/`self-start`/`self-end` keywords, and optional `safe`/`unsafe` overflow keywords.
- **`src/layout/LayoutBox.cpp`**:
  - Added shared helpers `resolve_item_align`, `distribute_justify`, `distribute_align_content`, and `flip_justify_for_reverse`.
  - **Row flex**: resolves per-item `align-self` against `align-items`; distinguishes single-line (`flex-wrap: nowrap`, where `align-content` has no effect and the sole line takes `container_inner_h`) from multi-line (`wrap` / `wrap-reverse`, where `align-content: stretch` distributes free cross space equally across all lines and other `align-content` modes distribute it between/around lines per CSS Flexbox §9.4 & §9.6); supports `row-reverse` and `wrap-reverse`; uses `col_gap` during `flex-grow`.
  - **Column flex**: builds column lines (wrapping on `flex-wrap: wrap` / `wrap-reverse` when `container_inner_h >= 0`), resolves vertical `flex-grow`, applies `align-content` across column lines on the horizontal cross axis, applies `justify-content` on the vertical main axis (with `column-reverse` support), and applies `align-self` overriding `align-items` on the horizontal cross axis.
  - **Grid**: distributes column tracks with `justify-content` and row tracks with `align-content` (`stretch` expands auto-sized rows while leaving fixed-size row tracks unchanged per CSS Grid §11.8); aligns each item on the inline axis with `justify-self` / `justify-items` and on the block axis with `align-self` / `align-items`.

## 3. Finding during verification

Enabling spec-compliant `align-content: stretch` on multi-line flex exposed a pre-existing assumption in `flex_wrap_puts_an_overflowing_item_on_the_next_line` (`tests/render/reftests.json`): the test gave a `flex-wrap: wrap` container `height: 100px` with three `60x20` items and asserted `y = 0, 20, 40` with `#ffffff` at `y = 70`. Under CSS Flexbox §8.4 and §9.4 step 10, `align-content` defaults to `stretch`, which shares the remaining `40px` equally across the three lines (`+13.33px` each -> `y = 0, 33, 67`). That test now declares `align-content: flex-start` so its assertions match CSS Flexbox, and a dedicated new reftest (`flex_align_content_stretch_distributes_free_cross_space`) explicitly asserts default `align-content: stretch`.

## 4. Verification

- `make render-conformance`:
  - `tests/render/reftests.json`: **45/45 pages, 179 assertions, 0 known failures** (was 36/38 with 2 `known_failure` entries; 7 new box-alignment reftests added).
  - `tests/render/body-origin.json`: 34/41 match, 7 recorded.
  - `canvas_to_data_url.py`: 45,016/45,016 checks.
- `./build/test_suite`: **336/336** suites pass, including `flex_and_grid_box_alignment_properties`.
- `make dom-conformance`: **678/678 (100.0%)**.
- `make check-numbers`, `make check-suites`, `python3 tools/ci/check_docs.py`: 0 failures.

## 5. Still open

- `align-items: baseline` / `align-self: baseline` (font-metric baseline alignment across flex/grid items).
- `safe` vs `unsafe` overflow fallback when items overflow a container on `center` / `flex-end`.
- `flex-shrink` on the vertical main axis of column flex containers and `auto` margin absorption along flex main/cross axes.
