# Phase 46 Architecture: Canvas 2D Text — `fillText`, `strokeText`, `measureText`

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 46 Architecture: Canvas 2D Text — `fillText`, `strokeText`, `measureText`` section.
> The text below is the section as it stood, unchanged.


ROADMAP.md Wave 1, row 6 names the text gap as the largest part of what
Canvas 2D was missing; this phase does that part (path and hit-test gaps
remain — row 6 stays open for them). Before it, a canvas could draw every
shape it has and no text at all: `ctx.fillText(...)` was `undefined`, so a
page that drew a label, a chart axis or a captcha painted nothing and any
script that measured its own strings threw `TypeError`.

### 1. What was added

- **`src/render/FontRenderer.cpp`** — two public accessors:
  `em_ascent_descent(font_size, weight)` returns the scaled em metrics
  (ascent above the baseline, descent below it, positive), falling back to
  (0.8em, 0.2em) when no `.ttf` loaded; `glyph_advance_px(cp, size, weight)`
  returns the advance at the exact size. `measure_text` now goes through the
  same path, so what measures is what draws. The system font is always the
  engine's regular/bold pair (Phase 36) — no family list, kerning or
  ligatures, as before.
- **`include/litebird/render/Canvas2D.hpp` / `.cpp`** — new drawing state
  `font` (`10px sans-serif` default), `text_align` (`start`) and
  `text_baseline` (`alphabetic`), saved/restored with the rest of the state,
  and three operations:
  - `fill_text(text, x, y [, maxWidth])` / `stroke_text(...)` — alignment
    resolves the pen position from `x` (`center` halves the advance, `right`
    and `end` subtract it; `direction`/RTL is not implemented, so `start` is
    `left` and `end` is `right`); the baseline resolves from `y`
    (`top` puts the em box top at `y`, `middle` centres it, `bottom` puts its
    bottom at `y`, `alphabetic` uses `y` directly, `hanging` is approximated
    at 0.8em above the alphabetic baseline, `ideographic` at the bottom of
    the em box). Text respects `globalAlpha`, `clip()`, both gradient and
    solid paints, and the CTM. `maxWidth` requests a horizontal condense of
    the whole run (true scaling down to a floor of 0.02×, not re-spacing).
  - `measure_text(text)` — `TextMetrics`: `width` (natural advance),
    `actualBoundingBoxLeft/Right/Ascent/Descent` (ink boxes relative to the
    natural pen origin; the alignment point is not applied, values can be
    negative), `fontBoundingBoxAscent/Descent` and `emHeightAscent/Descent`
    (em metrics), `hangingBaseline` (0.8em approximation),
    `ideographicBaseline` (descent) and `alphabeticBaseline` (0).
- **The `font` attribute** parses the CSS font shorthand subset: `style`
  (normal/italic/oblique, oblique widths ignored), `font-variant` and
  `stretch` (accepted, otherwise ignored), `weight` (normal/bold/bolder/
  lighter/100–1000; ≥600 selects the bold face), `size` **required**, in
  px/pt/em/rem/% with an optional `/line-height` (validated for syntax, not
  rendered), then a non-empty family list that is parsed and reflected but
  not used for rasterisation. An invalid font string is ignored and the old
  value kept, per the spec; the same for unknown `textAlign`/`textBaseline`
  values.
- **Rasterisation.** Glyphs are drawn into a tight AA coverage buffer
  (capped at 16384×512 px; a wider run truncates on the right for painting,
  measurement is unaffected), then blitted through the CTM by inverse
  mapping of the destination bounding box — the same inverse-map idiom
  `drawImage` uses. A device pixel maps back to the whole horizontal source
  span and takes the maximum coverage there, so down-sampling (condensing)
  never drops stems; the y axis samples the row nearest the inverse-mapped
  position. Gradients are sampled in user space, as `rasterize_paths` does,
  and `globalCompositeOperation`, clip and alpha run through the ordinary
  `set_pixel_safe` path.
- **`strokeText`** is an honest approximation, documented as such: a band
  centred on the binary glyph boundary (dilate(mask, d) minus erode(mask, d),
  d = half the line width, clamped to 16 px; lineWidth ≤ 1 yields the inner
  boundary ring). It is not a stroked vector outline — no joins or mitres on
  glyph corners. Placement, CTM, clip and paint are identical to `fillText`.

### 2. What this does not do (plainly)

- No `direction`: RTL/bidi is unparsed, `start` ≡ `left`.
- No kerning or ligatures; advance is the sum of glyph advances, and the
  same sum is used by measure and by draw.
- The family list is a no-op for rasterisation — the engine still has one
  regular and one bold system font (Phase 36).
- Rotated/sheared CTM places the run correctly but samples the axis-aligned
  coverage texture by nearest sampling: visibly blocky at large scales.
  True vector glyph rendering under rotation is future work.
- `strokeText` is the band approximation described above.
- Text metrics beyond those listed (e.g. boxes relative to the alignment
  point) are approximated relative to the natural origin.
- Rasterised runs are capped at 16384×512 px.

### 3. How it is verified

```
make test          # 179 engine suites, all passing (one new)
make conformance   # html5lib 1781/1792, unchanged
make js-conformance # 926/940, unchanged
make dom-conformance # 644/644, unchanged
make url-conformance # 1188/1188, unchanged
make xml-conformance # 1718/1718, unchanged
make wpt           # 7968/8009, the 41 listed failures, 0 unlisted
make diff-trees    # 6,000 of 6,000 pages equal to the corrected parse5
make diff-xml      # 4,000 documents, 0 differ; 3,000/3,000 round trips
make deep          # 1,332 runs over 465 pages at depth 200,000, 0 failed (slowest 23.7 s)
make asan          # ASan/UBSan green on this tree: XML ≥ 1718, WPT ≥ 7968 with 0 unlisted,
                   # JS ≥ 924, DOM ≥ 644, diff-xml 1,000 documents 0 DIFFER, round trip
                   # 1,000/1,000, fuzz 0 findings, asan-deep 882 runs over 315 pages,
                   # 0 failed (slowest 3.5 s; see AUDIT.md finding 52)
```

(Numbers as run on this branch; see AUDIT.md finding 52 for what each one
prints.) The new unit suite
`canvas_text_filltext_stroketext_measuretext_and_font_state` covers the
default and restored state, measurement ordering and metric boxes, painting
with a solid and with a gradient fill, alignment and baseline placement,
`maxWidth` condensing, the stroke band widening with `lineWidth`, setter
validation from JavaScript, and `measureText`'s property set end to end.
A generated page (heading, gradient display type, stroked text, centered and
condensed lines) rendered with `--render-png` shows glyphs painted along the
baseline with the gradient applied across the run.

### 4. Task queue

`ROADMAP.md` row 6 now reads path/hit-test gaps; the AGENTS.md row was
rewritten to what is left (WebP/AVIF, SVG `<text>`). No expectation was
re-recorded, no `.dat` corpus file was touched, and no floor was lowered;
the unit scoreboard went from 178 to 179 suites.

---
