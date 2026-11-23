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

## Results

Results are reported in a later section once both runs have been executed and frame-reviewed — never from the traces alone.
