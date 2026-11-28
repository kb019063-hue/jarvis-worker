# Finding 525: the viewport was never measured

An attached `<use>` referencing an SVG viewport containing a 3×4 rectangle
returned `0,0,0,0`; Chromium returned `13,12,3,4` after target position,
viewBox fitting and use position. The bbox walker explicitly returned early
for this target type. Phase 525 walks its instance viewport instead; its
1254-case browser differential and the remaining paint defect are documented
in [the phase](../phases/525-use-viewport-boxes.md).
