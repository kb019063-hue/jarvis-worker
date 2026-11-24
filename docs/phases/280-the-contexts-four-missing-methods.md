# Phase 280 — the context's four missing methods

Four methods §4.12.5.1 gives `CanvasRenderingContext2D` were `undefined` on this
engine's context object, measured rather than assumed:

```
typeof ctx.reset               -> undefined
typeof ctx.isPointInStroke     -> undefined
typeof ctx.createPattern       -> undefined
typeof ctx.createConicGradient -> undefined
```

Two of them throw at the call site, so a page's drawing code dies on the line
that asks for them. One leaves the previous drawing's state in place, so the
next fill is the old colour. One cannot be asked for at all.

## What changed

**`reset()`** — every state property back to its initial value, the path
emptied, the transform identity, the clip empty, the state stack **discarded**
rather than popped. The suite asserts the difference from `restore()`: a
`restore()` after a `reset()` must not resurrect what was saved before it.

The trap: `to_js_value` copies `fillStyle`, `strokeStyle`, `lineWidth` and
`globalCompositeOperation` onto the context object as **own data properties**,
and an own property shadows the live read in `JsValue::get_property`. A `reset()`
that put back only the renderer's fields left the object answering `#ff0000`;
`delete g.fillStyle` showing `#000000` is how that became visible. So the binding
calls the renderer's `reset()` and then writes those four values back onto the
object. Binding it twice — once in `Canvas2D.cpp`, once in
`DomHtmlBindings.cpp` — was the first attempt and was removed: the second wrapper
is the one that runs, so the first was a trap for the next reader.

**`isPointInStroke(x, y)`** — within half the line width of a segment of the
current path, walking the same device-space geometry `is_point_in_path` uses. An
**open** subpath has no closing segment, so a point on the imaginary return line
is on neither; that is the one place the two methods deliberately differ, and it
is asserted. The half width scales with the CTM's longest axis.

**`createConicGradient(startAngle, x, y)`** — a third `CanvasGradient::Type`,
sampled as the angle from `startAngle` to the point, clockwise (y grows down),
over a full turn, then through the same stop ramp and spread as every other
gradient. A non-finite argument throws `TypeError`.

**`createPattern(image, repetition)`** — both arguments read, `null` for an
unrecognised `repetition` and for a source that is not a readable image, and a
`CanvasPattern` with `setTransform` otherwise. **It does not paint yet:** see
below.

## How it was verified

```
$ make -j2 && make test
All 414 engine test suites passed successfully!
Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

$ make wpt
Web Platform Tests: 9569/9824 subtests pass in 190 files; 255 fail as listed, 0 fail and are not listed
```

The suite is `tests/suites/280-canvas-2d-reset-and-patterns.cpp`. Three wrong
versions were compiled, each failing a different assertion: the pre-phase tree
(all four `undefined`), a `reset()` that fixed only the renderer, and a conic
angle computed the other way round.

**The third one passed at first**, which is the part worth keeping. The suite
asserted the ramp at east, south and west; reversing it still answers red at east
and still passed. Adding stops at 0, 0.25, 0.5 and 0.75 with **all four**
cardinals asserted makes the reversal fail at south. The attack found a gap in
the test, not only in the code.

## What is still open

**A `CanvasPattern` is not a paint.** `fillStyle` accepts a `CanvasGradient` or a
colour, carried by `JsValue::canvas_gradient_value()` and the fast-path in
`JsValue::set_property`, and the rasteriser keeps one gradient per style. A
pattern needs a third slot on `JsValue` and a third branch in `rasterize_paths`.
That is a change to `JsValue`, a large shared file, so it is named and not done
in passing: **`createPattern` returns the right object and the right nulls, and
assigning it to `fillStyle` is a no-op.** The `CanvasPattern`'s own
`setTransform` is stored and nothing samples through it, so it changes no pixels
either.

Also open: `isPointInStroke` ignores `lineCap: round`/`square` and
`lineJoin: miter` (both extend the stroke past the geometry the walk uses) and
`miterLimit`; and `ctx.filter` remains absent, which is another row.
