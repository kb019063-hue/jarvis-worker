# Phase 512 — border styles paint as their style, not as solid

**Finding:** [`docs/findings/512-border-style-was-never-painted.md`](../findings/512-border-style-was-never-painted.md)

CSS 2.1 §8.5.3 lists ten border styles. The parser accepted all ten,
`getComputedStyle` reported them, and the painter drew every one of them as
`solid` — `DrawBorderCommand` carried no style at all, and the same command
draws the outline ring. The `border` shorthand was worse still: it expanded
to a width and a colour and dropped the style outright, so
`border: 2px dashed red` itself was solid.

## What was wrong

Three stages, three separate gaps:

1. **The cascade kept nothing.** `ComputedStyle` had no border-style field at
   all. The CSSOM answered out of the declaration block
   (`DomStyleBindings.cpp` needed no changes; its tables already knew the
   property), so the report could not have betrayed the silence.
2. **The shorthand ate the style.** The `border`/`border-*` expansion emitted
   a width and a colour and discarded the style token, which is why even an
   explicit `border: 2px dashed red` painted solid.
3. **The painters had no input.** Neither the bitmap nor the terminal backend
   could have drawn a dash, let alone an `inset`.

## What changed

- **Cascade** (`StyleResolver.hpp`/`.cpp`): a `BorderStyle` enum (`none`,
  `hidden`, `dotted`, `dashed`, `solid`, `double`, `groove`, `ridge`,
  `inset`, `outset`) and a per-edge `EdgeStyles` parallel to `EdgeSizes`
  (initial `none` everywhere, hashed with the style). `border-style` takes
  one to four keywords (top/right/bottom/left, box rules); an invalid
  keyword drops the declaration, as everywhere else in the cascade.
  `border-*-style` longhands, `initial`/`unset` → `none`, `inherit` keeps
  the parent copy. `border: 2px dashed red` now emits
  `border-style: dashed` alongside the width and colour (and
  `border: none` emits `border-style: none`). `outline-style` is stored the
  same way (`auto` is accepted as Firefox draws it, stored as `solid`;
  `initial`/`unset` → `none`), and `outline_drawn` is gone.
- **Display list** (`PaintCommand.hpp`/`.cpp`): `DrawBorderCommand.styles`
  (default `EdgeStyles(BorderStyle::Solid)`). The ordinary border feeds the
  box's styles; the outline ring feeds `EdgeStyles(outline_style)`, gated on
  `border_style_draws(outline_style)` and a non-zero width, so
  `outline-style: none` paints no ring at all.
- **Bitmap painter** (`BitmapRenderer.cpp`): every edge paints in its own
  style. `double` = two bands at `t/3` with a `t/3` gap (a single solid band
  below 3px); `dashed` = 3× width long dashes, 2× width gaps (min 2px);
  `dotted` = dots of diameter `t` spaced `2t`; `groove`/`ridge` = the band
  split in half, the two tones halfway towards black and halfway towards
  white, outer half dark for `groove` (reversed for `ridge`, and reversed
  again on the bottom/right edges); `inset`/`outset` = full edge in one
  tone — `inset` dark on top/left and light on bottom/right, `outset` the
  inverse. `none`/`hidden` paint nothing. Horizontal edges paint first and
  own the corner overlap. `groove`/`ridge`/`inset`/`outset` derive their
  tones from the edge's own colour (or per-edge colour override), so a
  coloured border shades its own ink rather than introducing greys.
- **Terminal painter** (`TerminalRenderer.cpp`): the border and outline
  branches paint per edge with glyph approximations of the same styles and
  are gated per edge by `border_style_draws`, so `none` produces no cells.
- The UA stylesheet's literal `hr { border-style: inset; … }` and
  `fieldset { border-style: groove; … }` now paint as what they say.

## What it measured

- One new suite: `tests/suites/512-border-styles.cpp`, one TEST with
  resolver bookkeeping blocks (shorthand split, longhands, 1–4 keyword box
  form, bad keyword dropped, per-edge and outline longhands) and ink blocks
  (`dashed`, `dotted`, `double`, `groove`, `ridge`, `inset`, `outset`,
  `hidden`, `border-style: none`, and three outline-ring blocks reading back
  pixels, plus the same declarations through `getComputedStyle`). Writing it
  is what surfaced the `border-top-style` off-by-one: `prop.compare(7, 4,
  "top")` for a three-character token, so the top edge never applied the
  box expansion — the fix and the suite are in the same commit.
- `make test`: **552/552** engine suites pass (551 + this one) and the
  cross-origin suite passes 114/114.
- `make render-conformance` (`tools/conformance/render_check.py
  --binary build/litebird`): **67/68** pages, the one miss being the
  recorded AVIF `known_failure` (68 of 68 accounted for).
  `--suite tests/render/body-origin.json`: **41/41**. `canvas_to_data_url.py`:
  **45016/45016**. The unfixed cascade broke
  `flex_stretch_subtracts_the_padding_and_the_border` (top border never
  drawn); it passes with the fix, and the baseline was verified by rebuilding
  without it.
- `make wpt`: **9570/9824**, no subtest newly failing (floor 9570,
  `0 fail and are not listed`).
- The rest of the ledger, all green: `make conformance` 1973/1973;
  `make js-conformance` 1153/1158 (everyday 90/90);
  `make dom-conformance` 678/678; `make url-conformance` 1188/1188;
  `make xml-conformance` 1718/1718; `make diff-trees` 1500×4, 0 differ;
  `make diff-xml` 0 DIFFER (round trip 3000/3000); `make fuzz` six of six
  parts, 0 findings; `make deep` 1332 runs, 0 failed;
  cross-origin Window/Location suite 114/114.
- `make asan` on this two-core, 4 GB sandbox: **550 of 552** suites pass
  under the sanitizer (the unity `test_suite.cpp` had to be built with
  `-g1` for its compile to fit). The two skipped suites are both
  pre-existing and environmental, neither is this phase's code:
  `deep_nesting_never_exhausts_the_native_stack` never returns here under
  ASan (the kernel OOM-killed it at ~3.9 GB RSS; it passes in the normal
  build at 14 s) and
  `automatic_gc_reclaims_churn_without_calling_gc` asserts
  `heap.auto_gc_collections() > auto_before_vm` under ASan only (it passes
  in the normal build; nothing in this phase touches the GC). The 550
  include `border_styles_paint_as_their_style_not_as_solid`, which is
  clean under ASan. The cross-origin suite under ASan: 114/114.

## Still open

- The outline ring is square on a rounded box.
- The terminal backend approximates the compound styles in glyphs; `dotted`
  is circles in the bitmap backend and per-edge marks in the terminal.
- `hidden` paints like `none`; collapsed-table conflict resolution is not
  modelled anywhere in this engine.
- A declared `border-width` is still reserved in layout when
  `border-style: none` — the used-value zeroing was considered and
  deliberately not taken.
- At the corners the horizontal bands own the overlap.
- Table `border` styles remain a single solid style — table borders are
  out of scope for this phase (ROADMAP row 17).
