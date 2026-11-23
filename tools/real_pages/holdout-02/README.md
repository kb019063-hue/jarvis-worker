# Holdout 02 · Four more unfamiliar browsing tasks

**First exposure only.** The four tasks in this holdout were chosen blind, before either browser ran: a declarative popover, a web worker's `postMessage` loop, `insertAdjacentElement`, and a URL-parameter table filled by a sorting pass after an ordinary link click. The engine measured Litebird, built from the commit named below, is fresh at measurement time; the reference is a fresh Chromium run the same day. Neither figure is a measure of general web compatibility. Do not add this denominator to v1's or to holdout-01's.

Both runs use the same saved local assets, the same action sequence and the same gates. The Chromium reference was executed **first**, before the native engine ran; no result existed when this manifest and this harness were committed (see the freeze commit). No page was substituted, no engine change was made before results, and the holdout's numbers are regression/improvement measurements against a now-exposed set — not an unseen sample.

## Attribution and license

All four pages come from [`mdn/dom-examples`](https://github.com/mdn/dom-examples), commit `a2c57197c219bd1c87d48aa901f4c2a24b249658`, licensed **CC0-1.0** (the repository declares `"license": "CC0-1.0"`). The four example folders are carried unmodified: `popover-api/basic-declarative/`, `web-workers/simple-web-worker/`, `insert-adjacent/`, `url-params/`. `capture.py` downloads the pinned tarball from `codeload.github.com` and copies those four directories; it never fetches anything else, and none of the examples is built or executed at capture time.

| Task | Page | What it exercises |
|---|---|---|
| H1 · `popover-api/basic-declarative/index.html` | Declarative popover | `popover` attribute, `popovertarget` invoker buttons, popover visibility |
| H2 · `web-workers/simple-web-worker/index.html` | Web Worker multiply | worker spawn, `postMessage` round-trip on `change` events, `.result` DOM write |
| H3 · `insert-adjacent/insertAdjacentElement.html` | `insertAdjacentElement()` | click hit-test, inline-style border mutation, `insertAdjacentElement("afterend")` |
| H4 · `url-params/index.html` | `URLSearchParams` sorting demo | URL parsing, query navigation via one plain `<a href>` click |

## Protocol

Fresh context/process per task; fresh Chromium reference (`./oracle.cjs`) executed before the native run; saved local assets only; no DOM mutation or handler calls by the driver — every activation is a real click, key or type; viewport 800×600; 45 s wall budget on the native side; sockets denied (seccomp) in the native driver. Reads are assertions on computed/observed state; the frames are the separate visual record (RGBA equality on the post-action pair, zero tolerance, `results/*--lite-after.png` vs `results/*-ref-after.png` vs the committed reference frames).

## What is frozen and when

The manifest names the upstream revision, the four paths, the viewport, the timeout and the per-task gates. It was written before either browser ran, on the same day the harness was committed.

## Results (Vellum, 2026-11-23)

Measured on the engine built from commit `5df54f88` of this branch (`sha256` of `build/litebird` `47850f5256938d45…`, which includes the `#240` landing: OffscreenCanvas, terminal text safety, docs). Sites captured from `codeload.github.com` at the pinned revision; `.revision` checked before each task. Reference: fresh Chromium (**HeadlessChrome/131.0.6778.0**, `@sparticuz/chromium@131.0.1`), one fresh context per task, executed **before** the native engine ran. Native: fresh process per task, sockets denied (seccomp), real clicks/keys/types, read-only JS observations. Neither denominator pools anything with v1's or holdout-01's.

**Chromium: 4/4. Litebird: 1/4 — H3 only.**

| Task | Gate | Verdict |
|---|---|---|
| H1 · popover | not blocked, reads visible/hidden | fail on gate (popover renders as an always-visible div; the `[popover]` attribute has no effect and the invoker buttons do nothing; the trace records hits with no show/hide behavior) |
| H2 · worker multiply | initial reads match, interaction does not | fail on gate (after typing 6/7 and two Tab presses the worker never receives a message: `change` never fires — see "What this isolates" (2)); `window.Worker` **is** a function in this build |
| H3 · insertAdjacentElement | before/after boxes | **pass** (four boxes → five, first box keeps the aqua border after selection; the inserted box is visible in the frames) |
| H4 · url-params | navigation + table | blocked on an engine defect — see "What this isolates" (1); the trace never leaves the initial page, and the after frame is the full-screen "Navigation Error — Mapped origin resource not found" |

**Pixels.** Zero tolerance on 800×600 RGBA post-action pairs ([review.json](results/review.json); [frames](results/frames/) native, [oracle frames](results/oracle-frames/) Chromium; native traces [H1..H4](results/) and [litebird.json](results/litebird.json)):

| Task | before | after | note |
|---|---|---|---|
| H1 | 5,895 | 6,197 | end states differ (Chromium shows no popover; Litebird always paints "Popover content") |
| H2 | 51,940 | 52,796 | same visual family both sides — layout/typography/chrome; the action never happened |
| H3 | 35,170 | 42,036 | gate passed in both engines; the inserted box differs in colour (random) and the five divs paint as stacked blocks rather than inline-block; button chrome differs |
| H4 | 43,361 | 480,000 | after frame is the engine's full-screen navigation-error page; not comparable to the table render |

**What this isolates, two newly, both with root causes:**

1. **`ResourceLoader::resolve_url` drops the base document's last path segment for query-only references.** Probing it directly: from base `http://snapshot.test/url-params/index.html`, `?firstName=Ayse&food=rice%20pudding` resolves to `http://snapshot.test/url-params/?firstName=Ayse&food=rice%20pudding` — `index.html` is gone. The navigation then maps `/url-params/` to a directory, is not a regular file, returns 404 and the shell replaces the document with its error page. Every `<a href="?q=1">` on the web is affected. This is **not** H4's page being broken; it is a defect the holdout found. It is now [a row in `AGENTS.md`](../../AGENTS.md).
2. **`Tab` never calls `focus()` on the element it lands on.** `focus_next()` advances an index over `m_interactive_elements` (with `focus_prev()`/`focus_element()`) but never calls `Element::focus()`, never dispatches `focus`/`blur` — so `document.activeElement` stays the clicked input, `change` never fires on blur, and no task that depends on `change` completing can do so (H2's worker never got a message). Now a row in `AGENTS.md`.

**Also seen, not rows yet:**

- `[popover]` has no semantics at all — the UA stylesheet has no popover rules; the element paints as a plain in-flow div and `popovertarget` buttons are inert. The dialog UA-chrome row is the adjacent work (that row covers `dialog`, generic UA form-control and `select` rules; popover rules would ship with the same stylesheet pass).
- `insertAdjacentElement`'s insertion works and is hit-tested correctly (H3), but the five divs paint as **stacked blocks**, not `inline-block` in a row — the before/after frames show it both engines at the same 5-box count; `is_inline_block()` exists in layout, so the root cause is below this PR's scope of proof (inline formatting path). Noted, not explained here.
- H2's `change`-dependence: even with Tab fixed, `change` on blur would need to fire; the Enter path fires `change` (`handle_key("Enter")`), the blur path does not. Same row as (2) for now.

**Honesty notes.**

- The Chromium calibration caught an oracle error, not an engine error: the frozen gates expected the sorted table as `food` first; `'firstName' < 'food'`, so both engines sort firstName first. `steps.json`/`manifest.json` were corrected (commit `5df54f88`) **before any native run and before the reference was declared passing**; the full trace of that correction is in the commit.
- H3 passing its gate does not mean the visual is right — the inline-block stacking and chrome differences are both recorded above. Chromium's H3-after frame shows a horizontal row of boxes with a green (random-colored) second box; Litebird's shows a vertical stack with a mauve one.
- `results/frames/` and `results/oracle-frames/` hold the full before/after pairs. Pixel equality failed on all four in both states, as recorded — the delta numbers are not "scores".
- This holdout is now exposed: its results are a regression/improvement baseline, not an unseen sample. v1 and holdout-01 were not touched; no pooling, no denominators added.
