# Phase 502: when a symbol draws no rectangle

## What was wrong

Phase 481 left a measured mismatch: a `<use>` with `width="0"` referencing a
symbol with no viewBox and percentage geometry gave `5,9,0,4`; Chromium gave
`0,0,0,0`. The symbol's rectangle had zero width, yet its degenerate box was
included and translated. A second, nondegenerate child showed this was not
just the empty-box answer: the zero-width rectangle stretched the union.
Negative use dimensions were also passed as negative percentage bases instead
of being clamped to zero. The wider differential found 175 mismatches in the
462-case baseline, not one.

## What changed

The bounding-box walk carries the fact that it is inside an instanced symbol,
including through groups and nested use references. In that context a rect with
a zero side contributes nothing to its container, while direct `rect.getBBox()`
keeps the zero-width result from Phase 433. Negative use viewport dimensions
are clamped when used as percentage bases. A zero-at-origin symbol source that
remains a zero-at-origin box after viewBox mapping is treated as empty before
applying the use offset, matching the measured Chromium results for point paths
and lines; a non-origin point that maps to the origin is retained.

## Verification

`python3 tools/conformance/verify_svg_symbol_degenerate.py` runs **466** attached
Chromium comparisons: **466/466** match, including axes, zero and negative
lengths, percentages, viewBox and no-viewBox, degenerate rectangles, point
paths and lines, transformed and nested groups, multiple children, and live
mutations. On the original tree its first 462 cases matched **287/462**; the
four mutation cases were added after that baseline. The browser installation is
the pinned one documented in Phase 433. The existing `verify_svg_bbox.py`
improves **47/49 to 48/49**, the remaining difference being the pre-existing
`defs.getBBox` interface mismatch. Reverting the implementation makes
`tests/suites/502-degenerate-symbol-instance-boxes.cpp` fail at its first box
assertion; restoring it passes. Three narrower mutations also fail the suite:
removing the negative viewport clamp, dropping the nested-use context, and
misclassifying a non-origin point mapped to the origin. `make test` passed
**536/536**, and the deep-page run passed **1332/1332**. The normal sanitizer
harness compile exceeded this sandbox's 3.8 GiB; with the test harness built
using `TEST_OPT="-O0 -g1"`, its new suite passes under ASan. The ASan engine
also passes the 466-case Chromium differential. Full sanitizer CI is still
required; these focused checks do not stand in for it.

## Still open

Referenced `<svg>` viewports and external references, text, stroke options,
and exact transformed curve extrema remain open as recorded in Phase 481. The
all-zero point handling reflects measured Chromium behavior, not a claim that
other browser engines or every SVG shape implement the same empty-box rule.
The 466 cases are a regression boundary, not exhaustive SVG conformance.
