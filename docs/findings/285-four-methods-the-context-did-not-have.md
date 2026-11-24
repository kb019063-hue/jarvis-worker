# Finding 285 — four methods the 2D context did not have

**Severity:** medium. Four methods HTML §4.12.5.1 defines were `undefined` on
this engine's `CanvasRenderingContext2D`. Two of them throw at the call site, so
a page's drawing code dies on the line that asks for them; one silently leaves
the previous drawing's state in place; one cannot be asked for at all.

**Found by:** reading the canvas row's open items and then measuring the context
object instead of trusting the row — `typeof ctx.reset`, `typeof
ctx.isPointInStroke`, `typeof ctx.createPattern`, `typeof ctx.createConicGradient`
were all `"undefined"` where the context carries 56 other methods.

## What was wrong

The context object is assembled in `CanvasRenderingContext2D::to_js_value` and
two installers in `DomHtmlBindings.cpp`. Of the four:

| method | was | a page's result |
| :--- | :--- | :--- |
| `reset()` | `undefined` | `TypeError`; the state stays as the last drawing left it |
| `isPointInStroke(x, y)` | `undefined` | `TypeError` in a hit test that had no fallback |
| `createConicGradient(a, x, y)` | `undefined` | `TypeError` on the first line of the gradient code |
| `createPattern(image, rep)` | `undefined` | `TypeError`; a pattern fill cannot be requested |

## What changed

**`reset()`** — the whole drawing state back to its initial values, the current
default path emptied, the transform identity, the clip empty, and the state
stack **discarded** rather than popped. It is not `restore()`: `reset()` does not
answer the saves that are still open, and the suite asserts that a `restore()`
after a `reset()` does not resurrect what was saved before it.

There was a trap here, and it is the reason the binding lives in
`DomHtmlBindings.cpp` rather than beside the rest of the context.
`to_js_value` copies `fillStyle`, `strokeStyle`, `lineWidth` and
`globalCompositeOperation` onto the context object as **own data properties**,
and an own property shadows the live read in `JsValue::get_property`. A `reset()`
that put only the renderer's fields back left the object answering `#ff0000`
after the reset — measured, not reasoned about: `delete g.fillStyle` revealed
`#000000`, which is where the shadowing became visible. The binding therefore
calls the renderer's `reset()` and then writes the same four values back onto
the object.

**`isPointInStroke(x, y)`** — true within half the line width of a segment of the
current path. It walks the same device-space `m_paths` geometry
`is_point_in_path` already had, and it differs from that method in the one way
that matters: an **open** subpath has no closing segment, so a point on the
imaginary return line is on neither. The half width is scaled by the CTM's
longest axis, which is exact under a uniform transform.

**`createConicGradient(startAngle, x, y)`** — a third `CanvasGradient::Type`,
sampled as the angle from `startAngle` to the point, clockwise in the canvas'
coordinates (y grows down), divided by a full turn, then through the same stop
ramp and `spread` every other gradient uses. A non-finite argument is a
`TypeError`, which is the one case the method throws.

**`createPattern(image, repetition)`** — exists, takes both arguments, and answers
`null` in both of the standard's `null` cases: an unrecognised `repetition` and a
source that is not an image it can read. A `CanvasPattern` with `setTransform` is
returned otherwise. **It does not yet paint the tile when assigned to
`fillStyle`,** and that is stated in the binding's comment and below rather than
left to be discovered.

## The tell

A test of `typeof ctx.reset === "function"` passes on all four of the failures
above except the two that throw, and it passes on a `reset()` that does nothing.
The suite therefore asserts **answers**: the point on the stroke and the point
off it, the state before and after `reset()`, and the colours a conic gradient
paints at each cardinal.

One gap in that was found by attacking the suite, which is worth recording. The
first version asserted the conic ramp at east, south and west only; reversing
the angle (`startAngle - atan2` instead of `atan2 - startAngle`) still answered
red at east and still passed. With stops at 0, 0.25, 0.5 and 0.75 and all four
cardinals asserted, the reversal fails at south — and that injection was only
caught after the assertion was added, not by the suite as first written.

Four wrong versions were compiled, and each failed a different assertion:

| what was put back | assertion that failed |
| :--- | :--- |
| the pre-phase tree (all four `undefined`) | `typeof g.reset == "function"` |
| `reset()` fixing the renderer but not the object | `g.fillStyle == "#000000"` |
| the conic angle reversed | `canvas_pixel(g, 10, 19) == "0,255,0,255"` |
| — | (and the pre-suite version passed the third one, which is why it was rewritten) |

## Verification

```
$ make -j2 && make test
All 414 engine test suites passed successfully!
Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

$ make wpt
Web Platform Tests: 9569/9824 subtests pass in 190 files; 255 fail as listed, 0 fail and are not listed
```

`tests/suites/280-canvas-2d-reset-and-patterns.cpp`.

## What is still open

- **A `CanvasPattern` as a paint.** `fillStyle` accepts a `CanvasGradient` or a
  colour, through `JsValue::canvas_gradient_value()` and the fast-path in
  `JsValue::set_property`. A `CanvasPattern` needs a third slot on `JsValue` and
  a third branch in `rasterize_paths`, which is the paint the rasteriser keeps.
  That is a change to `JsValue` — a shared, large file — so it is named here and
  not done in passing. **This is the honest state: `createPattern` returns the
  right object and the right nulls, and assigning it to `fillStyle` is a no-op.**
- The same applies to `strokePattern`; the field exists and nothing reads it yet.
- `CanvasPattern.setTransform` stores the matrix and **nothing samples through
  it**: `sample_at` ignores `transform`. It is accepted so that a page that sets
  one is not refused, but it changes no pixels.
- `isPointInStroke` does not honour `lineCap: "round"`/`"square"` (which extend
  the stroke past each end) or `lineJoin: "miter"` (which extends it past a
  corner), nor `miterLimit`'s cut-off. The suite asserts the butt-cap rectangle
  behaviour, which is what the geometry walks.
- `reset()` does not restore `fillPattern`/`strokePattern` because nothing reads
  them; it does clear them.
- Not this row's: `createConicGradient` in a worker scope, `OffscreenCanvas`
  transfer, and `ctx.filter`.
