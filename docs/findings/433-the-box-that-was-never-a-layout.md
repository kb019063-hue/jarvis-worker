# Finding 433: the box that was never a layout

## The symptom

```js
var r = document.createElementNS(SVG_NS, 'rect');
r.setAttribute('x', '1'); r.setAttribute('y', '2');
r.setAttribute('width', '3'); r.setAttribute('height', '4');
r.getBBox();      // {x: 0, y: 0, width: 0, height: 0}
```

Every SVG element, every shape, every container: all zeroes.

## Why it answered zero, and why the phase before it believed it worked

The getter read `Element::bounding_box()`, which is the **HTML layout** border
box. Layout builds boxes for HTML boxes; an `<svg>` subtree is painted by
`SvgRenderer` and the layout tree never sees a rectangle inside it. So the field
was there, held a default `Rect()`, and was read back as four zeroes.

Phase 374's job was the interface table, and it did that job: `getBBox` moved
from nowhere to `SVGGraphicsElement`, and `typeof g.getBBox === 'function'`
became true. That is exactly what its suite asserted:

```js
assert(S("typeof g.getBBox") == "function");
```

and it was true, and the method it found returned nothing. **A test for the
existence of a member is not a test of the member.** The phase wrote "`getBBox()`
is in" in the row and moved on, and the row said it was done for the next
fifty-nine phases.

## Why nothing else caught it

- **jsdom has no `getBBox` at all.** It is not on `SVGElement` there, and
  `SVGGraphicsElement` does not exist. The DOM corpus is recorded from jsdom, so
  no case in it could have held a value that disagreed.
- **The render corpus had no SVG page.** `tests/render/reftests.json` was 66
  pages of HTML boxes, and not one of them drew an `<svg>`. Painting an SVG was
  therefore never graded by a pixel, and the geometry the painter used was never
  compared against the geometry the DOM reported. This phase adds the first SVG
  page to that corpus.
- **The two geometries were separate.** The renderer had its own, correct, path
  by which a shape reached the screen; the DOM had a field that was always
  empty. Nothing joined them, so nothing could notice they disagreed.

## What the box needs that the layout box never had

Three rules, each measured against Chromium rather than read off the
specification:

**The element's own transform is excluded, its children's included.** A box in
the element's own coordinates: `g.getBBox()` is where the children draw.

**Stroke is not in it.** `stroke-width="20"` changes nothing, because
`getBBox()` is the fill geometry and `getStrokeBBox()` is the member that would
say otherwise.

**Curve bounds are the curve's.** This one was a bug in the *renderer*, found
only because the box gave the path a second reader:

| `d` | Chromium | renderer's box |
| :--- | :--- | :--- |
| `M0 0 C0 100 100 100 100 0` | height 75 | 100 |
| `M0 0 Q50 100 100 0` | height 50 | 100 |

`build_svg_path` folded each control point into the box as though the curve
went through it. A cubic's control points are not on the cubic, so a tall arch
reported the full height of its control polygon. The extremes are now solved per
axis, and the arcs' too.

## The mistake this phase made and caught

The first version resolved a child's percentage against the `<svg>`'s `width`
attribute. With `<svg width="200" viewBox="0 0 20 10">` a child's
`width="50%"` came out **100**; Chromium says **10**, because the `viewBox`
replaces the coordinate system the children draw in. The renderer already had
this right — it hands `doc_ctx.viewport_w` to the children it paints — and the
DOM's new box had re-derived it wrongly. That is the argument for one unit of
geometry: two implementations of "how wide is this?" disagree the first time
either one is written from scratch.

## What is asserted

`tests/suites/433-svg-bbox-is-the-geometry.cpp` — seven shapes and a container,
the three rules above, a rotation mapped to its corners, the viewport rule both
with and without a `viewBox`, a degenerate rect, an empty path, `defs`
contributing nothing, nested transforms, and the interface table's own split.
Reverting the change fails it at `:58`; reverting only the cubic bound fails it
at `:102`.

`tools/conformance/verify_svg_bbox.py` keeps the Chromium comparison: 27 probes,
**4/27 before and 26/27 after**. The sixteenth is a genuine interface-table
difference (`defs.getBBox`), recorded in the phase document rather than hidden.

And the new reftest page is the guard that crosses the two halves: it asserts
that a rect, a circle and an arc **paint** where their geometry says — including
the arc's rim one pixel above white — so a future change to the geometric code
cannot move the screen without failing a pixel.
