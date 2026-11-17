# 52. ✅ Canvas 2D could draw every shape except text

> Moved out of `AUDIT.md` — written there as a `## 52. ✅ Canvas 2D could draw every shape except text` section.
> The text below is the section as it stood, unchanged.


**The hole.** `CanvasRenderingContext2D` could draw paths, rectangles, arcs,
gradients and images, and it had no text at all: `fillText`, `strokeText` and
`measureText` did not exist, and neither did the `font`, `textAlign` and
`textBaseline` state. A page that labelled a chart, drew a scoreboard or
measured a string before laying it out got `undefined` for all three calls —
`ctx.measureText('x').width` threw `TypeError` — so no canvas text reached the
screen, by any route. The JavaScript-facing API and the property table the
canvas context was built with both knew every other member.

**The fix.** ROADMAP Wave 1, row 6 (the text gap); `include/litebird/render/
FontRenderer.hpp`, `src/render/FontRenderer.cpp`,
`include/litebird/render/Canvas2D.hpp`, `src/render/Canvas2D.cpp`.
`FontRenderer` gained two public accessors (`em_ascent_descent`,
`glyph_advance_px`) and `measure_text` now walks the same advances, so what
measures is what draws. The canvas paints a glyph run as an anti-aliased
coverage buffer (capped at 16384×512 px) blitted through the CTM by inverse
mapping of the destination box — the `drawImage` idiom — with a max filter
over the source span, so horizontal condensing never drops stems; gradient
paint is sampled in user space, the same as `rasterize_paths`, and clip,
`globalAlpha` and the composite operations go through `set_pixel_safe`. The
`font` attribute parses the CSS shorthand subset (style, variant and stretch
accepted and otherwise ignored, weight 100–1000 with ≥600 selecting the bold
face, a required size in px/pt/em/rem/% with an optional validated
`/line-height`, and a family list that is parsed and reflected but not used
for rasterisation — the engine still has one regular and one bold system
TrueType, Phase 36). An invalid `font` string is ignored and the previous
value kept; so are unknown `textAlign`/`textBaseline` values. `textAlign` is
`start` ≡ `left` (no `direction` yet); `textBaseline` is `top`, `hanging`
(approximated at 0.8em above the alphabetic baseline), `middle`, `alphabetic`,
`ideographic` (approximated at the bottom of the em box) and `bottom`.
`maxWidth` condenses the whole run horizontally (true scale, floored at
0.02×). `measureText` returns the whole `TextMetrics` set: `width`, the ink
boxes `actualBoundingBoxLeft/Right/Ascent/Descent` (relative to the natural
pen origin; the alignment point is not applied; can be negative), the font
boxes `fontBoundingBoxAscent/Descent` and `emHeightAscent/Descent`,
`hangingBaseline`, `ideographicBaseline` and `alphabeticBaseline` (0).
`strokeText` is an outline band (dilate minus erode by half the line width of
the binary glyph mask; lineWidth ≤ 1 yields the inner boundary ring), not a
stroked vector path — it is placement-accurate and paint-accurate, but glyph
corners get no joins or mitres, and that is documented in the header and in
ARCHITECTURE.md, Phase 46.

**How verified.** A new suite,
`canvas_text_filltext_stroketext_measuretext_and_font_state` (ten parts:
the default members; `measure_text` width ordering and metric boxes; a solid
fillStyle; the `textAlign` ink shift; the `textBaseline` placement; the
`maxWidth` ink bound; the stroke band widening with `lineWidth`; a gradient
fillStyle painting along the run rather than flat; `save`/`restore`
round-tripping the text state; the JavaScript bindings end to end — setter
validation, invalid assignments ignored, `measureText`'s properties,
`maxWidth`, rendering through the canvas context). A generated page (heading, gradient display type, stroked
text, centred and condensed lines) rendered with `--render-png` paints all of
it. The scores on the tree merged with `main` (Phase 42 and the
table-completeness phase included):

```
make test            # 179 engine suites, all passing (178 before, +1)
make conformance     # html5lib 1781/1792 — unchanged
make js-conformance  # 926/940 — unchanged
make dom-conformance # 644/644 — unchanged
make url-conformance # 1188/1188 — unchanged
make xml-conformance # 1718/1718 — unchanged
make wpt             # 7968/8009; 41 listed, 0 fail and are not listed
make diff-trees      # 6,000 of 6,000 pages equal to the corrected parse5
make diff-xml        # 4,000 generated documents, 0 differ; 3,000/3,000 round trips
make deep            # 1,332 runs over 465 pages at depth 200,000, 0 failed (slowest 23.7 s)
make asan            # ASan/UBSan, re-run green on this merged tree (Phase 47/48):
                     # XML ≥ its 1718 floor, WPT ≥ its 7968 floor with 0 unlisted,
                     # JS ≥ its 924 ASan floor, DOM ≥ its 644 ASan floor; diff-xml 1,000
                     # documents 0 DIFFER; round trip 1,000/1,000; fuzz (400 parser
                     # mutations, 400 pipeline runs, 600 XML mutations of 3,074 documents,
                     # the fuzz-xml round trip of 1,927 documents, 1,800 media) 0 findings;
                     # asan-deep
                     # 882 runs over 315 pages at depth 10,000 on a 2 MB stack,
                     # 0 failed (slowest 3.5 s)
```

**What this does not change.** No `direction`/RTL or bidi, no kerning or
ligatures, no use of the family list, no true vector glyph outlines under a
rotated CTM (the axis-aligned AA texture is sampled by nearest), no stroke
joins/mitres on glyph corners, ink boxes reported relative to the natural
origin regardless of alignment, and a 16384×512 px cap on a rasterised run
(paint truncates on the right; measurement is exact). SVG `<text>` and
WebP/AVIF are still not there (row of the task table); path and hit-test
gaps of ROADMAP row 6 remain. No expectation was re-recorded and no floor
was lowered.

---
