# Phase 454 — elliptical `roundRect` radii

Phase 443 made `Path2D.roundRect` preserve four distinct **circular** radii and
made its output match the context's direct method. It deliberately left the
shared limit visible: a corner with separate horizontal and vertical radii became
zero in both routes.

```js
ctx.roundRect(5, 5, 50, 40, [{x: 20, y: 5}, 0, 0, 0]);

const path = new Path2D();
path.roundRect(5, 5, 50, 40, [{x: 20, y: 5}, 0, 0, 0]);
```

Both calls drew a square top-left corner. A DOMPoint-like value was converted as
one JavaScript number, produced `NaN`, and was clamped to zero by the circular
geometry. The engine's existing nested-array point spelling, `[[20, 5], 0, 0,
0]`, took the same route.

Measured two pixels inside a 50×40 rectangle:

```
                       (7,7)  (10,7)  (7,15)  opposite square corner
before Phase 454          255      255      255           255
elliptical corner         102      255      255           255
```

The first value is antialiasing on the ellipse boundary. The important fact is
that it is no longer fully inside a square corner.

## One geometry, now with two axes

The circular overloads still exist, but they end in a new overload whose four
corners are `Point`s:

```cpp
round_rect(x, y, w, h,
           Point top_left, Point top_right,
           Point bottom_right, Point bottom_left);
```

For a corner, `Point::x` is the horizontal radius and `Point::y` the vertical
one. A scalar radius becomes `{r,r}`. `Path2D::Op::rr` stores the four points, so
recording and replay do not lose an axis.

The path is made from four lines and four calls to the context's existing
`ellipse()` method. There is no new Bézier approximation:

* top-right goes from angle `-π/2` to `0`;
* bottom-right from `0` to `π/2`;
* bottom-left from `π/2` to `π`;
* top-left from `π` to `3π/2`.

A corner with either axis zero is square. Retaining the other axis and skipping a
degenerate ellipse would otherwise create a diagonal between two edge points.

## The overlap factor has two dimensions

HTML scales all radii by one common factor when adjacent corners do not fit. For
ellipses, horizontal sums are compared with width and vertical sums with height:

```
min(
  width  / (top-left.x     + top-right.x),
  width  / (bottom-left.x  + bottom-right.x),
  height / (top-left.y     + bottom-left.y),
  height / (top-right.y    + bottom-right.y)
)
```

Only factors below one are applied. Multiplying both axes of all four corners by
the same factor preserves every ellipse's aspect ratio. The suite checks that
four 15×5 corners on a 20×20 box produce the same pixels as explicit 10×(10/3)
corners, and that the vertical 5×15 case becomes (10/3)×10.

## Argument forms

Both bindings now read Web IDL's numeric-or-DOMPoint shape:

* `8` becomes `{8,8}` and keeps the compact circular operation;
* `{x:20,y:5}` is one elliptical value, spread to all four corners;
* `[{x:20,y:5}, 0, 0, 0]` supplies four values;
* the engine's prior inner-array spelling `[[20,5], 0, 0, 0]` remains accepted;
* one-, two-, three-, and four-value sequences spread the complete pair, not its
  two components.

Missing DOMPoint dictionary members default to zero. Phase 443's observable empty
sequence fallback remains a square `Rect`.

## Test and bite proof

`tests/suites/454-roundrect-elliptical-radii.cpp` has one seven-part suite. Its
independent oracle is a path built by hand from lines and `ellipse()`. That is
important: after Phase 443, direct and `Path2D` output agreed even when both were
square, so comparing only those two could bless the shared defect.

Four compiled mutations were restored:

| mutation | caught by |
| --- | --- |
| arrays and DOMPoint objects become zero, reproducing the old picture | Part 1, manual ellipse vs direct `roundRect` |
| vertical radius replaced by horizontal radius | Part 1, manual ellipse mismatch |
| vertical overlap judged using x radii | Part 5, the tall oversize case does not scale |
| `Path2D` replay replaces y with x | Phase 443's existing relational pair assertion |

The suite also distinguishes a flat 20×5 corner from a tall 5×20 corner at two
pixels, compares DOMPoint and nested-array forms, checks 1/2/3/4 spreading,
preserves circular and empty-sequence behaviour, and proves that a recorded
ellipse remains in user space until the CTM at use.

## Still open

* Negative radii are clamped to zero; Web IDL's `roundRect` algorithm requires a
  `RangeError` for a negative component.
* More than four radii are currently truncated, and an empty sequence draws a
  square rectangle; validation should reject both before geometry is recorded.
* Negative width or height is normalised geometrically, but corner identity is
  not swapped with the flipped rectangle.
