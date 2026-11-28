# Phase 481: a symbol instance has a box

## What was wrong

Phase 447 made `getBBox()` follow `<use>` references to shapes, groups and
other uses, but excluded symbols. For a symbol with `viewBox="0 0 10 10"`
and a rect at (1,2) of size 3 by 4, a use at (5,7) sized 20 by 10
answered `0,0,0,0`; Chromium answers `11,9,3,4` because the default
`xMidYMid meet` adds five units of horizontal padding.

## What changed

The box walk enters a symbol only when it is the target of a `<use>`, retaining
cycle detection and excluding uninstanced symbol definitions. It resolves
percentages inside a valid viewBox in that viewBox's coordinate space and maps
the resulting box using the painter's viewBox and preserveAspectRatio matrix,
then adds the use's x/y. With zero or negative viewport dimensions Chromium
reports the unscaled geometry; percentages still use the valid viewBox.
The symbol's own transform is ignored, as in Chromium and the painter.

## Verification

`python3 tools/conformance/verify_svg_bbox.py` compares against pinned
Chromium: **47/49** match. One mismatch is the previously documented `defs.getBBox`
interface difference; the other is a no-viewBox symbol with percentage
geometry and a zero-width use: Chromium answers `0,0,0,0`, this engine
`5,9,0,4`. The expanded baseline was 33/45 before the fix. The browser setup
is in Phase 433. `make test` passed **524/524** before the last zero-width
case was added; the new suite passed separately after that addition. Reverting
`src/render/SvgRenderer.cpp` makes its first box assertion fail. The fix was
restored and rebuilt afterward.

## Still open

Referenced `<svg>` viewports, external references, text, stroke options and
exact transformed curve extrema remain open as in Phase 447. Boxes of rotated
paths are conservative because the walk transforms corners of a bounding box,
not the path itself. The no-viewBox zero-width percentage case above remains open. Invalid and
unusual symbol dimensions, clipping and CSS visibility need broader
cross-browser measurement; the result here is not a
claim of complete SVG layout or SVG 2 conformance.
