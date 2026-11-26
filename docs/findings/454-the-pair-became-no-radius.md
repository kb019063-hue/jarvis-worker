# Finding 454 — the pair became no radius

Phase 443 fixed an information-loss bug and left a second one beside it. Four
corner values survived into `Path2D`, but each corner still had room for one
number. A corner with independent axes therefore went through scalar conversion:

```
{x: 20, y: 5} -> NaN -> 0
[20, 5]        -> "20,5" -> NaN -> 0
```

The first is the standard DOMPoint-like shape. The second is the engine's existing
array point spelling. Both direct `CanvasRenderingContext2D.roundRect` and
`Path2D.roundRect` reduced either to a square corner.

## Agreement was no longer an oracle

Phase 443's central test was relational: draw the same rectangle directly and
through a `Path2D`, then compare every pixel. That was exactly right for its bug,
where one route knew all four circular radii and the other route lost them.

After that fix, the relationship was insufficient. The direct and recorded forms
both answered the same square. A perfect byte-for-byte match proved only that the
shared limitation was shared.

The independent oracle was already public: `ellipse()`. A top-left 20×5 corner
can be written by hand:

```js
ctx.moveTo(25, 5);
ctx.lineTo(55, 5);
ctx.lineTo(55, 45);
ctx.lineTo(5, 45);
ctx.lineTo(5, 10);
ctx.ellipse(25, 10, 20, 5, 0, Math.PI, Math.PI * 1.5);
ctx.closePath();
```

The suite compares that path byte for byte with both `roundRect` routes. Restoring
the old point-to-zero conversion makes the first assertion fail even though the
two broken routes still agree with each other.

The rule is: **a relational oracle stops being independent when both sides join
above the defect.** Find a third route that joins below it. Here, direct and
recorded `roundRect` joined at circular geometry; a manually called `ellipse`
joined at curve flattening, below the missing x/y representation.

## One scale factor, not two clamps

The geometry change was not just replacing `arc` with `ellipse`. Adjacent
elliptical corners can overlap on either axis. The horizontal radii constrain the
width, the vertical radii constrain the height, and one minimum factor scales
every component. Scaling x and y separately would fit the rectangle while changing
the requested aspect ratio; clamping each corner independently would change the
relative sizes of corners.

The bite mutation that compares vertical overlap with x radii is useful because
it passes the obvious flat 15×5 case: horizontal overlap already selects 2/3. It
fails only the transposed 5×15 case, where width fits and height does not. One
orientation is not a two-dimensional test.

## A zero axis is a square corner

A DOMPoint dictionary defaults a missing member to zero. That exposes a geometry
trap: if `{x:20}` keeps the nonzero x coordinate but omits the degenerate ellipse,
the top edge ends at `x + 20` and the left edge starts at `y`, so the next segment
cuts diagonally across the corner. Normalising the pair to `{0,0}` when either
axis is zero keeps the corner square and the path connected along the rectangle's
edges.

This is asserted with `{x:20}` on both routes and compared to a zero-radius
rectangle. It is not left as an implementation comment because the diagonal looks
like an ellipse at many centre pixels.

## What remains validation, not geometry

The shared geometry now carries and paints x/y radii, but the binding still has
older validation compromises:

* a negative component becomes zero rather than throwing `RangeError`;
* more than four sequence entries are ignored after the fourth;
* an empty sequence draws a square rectangle rather than throwing;
* a negative rectangle dimension is made positive without swapping which radii
  belong to its physical corners.

Those have distinct observable error and corner-identity rules. They are recorded
rather than folded into this phase, whose claim is that an accepted positive
elliptical radius reaches the pixels with both axes intact.
