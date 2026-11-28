# Finding 475 — a flipped `roundRect` kept radii on the wrong corners

The rectangle and its corner radii describe one shape. The engine normalized a
negative width or height by moving the origin and taking the positive dimension,
but did not apply the matching reflection to the corner identities. A distinct
radius that began at the top-left of the supplied rectangle could end up drawn at
the physical top-left after a horizontal flip, even though that supplied corner
had moved to the physical top-right.

## Why direct-versus-path comparison was not enough

The direct context call and `Path2D.roundRect` both reach
`CanvasRenderingContext2D::round_rect`. Their pictures therefore remained equal
while both used the wrong physical corners. A relational test must compare a
flipped rectangle with a positive rectangle at its normalized origin and with its
radii explicitly permuted:

* negative width swaps top-left with top-right and bottom-left with bottom-right;
* negative height swaps top-left with bottom-left and top-right with bottom-right;
* both flips compose into a 180-degree rotation.

The suite uses four distinct elliptical radii, checks that the expected positive
controls are observably different, then compares every RGBA byte for each sign
combination on both API routes.

## What changed

The shared geometry swaps the corner `Point`s while normalizing each negative
dimension. This keeps radii attached to the same physical corners after the
rectangle is made positive. It also means overlap scaling receives the radii in
their corrected positions. No `Path2D` recorder change is needed because replay
uses this geometry.

## Verification

After merging the current `main`, `make -j2 test` — **532/532 engine suites
passed; 114/114 cross-origin checks passed**.

## Still open

No remaining corner-identity limitation for negative `roundRect` dimensions is
known. Path flattening and other unrelated `Path2D` behavior remain outside this
finding.
