# Phase 107 — SVG text: one value per character, one chunk per anchor

Task row: **AVIF, and the rest of SVG text layout** — the SVG half of that row.
Finding 113.

## 1. What was wrong

Phase 82 laid `<text>` out one text node at a time. It measured that node with
`measure_text`, put the cursor at the element's `x`/`y` (a single float — the
parser read the attribute with `parse_float_value`, so a list was read as its
first number and the rest of the string was ignored), set
`ctx.text_align` from `text-anchor`, drew the string, and advanced. That works
for one run of one style. It is wrong for everything SVG's text model is for.

**`text-anchor` is a property of the chunk, not of the node.** A chunk is the
run that begins at an absolute `x` or `y`. With

```svg
<text x="60" y="32" text-anchor="middle" font-size="24">
  <tspan fill="red">A</tspan><tspan fill="blue">B</tspan>
</text>
```

the two glyphs are one chunk and the pair is centred on 60. The old code
centred `A` on 60 and then centred `B` on 60 + the advance of `A`, so the pair
sat to the right of its anchor by half a pair.

**`x`, `y`, `dx` and `dy` are lists**, one value per character, and `rotate`
was not read at all. `x="10 30 50"` places three characters at three absolute
positions; `dx="0 5 0"` shifts the second by five; `rotate="30 0"` turns the
first glyph 30° and the second 0°, and — SVG 1.1 §10.5 — the last value in a
`rotate` list carries on to the characters the list does not reach, which is
the one list that repeats. None of it happened.

**`<textPath>` was not read.** The walker descended into `<tspan>` and skipped
every other element, so a `<textPath>` child was dropped and its text was
never drawn.

**A gradient on a glyph was skipped.** The paint function in the text path
ended in `else return; // A text gradient/path is not yet a supported paint
server`, while the same attribute on a `<rect>` went through
`build_svg_gradient`.

## 2. What changed

The text is collected as glyphs first and drawn second, which is what makes a
chunk measurable before any of it is painted.

- **A collector.** `TextCollector` walks the `<text>` subtree in document
  order and produces `TextGlyph`s: one code point each, with the style it
  inherits, the `<textPath>` it belongs to, the transform that wraps it, and
  the position directives that were in force when it was reached. Whitespace
  collapsing is unchanged from Phase 82 (`xml:space`, a run of spaces becomes
  one, leading and trailing spaces are dropped), and a 16,384-byte bound still
  applies to one text node.
- **Position lists as a stack.** Every `<text>`, `<tspan>` and `<textPath>`
  that carries `x`, `y`, `dx`, `dy` or `rotate` pushes a `PositionList` with
  its own cursor, because the spec counts characters "within this element or
  any of its descendants". Values are resolved with `parse_length_list`, which
  accepts `10, 20 30` and percentages against the viewport, and each active
  list contributes one value per drawn character: `x`/`y` are absolute (the
  innermost element's wins), `dx`/`dy` accumulate, and `rotate` falls back to
  the last value it has.
- **Chunks.** An absolute `x` or `y`, a change of `<textPath>`, or a change of
  tspan transform starts a new chunk. The pen is carried across chunk
  boundaries, so a `<tspan>` that names only `y` keeps the x the previous run
  left it at. Within a chunk the glyphs are grouped into segments — maximal
  runs of one style at one position — and each segment is drawn with one
  `fill_text`, so a run that has no list in it is still whatever the shaper
  makes of it: `AV` is kerned exactly as Canvas kerns it.
- **Anchoring after measurement.** The segments are laid out and measured
  first; the chunk's extent is the advance from the first segment's origin to
  the cursor after the last, and `middle`/`end` shift every segment by half or
  all of it. A chunk with an absolute `x` inside it is not one chunk, which is
  why the split happens before the anchor is computed.
- **`<textPath>`.** `build_svg_path` and `emit_svg_arc` became templates over
  the sink that receives the path, so the same parser that fills a Canvas path
  can fill a `PathFlattener` instead: a polyline with 24 steps per cubic, 16
  per quadratic and the arc conversion's own subdivision. `PathSampler` walks
  that polyline by arc length and answers the point and the tangent at a
  distance. `startOffset` is a length or a percentage of the path's own
  length, the anchor shifts that distance, and each glyph is drawn turned by
  the tangent, so text on a diagonal sits on the line instead of beside it.
- **One attribute lookup that could not find its attribute.** SVG is parsed
  with the case the document used, and `get_attr_ci` fell back to the
  lowercased name only when that differed from the query — which it never
  does, because the query *is* the lowercased name. So a caller that asked for
  `startoffset` never found `startOffset`, and `startOffset` was silently 0.
  The helper now walks the element's attributes case-insensitively when the
  exact and lowercased lookups both miss.
- **Paint servers.** `resolve_svg_paint` is the one answer to "what colour is
  this paint server" — solid, `currentColor`, a gradient built from the
  document, or the fallback colour after an unresolvable `url(#…)` — and both
  `paint_shape` and the text painter call it. A chunk that asks for a gradient
  is resolved against the box its glyphs occupy, and the Canvas gradient is
  sampled per pixel by `blit_text`, so the ink is the gradient and not a
  stand-in for it.

## 3. How it was verified — a command, and the number it prints

```bash
make test
```

`265 / 265` engine suites, `114 / 114` cross-origin checks. Four of the suites
are new, and every one of them builds the same 200×80 image twice — once
through `SvgRenderer::render` and once with `CanvasRenderingContext2D`, asking
for the same glyphs at the position SVG asks for — and compares the pixels:

- `svg_text_position_lists_place_each_glyph` — a run with no list still kerns
  (`AV` at 40 px, the same 637 ink pixels as one Canvas run); `x="10 30 50"`
  puts three glyphs at three positions; `y="20 50"` moves one glyph down and
  keeps its x; `dx="0 5 0"` shifts the second; `rotate="30 0"` turns the first
  and leaves the rest upright; a `<tspan transform>` still moves its run.
- `svg_text_anchor_measures_the_whole_chunk` — `middle` over two `<tspan>`s of
  different colours centres the pair (354 ink pixels, x 43–74, in both
  images); `end` puts the pair's last glyph at the anchor; an absolute `x` on
  a tspan starts a chunk, so the parent's `end` stops at 10 and the tspan's
  `start` begins at 70.
- `svg_text_paint_servers_paint_the_glyphs` — a `userSpaceOnUse` linear
  gradient on `<text>` is pixel-identical to the same gradient on a Canvas
  context (334 ink pixels); `url(#missing) lime` falls back to lime.
- `svg_text_path_lays_the_glyphs_along_a_path` — a horizontal path, the same
  path at `startOffset="50%"` (A at 60), a 45° diagonal where each glyph is
  turned with the tangent (330 ink pixels, x 10–42, y 1–30 in both), and a
  path that does not resolve, which paints nothing.

The pre-existing `svg_text_uses_canvas_glyphs_and_svg_positioning` still
passes unchanged: first `x`/`y`, `dx`/`dy`, whitespace, a `<g>` transform, a
tspan's own paint and absolute x, a middle anchor, and `fill="none"`.

## 4. What is still open

- **AVIF**, the other half of the row. An AV1 intra-frame decoder is a phase of
  its own, not a corner of this one.
- `textLength` / `lengthAdjust`, `letter-spacing`, `word-spacing`,
  `dominant-baseline` beyond `alphabetic`, `baseline-shift`, `<tref>`, and
  bidi: none of it is read.
- Text on a path ignores the referenced element's own `transform` and only
  uses its `d`; a shape that is not a `<path>` (a `<circle>`, say) has no `d`
  and is not followed.
- The flattening step is fixed (24/16 per curve), so a point along a curve is
  an approximation of the curve, not a point on it. A test that needs the
  exact arc would have to sample the same fractions.
- A chunk's extent is the sum of its segments' advances. Kerning is therefore
  applied inside a segment and not across a style change, which is the honest
  description of what a Canvas-measured segment can promise.
- `x`/`y` lists advance one value per *drawn* character, so a space that
  whitespace collapsing removed does not consume a value.
- `<textPath>` does not carry `dy` per glyph, and a glyph that runs past the
  end of the path is dropped rather than clipped.
