# Phase 443 — four `roundRect` radii through a `Path2D`

Phase 426 gave `Path2D.roundRect` one recorded radius. When the page supplied a
sequence, the binding converted the whole array to one number and replayed the
one-radius overload. `[15, 0, 0, 0]` converts to the string `"15,0,0,0"`, then to
`NaN`; the geometry clamps that radius to zero:

```js
const direct = document.createElement('canvas').getContext('2d');
direct.roundRect(5, 5, 40, 40, [15, 0, 0, 0]); // only top-left is round

const path = new Path2D();
path.roundRect(5, 5, 40, 40, [15, 0, 0, 0]);   // all four were square
```

Nothing threw. Both calls drew plausible rounded rectangles. They just did not
draw the same shape.

Measured at the four pixels two pixels inside a 40×40 box:

```
                         top-left  top-right  bottom-right  bottom-left
context roundRect            0        255          255          255
Path2D before Phase 443     255        255          255          255
Path2D after Phase 443        0        255          255          255
```

The context had already solved the hard part. Its four-radius `round_rect`
overload takes top-left, top-right, bottom-right and bottom-left separately,
normalises negative dimensions, clamps bad radii, and scales overlapping corners
proportionally. Phase 426's finding said that overload did not exist. It did; the
`Path2D` binding simply did not record enough information to call it.

## What changed

`Path2D::Op` now has a `RoundRectRadii` kind and four corner slots in the same
order as the context overload. One equal radius remains the old `RoundRect` kind.
That separation keeps the simple form simple and makes the four-radius replay
explicit:

```cpp
round_rect(op.a, op.b, op.c, op.d,
           op.rr[0], op.rr[1], op.rr[2], op.rr[3]);
```

There is no second geometry implementation. Replay calls the context's existing
overload, so proportional clamping of overlapping radii and negative-dimension
normalisation cannot drift between the direct and `Path2D` forms.

The recorder applies the same spreading rule as the context binding:

| supplied sequence | top-left | top-right | bottom-right | bottom-left |
| --- | ---: | ---: | ---: | ---: |
| `[a]` | a | a | a | a |
| `[a, b]` | a | b | a | b |
| `[a, b, c]` | a | b | c | b |
| `[a, b, c, d]` | a | b | c | d |

A bare number and no fifth argument stay on the one-radius path. An empty sequence
records `Rect`, matching the context binding's square-corner fallback rather than
recording no operation.

## The two edge cases a broad probe found

A first fix made the four-number example work and still disagreed with the context
in two forms:

* `roundRect(x, y, w, h, [])`: the context draws a plain rectangle; the first
  recorder returned without adding an operation and drew nothing.
* Elliptical pairs such as `[[8, 4], 0, 0, 0]`: the engine's context binding
  converts each pair with `to_number()`, gets `NaN`, and therefore uses a zero
  circular radius. The first recorder used the pair's x value and produced a more
  useful shape — but a different one.

This phase's boundary is equality with the already-exposed context operation, not
silently improving one of two routes to the same API. Empty sequences therefore
record a square rectangle, and pairs currently become zero radii on both routes.
Supporting elliptical corners belongs in the shared context geometry so both
routes improve together.

## Test

`tests/suites/443-path2d-roundrect-radii.cpp` has one suite in seven parts. It
compares direct and `Path2D` drawing for one-, two-, three- and four-value
sequences; no radii; an empty sequence; elliptical pairs; four overlapping radii;
and a mixed non-square case. The original `[15, 0, 0, 0]` failure is also compared
byte for byte over both 60×60 canvases.

Three mutations were built and restored:

| mutation | caught by |
| --- | --- |
| restore Phase 426's exact one-slot recorder (`ToNumber(sequence)` → `NaN` → zero) | Part 1, the direct and path ink differ |
| make an empty sequence record nothing | Part 4, direct draws a rectangle and the path does not |
| swap top-right and bottom-left during replay | Part 2, the top-right corner is rounded instead of square |

## Files

* `include/litebird/render/Path2D.hpp` — `RoundRectRadii` and four recorded corners.
* `src/js/DomHtmlBindings.cpp` — sequence spreading and recording.
* `src/render/Canvas2D.cpp` — replay through the existing four-radius overload.
* `tests/suites/443-path2d-roundrect-radii.cpp` — seven-part equality suite.

## Still open

* Elliptical corner pairs are reduced to zero by the context's circular-only
  binding. Fixing that needs x/y radii in the shared `round_rect` geometry, not a
  special case in `Path2D`.
* The context treats an empty radii sequence as a square rectangle rather than
  rejecting it. `Path2D` deliberately matches that observable behaviour.
* A recorded path is still flattened on every use; there is no per-transform
  cache.
