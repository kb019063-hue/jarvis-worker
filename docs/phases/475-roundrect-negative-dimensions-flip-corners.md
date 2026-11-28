# Phase 475 — negative dimensions flip `roundRect` corner identities

`roundRect(x, y, w, h, radii)` accepts negative width and height. The shared
`CanvasRenderingContext2D::round_rect` geometry already normalized those
rectangles by shifting the origin and making each dimension positive, but it kept
the radii in their original slots. When the rectangle was flipped, a radius
therefore stayed with the old slot instead of following its physical corner.

This affected both the context's direct `roundRect` and `Path2D.roundRect`:
`Path2D` replays its recorded operation through the same geometry, so the two
routes agreed on the same incorrect corner assignment.

## The corner mapping

Radii are held in clockwise order: top-left, top-right, bottom-right,
bottom-left. Normalizing a negative width reflects that order across the vertical
axis, so the left/right pairs must swap:

| dimensions | normalized corner radii |
| --- | --- |
| `w < 0` | `[TR, TL, BL, BR]` |
| `h < 0` | `[BL, BR, TR, TL]` |
| `w < 0 && h < 0` | `[BR, BL, TL, TR]` |

`round_rect` now swaps the `Point` radii at the same time as it normalizes each
axis, before normal radius validation and overlap scaling. The width flip swaps
TL↔TR and BL↔BR; the height flip swaps TL↔BL and TR↔BR. The operations compose,
so a rectangle flipped on both axes rotates the corner identities by 180 degrees.

The change stays in the shared geometry. No direct-context or `Path2D` binding
needs special handling, and positive dimensions and radius-validation behavior
remain unchanged.

## Test

`tests/suites/475-roundrect-negative-dimensions-flip-corners.cpp` uses four
distinct elliptical radii and compares full-canvas RGBA output for negative
width, negative height, and both negative against positive normalized rectangles
with the expected corner permutations. The direct and `Path2D` routes are both
checked. Preliminary control assertions prove that each permutation produces a
different image, so the relational checks cannot pass with interchangeable
corners by accident.

## Verification

`make -j2 test` — **524/524 engine suites passed; 114/114 cross-origin checks
passed**.

## Still open

No further `roundRect` corner-identity limitation is known. This phase does not
change unrelated `Path2D` concerns such as cached flattening.
