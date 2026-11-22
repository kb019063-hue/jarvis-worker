# Finding 197 — inline formatting was unmeasured

Phase 193. Status: **partially measured, one engine defect open**.

## 1. What was wrong

The original pixel reftests checked that a single glyph painted, but had no
explicit `<br>` or automatic inline wrapping case. They therefore did not
assert the visible placement of successive lines of inline replaced elements.

## 2. What changed

Three `tests/render/reftests.json` pages use 20 by 20 red inline images in a
45-pixel container: a forced `<br>`, a normal soft wrap, and the same images
with `white-space: nowrap`. They assert eleven rectangles, with no dependency
on host font glyphs. The PNG reader is separate from the engine's writer.

## 3. Evidence

`make render-conformance` reports 47/48 original pages passing and the new
nowrap page as a recorded failure; the separate body-origin corpus remains
34/41 with seven recorded failures. `tools/conformance/verify_body_pixels.cjs`
compared the three pages with HeadlessChrome/153.0.8010.0: **11/11** pixel
assertions match the independent browser. Negative controls made each passing
case fail when its expected colour was changed, and exposed both nowrap
assertion failures after removing its `known_failure` marker. Commands and
context are in `docs/phases/193-inline-formatting-in-the-pixel-oracle.md`.

## 4. What remains wrong

Litebird wraps the third image to `(0,20)` when the span's `white-space` is
`nowrap`; Chromium leaves it at `(40,0)`, overflowing the container. This
change records but does not repair that behavior. A broader set of inline
layout, text and WPT reference cases is still needed.
