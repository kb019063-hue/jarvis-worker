# Phase 525: the SVG viewport behind a use

## What was wrong and what changed

The bbox walk returned an empty box whenever a `<use>` referenced an `<svg>`.
It now resolves the target and instance dimensions and the target's position,
uses the viewBox and preserveAspectRatio mapping, and walks nested SVG
viewports in the instance. The walk retains the cycle and depth guards and
continues to ignore the use element's own transform when queried directly.
SVG viewport transforms inside the instance are ignored to match the measured
Chromium behavior. This work changes bounding boxes, not painting.

## Verification

`python3 tools/conformance/verify_svg_use_viewport.py` compares **1254/1254**
attached-element results to the pinned Chromium setup of Phase 433; the
initial 280-case matrix matched **62/280**. Cases cover SVG and use sizes,
percentages, viewBox fitting, zero and negative dimensions, descendants,
nested viewports and transforms, and six live mutations. The dedicated unit
suite covers direct and parent boxes, sizing, aspect ratio, percentages,
degenerate children, nested viewports, transforms and a cycle. The older SVG
symbol differential remains **466/466**, and the existing bbox oracle remains
**48/49** (the unrelated `defs.getBBox` interface mismatch).

## Still open

The painter does not use the `<use>` dimensions for an SVG target. In
`python3 tools/conformance/verify_svg_use_viewport_paint.py`, Chromium paints
40×20 green while Litebird paints 20×10; only **2/6** sampled pixels match.
This reproducer intentionally returns failure until that separate defect is
fixed. External references, text, stroke/options, exact transformed curve
extrema and non-instanced nested SVG viewport boxes remain outside this phase.
The oracle is a measured Chromium boundary, not a complete SVG specification.

Serial local checks: `make test` **560/560**, conformance, JS, DOM, URL,
XML, WPT and tree differential passed their recorded floors; `make diff-xml`
passed with lxml in a local `PYTHONPATH`, and `make render-conformance`
passed. An earlier parallel test attempt was killed under memory contention,
and its interrupted deep run is not counted. The deep-page and sanitizer
checks are being rerun serially before this draft is finished.
Serial `make deep` subsequently passed **1332/1332** runs over 465 pages.
The standard `make asan` engine build succeeded, but the normal sanitizer test
harness compiler reached 3.8 GiB and was stopped under severe memory pressure;
the full sanitizer run has not passed. The ASan engine independently matched
**1254/1254** of the viewport box differential. CI's full sanitizer job remains
an independent required gate, not a substitute for claiming a local pass.

`make fuzz` passed, including all 2400 media mutations and 2000 Brotli
mutations. With `TEST_OPT="-O0 -g1"` the sanitizer harness compiled and the
Phase 525 suite passed; this focused run is not a full sanitizer pass.
