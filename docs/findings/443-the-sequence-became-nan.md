# Finding 443 — the radius sequence became `NaN` while the answer already existed

Phase 426 listed this as still open:

> `roundRect`'s four radii are averaged to one, because the context's
> `round_rect` takes one.

Neither diagnosis matched the source that shipped.

`Path2D.roundRect` did keep only one number, but it did not average the sequence.
It passed the array itself through JavaScript `ToNumber`:

```
[15, 0, 0, 0] -> "15,0,0,0" -> NaN -> 0
```

The final zero is the geometry's non-finite-radius clamp. A request for one round
top-left corner therefore produced four **square** corners.

The context also already had both overloads:

```cpp
round_rect(x, y, w, h, radius);
round_rect(x, y, w, h, top_left, top_right, bottom_right, bottom_left);
```

The four-radius overload contains the complete geometry: it normalises a negative
width or height, rejects non-finite rectangle coordinates, clamps bad corner
values to zero, scales all four radii proportionally when adjacent corners do not
fit, and emits the lines and arcs in order. The direct JavaScript
`ctx.roundRect(..., [a, b, c, d])` binding already called it.

That is the finding: **a documented limitation can be a missed call site, not a
missing capability — and its own comment can misdiagnose its observable result.**
Before accepting “the lower layer cannot do it,” search for the overloads and
measure the sibling API that reaches the same geometry. Here, that sibling drew
the exact desired shape and turned a geometry task into a small recording task.

## Why losing the sequence was quiet

A 40×40 rectangle with square corners is entirely plausible. A test that checks
its centre, colour or bounding box passes. Even a test that asks whether every
edge is painted passes. Only a corner comparison exposes that the requested
15-pixel top-left arc disappeared.

The useful oracle was not a hand-computed Bézier. It was the context's own method:

```js
ctx.beginPath();
ctx.roundRect(5, 5, 40, 40, [15, 0, 0, 0]);
ctx.fill();

const p = new Path2D();
p.roundRect(5, 5, 40, 40, [15, 0, 0, 0]);
ctx.fill(p);
```

Those are two exposed forms of one operation. Pixel inequality between them is a
defect even before deciding which corner algorithm is ideal. The final suite uses
that relation throughout and finishes with a byte-for-byte comparison of the
original two canvases.

## One radius was not the grammar

Fixing only four literal numbers would leave the same family of bug in the
sequence rules:

* one value applies to all corners;
* two alternate across opposite corners;
* three use the middle value twice;
* four map clockwise from top-left;
* adjacent radii too large for an edge shrink proportionally together.

Recording four resolved corner values and replaying the established overload
handles all of those without adding another geometry algorithm.

The broad probe also found two cases where the engine's direct binding is itself
limited. An empty sequence falls through to a plain rectangle. An elliptical pair
is converted to `NaN` and becomes a zero circular radius. The first recorder fix
disagreed with both: it recorded nothing for the empty sequence and used the x
component of a pair. Both choices were defensible in isolation and wrong for this
phase, because they made `Path2D` and the direct method diverge again. The fix
matches the direct form; the remaining pair limitation is now explicit.

## The bite proof

The most important mutation restored **the exact Phase 426 source** after the
final code was in place. The test executable rebuilt cleanly, the path again
converted `[15,0,0,0]` to one number, and the suite failed immediately in Part 1.
A pixel probe confirmed all four `Path2D` corners were square while the direct
form rounded only top-left.

Two more mutations proved the edges are not documentary assertions:

1. returning without recording an operation for `[]` failed Part 4;
2. swapping top-right and bottom-left at replay failed Part 2's top-right pixel.

All three were compiled and restored. The suite is therefore sensitive to the
lost sequence, the empty-sequence operation, and the ordering between storage and
replay.

## The rule to keep

When two public entry points are supposed to describe the same object, make one a
**relational oracle** for the other. Compare their output before implementing new
geometry. If they differ, inspect where their paths join: this one joined at
`round_rect`, and all the necessary capability was already below that point.
