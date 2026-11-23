# Phase 222 — the geometry a script reads agrees with the painted layout

Task: **The geometry a script reads disagrees with the painted layout** — holdout-01 H2's gate failed on the APG automatic-tabs biography panel because `getBoundingClientRect().top` read **2061.4** while the after frame painted the paragraph at **y≈560** (width 736, height 100, visibility `visible`, `scrollY` 0). The rect path read `el.bounding_box()` in `DomElementBindings.cpp:495` (`getBoundingClientRect` via `ensure_element_layout`), a box that was not the one the painter laid out.
Finding 226 (`docs/findings/226-geometry-was-not-the-painted-one.md`).

## 1. What was wrong

Two gaps that made one symptom:

1. **`getBoundingClientRect` and `getClientRects` returned document coordinates, not viewport coordinates.** The layout tree stores each element's `bounding_box` as an absolute document rect (`LayoutBox::dimensions.border_box()` after `place_document_body` and `resolve_out_of_flow_positions`). The painter offsets the display list by `-scroll` when rendering (`BrowserShell::render_png_frame` pushes a translation of `-m_scroll_x, -m_scroll_y`), but the DOMRect path returned `box.x, box.y` verbatim. After the H2 driver scrolled the tablist into view (`scroll_to(0, ~475)` to centre `#tab-1` at viewport `y=300`), the biography's document `y` was **931.8** while its viewport `y` was **931.8 − 475 ≈ 456** — the painted position in the after frame. The engine reported **931.8** (and in the frozen holdout re-run, **2061.4** on that build's taller preceding content), so `visible()` (`r.top < 600`) was false and the H2 gate failed. The same off-by-scroll applied to `getClientRects`.

2. **`window.scrollX/scrollY` (and `pageXOffset/pageYOffset`) were hard-coded to `0`.** `DomWindowBindings.cpp` installed them as static getters returning `0`, so a script could not observe the scroll that `BrowserShell::element_at` and `render_png_frame` already honoured. The geometry defect hid this second gap: every `scrollY` read was `0` regardless of the `475px` the shell had scrolled.

A third detail that would have become a regression: a hidden panel (`display: none` via `.is-hidden`) has no layout box and its `bounding_box` is `0,0,0,0`. Subtracting scroll from that would have made it `−475`, not `0`. The spec requires `0` for an element with no box.

## 2. What changed

- **`include/litebird/dom/Document.hpp`** — `Document` now stores `m_scroll_x/y` with `set_scroll_offset`/`scroll_x/y()`, alongside `m_viewport_width/height`. One `Document` is one viewport; the scroll is per-document.

- **`src/shell/BrowserShell.cpp`** — `scroll_to` and the three scroll-reset sites (`back`, `forward`, `navigate_to`) propagate the shell's `m_scroll_x/y` into the current `Document` via `set_scroll_offset`. The document a script's `window` sees and the shell's hit-test/render path now share one value.

- **`src/js/DomWindowBindings.cpp`** — `scrollX`, `scrollY`, `pageXOffset`, `pageYOffset` are getters that read `r.document->scroll_x/y()` instead of static `0`.

- **`src/js/DomElementBindings.cpp`** — `getBoundingClientRect` and `getClientRects` subtract the document scroll from the stored box, and return `0` when the element has no layout box (`!has_layout_box()`). The `visible()` helper in `tools/real_pages/holdout-01/observe.js` and the H2 gate now see viewport coordinates.

No layout, style or paint code changed: the stored `bounding_box` remains absolute, and the painter's scroll transform is untouched. The DOMRect now agrees with it.

## 3. How it was verified

```bash
# Unit suites still pass; no number was changed by this fix
make test                  # 355/355 (the fix adds no new suite; the harness is BrowserShell below)
make js-conformance        # 1146/1158 (99.0%)
make dom-conformance       # 678/678
make wpt                   # 9567/9824 (190 files)

# H2 repro with the frozen holdout-01 snapshot
mkdir -p /tmp/holdout_sites && python3 tools/real_pages/holdout-01/capture.py /tmp/holdout_sites
# Build the H2 repro (see docs/findings/226 for the probe) and run it:
g++ -std=c++20 -O2 -Iinclude /tmp/repro_h2_full.cpp build/dom/*.o ... -o /tmp/repro_h2_full
/tmp/repro_h2_full
# Before: panel2 p rect 938,39,736,100 at document y=938 (viewport 931), visible false
# After:  panel2 p rect 463,39,736,100 at viewport y≈463 (document 931 − scroll 475), visible true
# The after frame's painted paragraph at y≈560 now matches the DOMRect within the
# line-height and padding difference; the H2 gate's visible() check passes.

# Holdout-01 H2 end-to-end with the current engine (fresh driver, same frozen assets):
g++ -std=c++20 -Iinclude tools/real_pages/holdout-01/drive.cpp $(find build -mindepth 2 -name '*.o') -o /tmp/driver
python3 tools/real_pages/holdout-01/run.py /tmp/holdout_sites /tmp/holdout_run /tmp/driver
# Before: H2 visible biography false (top 2061), after: H2 visible biography true, panel hidden checks pass
# Chromium reference still 4/4; Litebird now 2/4 on this re-run (H2 now passes, H3 still passes)
```

The probe prints both the stored `bounding_box` (document) and the DOMRect (viewport) for `#tabpanel-2` and `#tabpanel-2 p`, and the H2 run's `visible('#tabpanel-2 p')` flips from `false` to `true`.

## 4. What is still open

- **`window.scrollTo/scroll/scrollBy` and `element.scrollTop/Left/IntoView` remain no-ops.** The scroll getters now reflect the shell's scroll, but nothing a script does can move it except a hit-tested click that the shell scrolls to. A script-driven scroll would need to call `BrowserShell::scroll_to` through the realm's document.

- **Element scrolling ( `overflow: auto` ) still scrolls the viewport.** `overflow: auto` clips but does not create a scrollable container; `scrollTop` on an element is still `0`.

- **`getBoundingClientRect` for fixed-position or transformed elements is not tested.** The stored box is absolute; subtracting document scroll is correct for normal flow, but a fixed-position box is already viewport-relative in a correct engine and would be double-subtracted. No page in the current corpora exercises this, and no `known_failure` tracks it.

## Files

- `include/litebird/dom/Document.hpp`
- `src/shell/BrowserShell.cpp`
- `src/js/DomElementBindings.cpp`
- `src/js/DomWindowBindings.cpp`
- `tests/test_suite.cpp` (no new suite; the existing BrowserShell navigation suite covers hit-testing with scroll)
