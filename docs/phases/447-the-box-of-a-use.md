# Phase 447: the box of a use

## What was wrong

Phase 433 made getBBox answer shape and group geometry, but `<use>` still
returned an empty box even when it drew a referenced shape. A rect at
(1,2) with size (3,4), scaled by two, used at (10,20), answers
`12,24,6,8` in Chromium; Litebird answered `0,0,0,0`.

## What changed

The box walk resolves local fragment references through the owning document,
walks the referenced shape or group, applies its transform and the use's x/y,
and continues to exclude the use element's own transform. Nested uses are
supported and an active-reference set and a depth limit stop cycles. The
reference is resolved on every call so mutations are visible. The same corner
mapping already used for children is shared with references.

## Verification

`python3 tools/conformance/verify_svg_bbox.py` compares against pinned Chromium:
33/34 probes match, up from 27/30 on the original tree with the first three
new probes. The remaining difference is the previously documented `defs`
interface difference, not a geometry failure. See that script and Phase 433
for the browser installation recipe. `make test` passed 495/495 suites.
Reverting just `src/render/SvgRenderer.cpp` made the new suite fail its first
use-box assertion. The ordinary build was restored afterward.

## Still open

This handles references to shapes, groups and other uses only. Referenced
`symbol` and nested `svg` need viewport and viewBox handling, and external
references are not resolved. The existing box walk maps corners of a bounding
rectangle rather than recomputing transformed path extrema, so rotations of
curves may be conservative. Text geometry, stroke/options and CTM remain
separate open work as in Phase 433.
