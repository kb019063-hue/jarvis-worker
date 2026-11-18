# Phase 82 — Positioned SVG text on the canvas rasterizer

Task: the SVG `<text>` part of the “AVIF, and SVG `<text>`” open-tasks row. Finding 88.

## What was wrong

`SvgRenderer::render()` walked shapes, groups, `<use>`, gradients, clips, and nested SVGs, but `<text>` was not a shape and its text nodes were never visited. An otherwise valid `<svg width="120" height="50"><text x="10" y="32">AB</text></svg>` produced a transparent bitmap. The same SVG inside `<img>` or a page's SVG image box had no glyphs. Canvas 2D already had a font parser, fallback faces, glyph rasterization, text measurement, transforms, and clipping; SVG was not using them.

## What changed

The SVG renderer now visits `<text>` and its `<tspan>` children in document order, with a cursor in SVG user coordinates. The first `x`/`y` and relative `dx`/`dy` on a text element or span position that cursor; text advances it for the next run. Presentation attributes and inline style pass `font-size` (including percentages and em units relative to the parent), `font-family`, `font-weight`, `font-style`, and `text-anchor` through the same inherited `SvgStyle` that already carries fill, stroke, color, opacity, display, visibility, and transformations. Text uses Canvas 2D's font and pixel writer; it keeps SVG's viewBox, group transform, and clip already active on the canvas. Solid/currentColor paints and URL fallbacks paint the glyphs; fill `none` suppresses fill. The text walk bounds recursion and caps each text node at 16,384 input bytes.

SVG's default whitespace mode collapses ASCII whitespace runs across text-node/span boundaries and trims leading/trailing whitespace; `xml:space="preserve"` leaves a run's bytes alone. A span may change paint and its absolute position without flattening its parent into `textContent()` and drawing that parent twice. As in the rest of the renderer, a text element referenced through `<use>` goes through the same rendering path.

## Verification

- `make test` — **236/236** engine suites plus 114 cross-origin/channel checks. The new `svg_text_uses_canvas_glyphs_and_svg_positioning` suite compares every pixel with Canvas 2D's rasterization for plain, whitespace-normalized, anchored, transformed, and split painted text; two empty images cannot satisfy the assertion.
- `make verify-media` — **105** checks, zero failures, including **7** SVG checks. Two added SVG pixel-oracle cases assert an identical alpha mask translated exactly by `(21, 8)` independently of the chosen host font, solid red fill, and a positioned blue tspan against inherited red/group-transform paint.
- `make wpt` / `make asan` retain their existing floors; no WPT score gain is claimed by this change. (Record the merged-head results in the PR.)

## What remains

This is a bounded first text path, **not** full SVG text layout. Each text node is drawn as a run, so text-anchor is applied to each run rather than the entire anchored text chunk when tspans split it. Per-character x/y/dx/dy/rotate lists, nested textPath, proper SVG text CSS/whitespace in every case, font-feature shaping/bidi, and gradient paints on glyphs remain open. AVIF decoding is a separate codec task and is not changed here. Font rasterization still shares the broader shaping limitations recorded in ROADMAP.md.
