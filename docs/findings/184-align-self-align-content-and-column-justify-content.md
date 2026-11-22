# Finding 184 — `align-content: stretch` exposed a pre-alignment wrap reftest, and `ComputedStyle::hash()` skipped grid and item alignment

**Phase 180** (`docs/phases/180-flex-and-grid-box-alignment.md`).

## What happened

Implementing the remaining CSS Box Alignment Level 3 properties left open by finding 139 (`align-self`, `align-content`, `justify-items`, `justify-self`, `place-*`, and column-flex `justify-content`) surfaced two concrete findings beyond the missing layout branches:

1. **A pre-`align-content` reftest encoded the old omission.** `tests/render/reftests.json`'s `flex_wrap_puts_an_overflowing_item_on_the_next_line` laid out three `60x20` items in a `display: flex; flex-wrap: wrap; width: 100px; height: 100px` container and asserted green at `(10, 30)`, blue at `(10, 50)`, and white at `(10, 70)`. That matches a container that ignores `align-content`, not CSS Flexbox Level 1 §8.4 / §9.4 step 10: the initial value of `align-content` is `stretch`, so a definite `height: 100px` shares the `40px` of free cross space across the three lines (`+13.33px` each), placing the three items at `y = 0`, `y = 33.33`, and `y = 66.67` (putting white at `y = 30`, green at `y = 50`, and blue at `y = 70`). Adding `align-content: flex-start` to that wrap test restores spec fidelity while preserving all four assertions, and `flex_align_content_stretch_distributes_free_cross_space` now explicitly verifies default `align-content: stretch`.
2. **`ComputedStyle::hash()` guarded alignment fields on `display == Display::Flex`.** Because `align-self` and `justify-self` live on flex/grid *items* (usually `display: block`) and `align-items`/`justify-items`/`align-content`/`justify-content` also live on `display: grid` containers, style interning could hash two distinct alignment styles identically unless the guard included `display == Display::Grid` and non-default `align_self`/`justify_items`/`justify_self`.
3. **Zero `ComputedStyle` size growth.** Five `uint8_t` flex/alignment enums preceded `float gap`, leaving three padding bytes — exact room for `align_self`, `justify_items`, and `justify_self`.

## Result

Both remaining `known_failure` entries in `tests/render/reftests.json` (`flex_column_justify_content_flex_end` and `flex_align_self_overrides_align_items`) printed `FIXED` on the first run and were removed. Seven new pixel reftests and unit suite `flex_and_grid_box_alignment_properties` verify row flex, column flex (all 6 `justify-content` modes, `flex-grow`, `align-self`, wrapping, `column-reverse`), and grid (`align-self`, `justify-items`, `justify-self`, `justify-content`, `align-content`, `place-items`/`place-self`/`place-content`). `tests/render/reftests.json` now stands at **45/45 pages, 179 assertions, 0 known failures**.
