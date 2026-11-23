# Finding 211 — pixels could not be compared across builds

Phase 207. Status: **general comparison available; conformance gaps remain**.

## 1. What was wrong

The render reftests measure specified pixel assertions, but a general tool to
compare complete screenshots between two builds was absent. A change outside
an asserted region could therefore escape that measurement.

## 2. What changed

`make diff-render` compares every RGBA pixel for the default render suite,
another compatible suite, or a single HTML page. It uses the independent PNG
reader, reports changed counts, inclusive bounds and first values, treats
errors as failures and defaults to exact comparison. Known-failure pages are
not exempt. It does not judge whether either build is correct.

## 3. Evidence

`make diff-render-self-test` passes 11 tests. Comparing `build/litebird` with
itself yields 0 differences and 0 errors on all 48 primary and 41 body-origin
pages. An independently built one-column `fill_rect` mutation changes 41 of
48 primary pages with 0 errors and exit 1; the first page changes exactly 50
pixels at x=49, y=0–49. See `docs/phases/207-comparing-two-render-builds.md`
for commands and limitations.

## 4. What remains wrong

A matching pair of builds can share a rendering defect, and a visual change
can be intended. Existing known failures and unmeasured features remain open;
this comparator is a regression detector, not a replacement for independent
reference assertions or cross-browser conformance tests.
