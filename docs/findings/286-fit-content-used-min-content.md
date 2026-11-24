# Finding 286 — `fit-content` used min-content

## What was wrong

`LayoutBox::layout_block()` and `layout_inline_formatting_context()` treated the
min-content width (the widest unbreakable run) as the natural width for
`width: fit-content`, then capped that by available space. This is not the
specified fit-content formula, `min(max-content, max(min-content, available))`.
A dialog with room for its complete text was consequently sized to its longest
word and wrapped the rest. Auto-width inline-blocks and floats used the same
min-only shortcut for CSS 2.1 shrink-to-fit.

The Phase 281 regression fails against the old implementation because the
open dialog's `Favorite animal: choose a favorite.` label wraps despite the
800px viewport.

## What changed

`LayoutBox::max_content_width()` measures unwrapped text with the active font
renderer and walks the existing block/inline tree, including preserved and
collapsed whitespace, hard line breaks, inline edges, images and inline-blocks.
The two fit-content layout paths and float shrink-to-fit now use the specified
min/max/available clamp. Min/max width constraints continue to apply after the
keyword sizing.

The change does not implement the native `<select>`'s intrinsic width or
painting. The engine's min-content text estimate remains approximate, and
specialized flex/grid/table intrinsic contribution rules remain open.

## How it was verified

- The new test `fit_content_uses_max_content_before_clamping` first failed on
  the old code at its one-line dialog assertion. After the change,
  `make -j2 test` reports **419/419** engine suites passed and **114**
  cross-origin checks with **0 failures**. Its measurements are 277.3px for
  the wide dialog, 150.0px when 150px is available, 224.1px for both the
  inline-block and float, and a 52.8px min-content floor when only 20px is
  available.
- `make render-conformance` reports **65/66** reftests, **40/41** body-origin
  pages and **45,016/45,016** canvas serialization checks; only the existing
  AVIF and margin-collapse cases remain known failures.
- The pinned holdout-01 H3 interaction still passes. Following its README's
  capture/run instructions, compare the open-frame screenshot with the saved
  Chromium reference:

  ```sh
  compare -metric AE \
    tools/real_pages/holdout-01/results-rerun06/oracle-frames/H3-ref-open.png \
    "$R/results/H3-lite-open.png" null:
  ```

  ImageMagick reports **24,300** differing pixels (the archived
  `results-rerun06` review recorded **36,424**). The current Litebird dialog's border box is 166.6×94px,
  versus Chromium's 293.7×111px; the missing select control still accounts for
  part of that gap. `make bench-floats` measures `rows-10000` at **0.119s**.

## What remains open

The dialog cannot yet size or paint its native `<select>`, expose a
`::backdrop` style, or clip overflow without clipping its own chrome. The
min-content lower bound remains approximate for font metrics and source-byte
runs; full intrinsic sizing for specialized formatting contexts and Unicode
line breaking are not claimed here.
