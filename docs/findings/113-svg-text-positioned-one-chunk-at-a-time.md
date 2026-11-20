# Finding 113 — SVG text was positioned one node at a time

Phase 107. The architecture is in
[`docs/phases/107-svg-text-positions-chunks-textpath-and-paint.md`](../phases/107-svg-text-positions-chunks-textpath-and-paint.md).

## 1. What was wrong

Phase 82's text walker measured one text node, drew it, and moved a cursor. It
read `x`, `y`, `dx` and `dy` with `parse_float_value`, which takes the first
number in the string and stops, so every list attribute became its first value;
it never read `rotate`; it turned `text-anchor` into `ctx.text_align` for the
node it was about to draw; it descended into `<tspan>` and nothing else; and
its paint function returned early on a gradient.

Four separate wrong answers came out of that one shape:

```svg
<!-- 1. the anchor is the chunk's, not the node's -->
<text x="60" y="32" text-anchor="middle">
  <tspan fill="red">A</tspan><tspan fill="blue">B</tspan>
</text>
<!-- the pair is centred on 60; the old code centred A on 60 and put B
     after it, so the pair sat half a pair to the right -->

<!-- 2. the lists are per character -->
<text x="10 30 50" y="32">ABC</text>
<!-- A, B and C at 10, 30 and 50; the old code drew all three from 10 -->

<!-- 3. a rotate list, whose last value carries on -->
<text x="10" y="40" rotate="30 0">ABC</text>
<!-- A at 30°, B and C upright; the attribute was not read at all -->

<!-- 4. a gradient is a paint server like any other -->
<text x="10" y="40" fill="url(#g)">A</text>
<!-- the ink is the gradient; the old code skipped the run entirely -->
```

and a fifth, which was not a wrong answer so much as a missing one:

```svg
<text font-size="24"><textPath href="#p">AB</textPath></text>
<!-- nothing was drawn: the walker only descended into <tspan> -->
```

## 2. What changed

Text is collected as glyphs, measured, and only then drawn — which is what
makes a chunk measurable before any of it is on the canvas. `x`/`y`/`dx`/`dy`
resolve per character from a stack of lists (one per element that names one,
because the spec counts characters "within this element or any of its
descendants"), `rotate` repeats its last value and nothing else does, an
absolute `x` or `y` starts a chunk, and `text-anchor` shifts the whole chunk
after its segments have been measured. `build_svg_path` became a template over
its sink, so `<textPath>` flattens the same `d` a shape would draw and walks
it by arc length, turning each glyph by the tangent. One thing stood in the
way of `startOffset`: `get_attr_ci` fell back to the lower-case spelling only
when that spelling differed from the query, which it never does, so
`startoffset` never matched the document's `startOffset`; the helper now
scans the attributes case-insensitively as a last resort. `resolve_svg_paint` is now
shared by shapes and glyphs, so `url(#…)` and the fallback colour after it
reach both.

## 3. How it was verified

```bash
make test        # 265 / 265 suites, 114 / 114 cross-origin checks
```

Four new suites, each comparing an SVG render against the same glyphs drawn
through `CanvasRenderingContext2D` at the position SVG asks for, pixel for
pixel: the position lists (`svg_text_position_lists_place_each_glyph`), a
chunk's anchor across `<tspan>`s (`svg_text_anchor_measures_the_whole_chunk`),
gradient ink and its fallback (`svg_text_paint_servers_paint_the_glyphs`), and
`<textPath>` on a line, at a `startOffset`, and at 45° where the tangent is
what makes the glyph sit on the line
(`svg_text_path_lays_the_glyphs_along_a_path`). The Phase 82 suite passes
unchanged, and its `AV`-style plain run is the check that batching a segment
back into one `fill_text` did not lose the font's kerning.

## 4. What is still open

`textLength`/`lengthAdjust`, `letter-spacing`, `word-spacing`, baselines below
`alphabetic`, `<tref>`, bidi, `rotate` on a `<textPath>` glyph beyond the
tangent, a path's own transform, and AVIF — the other half of the row this
came from. The list is the "What is still open" section of the phase write-up.
