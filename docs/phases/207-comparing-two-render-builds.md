# Phase 207 — comparing two render builds

Task row: pixels are measured, but the two-build comparison was limited to
body-origin pages. Finding 211.

## 1. What was missing

The pixel oracle tests selected assertions against an independent expected
answer. It could not generally compare every pixel produced by two builds
on its render suites or on an arbitrary page. Changes outside the asserted
regions could go unnoticed even when they changed the screenshot.

## 2. What changed

`tools/conformance/diff_render.py` renders the same input with two explicitly
chosen executables and decodes both PNGs with `render_check.py`'s independent
reader. It reports changed pixel counts, an inclusive bounding box and the
first differing RGBA values. Exact comparison is the default; `--tolerance N`
ignores channel differences of at most N, including alpha. Known-failure
pages are compared normally; assertions are not evaluated. A difference is
not necessarily a regression and identical screenshots do not establish
conformance. Use the existing `make render-conformance` for assertions.

```
make diff-render BEFORE=/path/to/old/litebird AFTER=build/litebird
make diff-render BEFORE=/path/to/old/litebird AFTER=build/litebird DIFF_RENDER_ARGS='--suite tests/render/body-origin.json --filter body_'
python3 tools/conformance/diff_render.py --before /path/to/old/litebird --after build/litebird --page examples/hello.html --width 800 --height 600 --json
```

Suite entries use their `width` and `height` or 800 by 600 by default; single
pages use the supplied viewport or the same default. Single-page paths are
retained so relative resources can resolve; inline suite HTML is written in a
temporary directory. The JSON output contains per-page results and summary
counts. Exit status is 0 for identical pixels, 1 for differences or rendering
errors, and 2 for invalid input. Timeouts, failed renderers, missing or invalid
PNGs, and wrong-sized images are errors, not matches. Temporary output is
cleared before each render so an old image cannot stand in for a missing one.
Both builds should use comparable runtime assets, fonts and environment.

## 3. Verification

```
make diff-render-self-test
# 11 tests pass, including alpha, tolerance, missing/corrupt/stale output,
# failed and timed-out renderers, dimensions, filtering and a single page.
make diff-render BEFORE=build/litebird AFTER=build/litebird
# Pixel diff: 48 pages, 0 differ, 0 errors (tolerance 0)
make diff-render BEFORE=build/litebird AFTER=build/litebird DIFF_RENDER_ARGS='--suite tests/render/body-origin.json'
# Pixel diff: 41 pages, 0 differ, 0 errors (tolerance 0)
make render-conformance
# 47/48 primary pages match, one known; 35/41 body-origin pages match,
# six known; Canvas toDataURL 45016/45016 checks agree.
```

A real negative control built a separate copy with the right edge of
`BitmapRenderer::fill_rect` changed from `rx + rw` to `rx + rw - 1`.
Comparing the unmodified build with that mutant over the primary suite
reported **41/48 pages different, 0 errors, exit 1**. The first page changed
50 pixels in the inclusive bounds `(49,0)`–`(49,49)`; its first change was
red `[255,0,0,255]` to white `[255,255,255,255]`. The mutation was not
applied to the tracked tree. CI runs the self-tests and compares a real build
against itself on the inline pages.

## 4. What remains open

This is not an independent browser oracle: two equally wrong builds can
match, and a change can be an improvement. Font availability and other
environmental differences can change pixels. Existing unmeasured layout,
canvas and glyph-shape gaps, including the recorded nowrap defect, remain;
no engine behavior or reference expectation was changed.
