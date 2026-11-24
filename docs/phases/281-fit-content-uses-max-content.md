# Phase 281 — `fit-content` uses max-content

**Finding:** [286. `fit-content` used min-content as its natural width](../findings/286-fit-content-used-min-content.md)
**Row:** `AGENTS.md`, the open `<dialog>` intrinsic-width item
**Claim:** [#347](https://github.com/kb019063-hue/jarvis-worker/pull/347), Phase 281, finding 286
**Oracle:** `tests/suites/281-fit-content-uses-max-content.cpp`; holdout-01 H3's open frame

## What was wrong

CSS Sizing Level 3 defines the `fit-content` size as
`min(max-content, max(min-content, available))`. The two layout entry points
instead called `min_content_width()` the content's natural width and computed
`min(natural, available)`. A dialog with ordinary multiword text therefore
wrapped at its widest word even when its containing block had ample space.

The same min-content-only shortcut was used for CSS 2.1 shrink-to-fit on
`inline-block`s and floats. The new suite failed on the unmodified tree at
`dialog_text->line_boxes.size() == 1`: its open dialog wrapped a sentence whose
max-content width fit comfortably in the viewport.

## What changed

`LayoutBox::max_content_width()` now measures text runs with the active
`FontRenderer` (family, weight, italic style and kerning), accounts for the
engine's collapsed/preserved whitespace and explicit line breaks, and walks
inline edges, atomic inline-blocks/images and block children. The block and
inline-formatting-context paths now clamp between the min-content floor,
max-content ceiling and available width. Float and auto-width inline-block
shrink-to-fit use the same bounds.

The helper is scoped to the ordinary block/inline formatting model already
laid out here. It does not supply the missing intrinsic size or paint box for
`<select>`, or a complete intrinsic-sizing algorithm for flex, grid and table
formatting contexts. The existing `min_content_width()` text metric remains an
approximation (`0.55em` per source byte), so the lower bound can still be
imprecise for fonts and non-ASCII text.

## Verification

- Before the fix, `make -j2 test` stopped in the new regression at
  `dialog_text->line_boxes.size() == 1u`. Afterward it reports **419/419 engine
  suites passed**, plus **114 cross-origin checks, 0 failures**. The new suite
  prints: dialog content width **277.3px**; a 150px available-width case
  **150.0/150px**; auto inline-block and float **224.1px** each; and the
  min-content floor **52.8px** when only 20px is available.
- `make render-conformance`: **65/66** reftest pages and **40/41** body-origin
  pages match; the existing AVIF and empty-block margin-collapse known
  failures are unchanged. Canvas `toDataURL` checks: **45,016/45,016**.
- Following `tools/real_pages/holdout-01/README.md`, the H3 task still passes
  its interaction gate. Its open frame is measurably closer, not a pixel pass:
  the Litebird border box is **166.6×94px** (computed CSS width **128.6px**),
  versus Chromium's **293.7×111px** border box (CSS width **255.7px**). Against
  the byte-identical saved Chromium frame, ImageMagick `compare -metric AE`
  reports **24,300** differing pixels, down from the recorded **36,424**. The
  remaining width gap includes the native `<select>`, which still has zero
  intrinsic width and paints no control.
- `make bench-floats`: **0.119s** for `rows-10000`, **0.159s** for
  `mixed-10000`, and **0.168s** for `clear-10000`.

## What is still open

The MDN dialog is still much narrower than Chromium because the native
`<select>` has no intrinsic width or painted box; this phase does not add
form-control rendering. `::backdrop` is not author-addressable, and
`dialog:modal` still omits `overflow: auto` because `paint_atom` clips before
painting the dialog's own chrome. Min-content text widths are still estimated,
and flex/grid/table intrinsic contributions and the full Unicode line-breaking
model are not implemented here.
