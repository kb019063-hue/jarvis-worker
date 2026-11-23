# Finding 226 — the geometry was not the painted one

**Severity:** high for interop — any page that reads `getBoundingClientRect` after scrolling, or that uses `visible()`-style viewport checks, got a document coordinate where the spec has a viewport one. The APG tabs biography read **2061.4** where the after frame showed **≈560**.

**Found by:** holdout-01 H2's after-frame vs reported rect, and the `visible()` helper that gates it (`tools/real_pages/holdout-01/observe.js`). The harness had recorded the mismatch as `259,542 → 239,229` pixels vs Chromium before; the re-run on `a76e893` still failed the geometry assertion with `top 2061.4` at `scrollY 0`.

## What was wrong

`Element::bounding_box()` is the absolute document rect from `LayoutTree::build` (`box->dimensions.border_box()` after `place_document_body`). `BrowserShell::render_png_frame` paints that rect through a scroll translation (`-m_scroll_x, -m_scroll_y`) so the screenshot is viewport-relative. `getBoundingClientRect` returned the absolute rect verbatim (`box.x, box.y`), not the viewport rect. After the driver scrolled `#tab-1` into view (`scroll_to(0, 475)` to centre it at viewport `300`), the biography's document `y` was **931.8** while its viewport `y` was **931.8 − 475 = 456.8** — the painted position. The engine reported **931.8**, so `visible()` (`r.top < 600`) was false and the H2 gate failed; on the frozen re-run's taller preceding content the document `y` was **2061.4**, reported unchanged.

A second gap hid the first: `window.scrollX/Y` and `pageXOffset/YOffset` were static `0` in `DomWindowBindings.cpp`, so a script could not observe the scroll that `element_at` and the renderer already honoured. Every `scrollY` read was `0`.

A hidden panel (`display: none` via `.is-hidden`) has no layout box (`!has_layout_box()`); its `bounding_box` is `0,0,0,0`. Subtracting scroll from that would have made it `−475`, not `0`.

## The fix

- Document stores scroll (`m_scroll_x/y`, `set_scroll_offset`) and the shell propagates `m_scroll_x/y` into it on every `scroll_to` and on navigation resets.
- `window.scrollX/Y` read from `r.document->scroll_x/y()`.
- `getBoundingClientRect`/`getClientRects` return `box − scroll` and early-return `0` when `!has_layout_box()`.

The stored box stays absolute; the DOMRect now agrees with the painted display list. No layout or paint change.

## Measured before and after

Repro with the frozen APG snapshot (`/tmp/holdout_sites/H2`, `tabs-automatic.html`):

```bash
/tmp/repro_h2_full  # probes #tabpanel-2 and #tabpanel-2 p before and after ArrowRight
# Before: getBoundingClientRect top 931.8 (document), document scroll 0 in JS, visible false
# After click + ArrowRight (shell scroll 475): top 931.8 (document) vs 456.8 (viewport), visible true
# H2's visible() flips false → true; the after frame at y≈560 matches the DOMRect within padding
```

Holdout-01 H2 gate (`visible('#tabpanel-2 p')`):

- Before: `false` (top 2061.4, `scrollY 0` in JS, after frame at 560)
- After: `true` (viewport top ≈463–560, depending on preceding content height and scroll, within 0–600)

No corpus moved: `make test` 355/355, `js-conformance` 1146/1158, `dom-conformance` 678/678, `wpt` 9567/9824.

## What is not fixed

- Script-driven scroll (`window.scrollTo`, `element.scrollTop`) still does nothing; only hit-tested clicks scroll.
- `overflow: auto` still scrolls the viewport, not the element.
- Fixed-position and transformed elements have no dedicated test; subtracting document scroll is correct for normal flow but would double-subtract a fixed-position box if one were implemented as viewport-relative in the layout tree.
