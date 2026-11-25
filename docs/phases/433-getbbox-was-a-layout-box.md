# Phase 433: `getBBox()` was a layout box, and SVG elements have no layout

## What a page saw

Phase 374 gave `SVGGraphicsElement` its `getBBox()` and the row was marked done.
The method was there. The geometry was not:

```js
var r = document.createElementNS(SVG_NS, 'rect');
r.setAttribute('x', '1'); r.setAttribute('y', '2');
r.setAttribute('width', '3'); r.setAttribute('height', '4');
r.getBBox();          // {x: 0, y: 0, width: 0, height: 0}
```

Every element answered all zeroes, because the getter read
`Element::bounding_box()` — the **HTML layout border box** — and an SVG element
never gets one: layout builds boxes for HTML boxes, and an `<svg>` subtree is
painted by the renderer without the layout tree knowing anything about it.

| query | Chromium | before | after |
| :--- | :--- | :--- | :--- |
| `rect(1,2,3,4).getBBox()` | `1,2,3,4` | `0,0,0,0` | `1,2,3,4` |
| `g` over that rect | `0,0,20,14` | `0,0,0,0` | `0,0,20,14` |
| `circle(cx=10 cy=10 r=4)` | `6,6,8,8` | `0,0,0,0` | `6,6,8,8` |
| `path d="M0 0 L10 10 L20 0"` | `0,0,20,10` | `0,0,0,0` | `0,0,20,10` |

**4/27 probes matched Chromium before; 26/27 after** — the differential is
`tools/conformance/verify_svg_bbox.py`, and the one remaining difference is
deliberate (below).

## The box is the geometry, and it has three rules

SVG 2 §8.10: the box of the geometry an element **renders**, in the element's
own user space. Three consequences, each asserted:

**The element's own transform is excluded; a child's is included.** A box is
in the element's own coordinates, so `g.getBBox()` answers where its children
draw, not where the `g` would be if it had a box:

| | Chromium | this engine |
| :--- | :--- | :--- |
| `rect` with `transform="translate(100,100)"` → own box | `1,2,3,4` | `1,2,3,4` |
| its parent `g`'s box | `0,0,104,106` | `0,0,104,106` |

**Stroke is not part of it.** `stroke-width="20"` leaves the box alone. That is
`getStrokeBBox()`, a separate member that is not built.

**Curve bounds are the curve's, not its control points'.** This was a real bug
in the renderer's own geometry, and the box exposed it:

| path | Chromium | was | now |
| :--- | :--- | :--- | :--- |
| `M0 0 C0 100 100 100 100 0` | height **75** | 100 | 75 |
| `M0 0 Q50 100 100 0` | height **50** | 100 | 50 |

`build_svg_path` included every control point in the box as though the curve
passed through it, which made a cubic's box 33% too tall. The extremes are
where the derivative is zero and are now solved per axis.

## Why the box comes from the renderer, not a second model

The geometry is `build_shape_geometry` and `build_svg_path` — the same two
functions the painter fills and strokes. `SvgRenderer.cpp` gained one exported
entry point, `svg_element_bbox()`, which walks the element and its descendants
and calls them. Nothing re-implements where a shape is, so a page cannot
measure a shape somewhere the screen does not draw it.

The walk has to know what draws where it stands: `defs`, `clipPath`, `mask`,
`symbol`, the gradient elements, `title`, `desc`, `metadata`, `style` and
`script` contribute nothing — the same set `render_element` returns on.

**A percentage is a share of the viewport the element draws in**, which a
`viewBox` replaces. `<svg width="200" viewBox="0 0 20 10">` is 20 user units
wide inside, so a child's `width="50%"` is **10**, not 100. That is the rule
`SvgRenderer::render` already applied when it painted, and Chromium agrees; the
first version of this phase used the `width` attribute and the differential
caught it.

**A degenerate shape still has a box.** `width="0" height="4"` answers
`[5,5,0,4]` — a line at its position — not an empty box. `build_shape_geometry`
returns false there because there is nothing to paint, so the box path answers
the degenerate cases itself.

## The one deliberate difference

A browser gives `getBBox` to every SVG element object, `defs` included. SVG 2's
interface table puts it on `SVGGraphicsElement` only, and this engine builds its
prototypes from that table, so `defs.getBBox` is `undefined` here. Phase 374's
suite asserts the same split. It is recorded rather than papered over: a
`SVGElement` is not a `SVGGraphicsElement` in this engine's interface model, and
changing that is a larger change than this phase.

## Verification

- `make test` **479/479**, `tests/suites/433-svg-bbox-is-the-geometry.cpp`
  passing; reverting the three changed files fails it at `:58` on
  `rect.getBBox()`
- reverting only the cubic bound fails it at `:102` on the `75` assertion, so
  that part of the phase is graded and not merely present
- the Chromium differential **26/27**, up from **4/27**
- DOM conformance **678/678** (probe 68/68) — unchanged
- render reftests **66/67** with the AVIF page still known, **+1 page**: the
  corpus had **no SVG page at all** before this phase, and
  `svg_shapes_paint_where_getbbox_says` now asserts that a rect, a circle and an
  arc paint where their geometry says, including the arc's rim at exactly one
  pixel above white
- a page rendered before and after the arc change is **byte-identical**, so the
  geometry work is render-neutral
- `make wpt` **9570/9824**, 254 listed, 0 unlisted — unchanged
- `make xml-conformance` **1718/1718** — unchanged
- `check_docs`, `check_numbers`, `check_suites` clean

## Still open

- `getStrokeBBox()` — the stroke-inclusive box, which is the other half of §8.10
- `getBBox({fill, stroke, markers, clipped})` ignores its options: SVG 2 lets a
  page ask for the stroke box or the clipped one, and neither is built
- `text.getBBox()` answers from the text element's own geometry, which is not
  implemented, so a `<text>` still answers an empty box where a browser answers
  the glyph run's
- `getClientRect()` and `getBBox()`'s relationship to the CTM: `getCTM()` and
  `getScreenCTM()` remain absent, so a page cannot map the box it measures into
  the viewport's coordinates
- `use` is not expanded in the box walk, so an element's box does not include
  what it references
