# Roadmap: what Litebird still needs to reach a modern engine

## What this file is — and what it is not

This is the arc from where Litebird is today to the practical capabilities of a
Chromium-class browser engine, listed in **priority (dependency) order**. It is
the "why" behind the smaller rows in `AGENTS.md` → **Open tasks**.

**Priority, not policy.** No item here is a command, and nothing in this file
makes any row compulsory. Agents pick whatever they like from either file; a
claim is a branch and a pull request, same as any other work, and the merge
rules are the same there as everywhere (CI green, merge commit). What priority
means is only this: phases lower in the list tend to depend on the ones above
them, and a claim higher up tends to unblock the items below it. If you prefer
a row from Open tasks over anything here, take that row.

- `AGENTS.md` → **Open tasks** is the atomic, claim-now queue (one file, one
  bug, one PR). This file is the phase-scale map of the same work.
- If the two ever disagree about what is open, **the task table wins** — it is
  what CI and the audit read. Fix this file in the same PR.
- Sizes are rough: **S** a few days, **M** a week or two, **L** a multi-week
  phase, **XL** an epic that will decompose into several PRs.
- Adopting a library is a legitimate part of a phase (HarfBuzz-class shaping, a
  TLS stack, libav for media) — say so in the PR; do not hand-roll where the
  standard ecosystem already solved it.
- Verification is not optional. Every phase keeps the existing scoreboards
  green and adds its own oracle (a corpus, a benchmark, a differential against a
  reference implementation) — or states plainly why it cannot.

## Where Litebird stands today (the honest baseline)

WPT-grade HTML/DOM/XML parsing for the covered corpora; JavaScript interpreted
on a tree-walking evaluator (plus a half-finished bytecode VM), 840/845 on its
recorded corpus; layout with an 8×8 bitmap font; a terminal/ANSI renderer;
`http://` only — no TLS at all; XHR for `data:` URLs only; no typed arrays,
`Proxy`, BigInt or ES modules; pixels are **unmeasured** by any scoreboard.
The long honest form of the list: `ARCHITECTURE.md` → *Known gaps*.

## Wave 1 — The browser can actually see (rendering & text)

| # | Phase | Size |
| :--- | :--- | :--- |
| 1 | **Fonts and shaping** — system font loading, `@font-face` web fonts, real metrics, complex-script shaping (adopt HarfBuzz-class shaping), bidi/RTL text, OpenType basics. The single biggest visual gap. | XL |
| 2 | **Rasterizer upgrade** — anti-aliased, scaled glyphs replace the 8×8 bitmap; image resampling; transforms at raster time. | L |
| 3 | **Windowed shell** — a real window with keyboard/mouse/touch, focus, scrolling, cursor; terminal renderer kept as a fallback. | L |
| 4 | **Image codecs** — PNG, GIF (animated), WebP (JPEG optional, AVIF later). Today: BMP/PPM only. Oracle: WPT `image/`. | M–L |
| 5 | **Compositor / tiling + damage tracking** — tile raster, repaint regions, scroll compositing; GPU path (Skia/ANGLE) or CPU tiles first. | L–XL |
| 6 | **Canvas 2D completion** — path/hit-test gaps vs WPT `canvas/`. The text gap (`fillText`/`strokeText`/`measureText` and the `font`/`textAlign`/`textBaseline` state) is done — Phase 46. | M |

## Wave 2 — JavaScript is a platform

| # | Phase | Size |
| :--- | :--- | :--- |
| 7 | **Finish the bytecode VM** (`VM.cpp`, `BytecodeCompiler.cpp` exist) as the single engine; tree-walker stays as a differential oracle. | L |
| 8 | **Language tier 1** — `ArrayBuffer` + typed arrays + `DataView` (unblocks everything), BigInt, complete RegExp flags (`u`, `v`, `d`), and array-hole semantics. (The recursive matcher of finding 26 was replaced by an iterative heap-stack bytecode VM in Phase 37.) | M |
| 9 | **Language tier 2** — Proxy, generators/async generators at the top level, ES modules + import maps + dynamic `import()`. | M |
| 10 | **Unicode / `Intl`** — ICU-grade data (the IDNA approach, generalized): full case mapping, `normalize()`, property escapes, collation, `DateTimeFormat`/`NumberFormat`/`Segmenter`. | L |
| 11 | **Automatic GC + leak audit** — collection only runs on `gc()` today; incremental sizing, CI memory benchmarks. | M–L |

## Wave 3 — Real pages (layout & CSS)

| # | Phase | Size |
| :--- | :--- | :--- |
| 12 | **Two known-wrong layout bugs** — block-in-inline is split correctly (Phase 36); sized floats are placed by a band-edge sweep, landing on the exact bottom edge of the float in the way (10,000 sized floats in seconds; nothing escapes the box). **Done.** | M |
| 12a | **Sized-float performance** — index nonempty bands for placement without re-sorting every prior band per float; preserve the already-correct exact-edge placement (Phase 48). **Done**; bands spanning many distinct intervals still require updating those intervals. | S |
| 13 | **Flexbox/Grid gap closure** vs WPT `css/flex` and `css/grid`. | M–L |
| 14 | **Selectors level 4** — `:has()`, `:focus-visible`, `@container`, CSS nesting, `::part`. (`:is()`, `:where()`, `:not()` selector lists, combinators `>`/`+`/`~`, `@layer`, and `@supports` landed in Phase 37.) | M–L |
| 15 | **Container queries** — `@container` evaluation inside the cascade. (Media Queries Level 4 range syntax and boolean operators landed in Phase 37.) | S–M |
| 16 | **CSS Color 4/5** — `oklch`/`lab`, `color-mix()`, relative-colour syntax. **Done (Phase 50)**: unified `CssColor` engine covering all 15 CSS Color 4 spaces (`srgb`, `srgb-linear`, `display-p3`, `display-p3-linear`, `a98-rgb`, `prophoto-rgb`, `rec2020`, `xyz-d50`, `xyz-d65`, `lab`, `lch`, `oklab`, `oklch`, `hsl`, `hwb`), exact §17 matrices & Bradford D65↔D50 adaptation, §13.2 OKLCH chroma-reduction gamut mapping, `color-mix()` (1..N args, all hue modifiers, missing-component carry-forward), relative color syntax (`from ...`), `light-dark()` + `color-scheme` inheritance, and CSS Values 4 math (`calc`/`min`/`max`/`clamp`/trig/constants) wired through `StyleResolver`, `DomStyleBindings`, and `SvgRenderer` (`make verify-color`, `make fuzz-color`, 4,860/4,860 upstream WPT `color-computed-*.html`). Open: wide-gamut (>8-bit sRGB) raster buffer, `@color-profile`, `contrast-color()`. | M |
| 17 | **Table completeness** — `border-collapse` widest-wins (the wider border of a shared edge wins, its colour included; style and positional priorities are not modelled — this engine has a single solid border style — and row borders are not painted), `table-layout: fixed` (the first row decides; later rows cannot resize a column), `caption-side`, `empty-cells` (inherited, separate borders only). **Done.** | S–M |
| 18 | **Multicol + fragmentation** — columns, `break-inside`. | M–L |
| 19 | **Scrolling & positioning corners** — `overflow: auto` actually scrolls, `position: sticky` robustness. | M |
| 20 | **Cascade arithmetic** — percentage `font-size` (against the parent's computed value) and `line-height` as a real property (`normal`, bare number, percentage of own font size, absolute length) across style, inheritance and inline layout. **Done** (percentage `border-radius` deliberately deferred: `ComputedStyle` carries a single scalar radius end to end, so a percentage radius — and two-axis elliptical radii — cannot be expressed without a bigger change; no pixel oracle measures them today). | S |

## Wave 4 — The network is real

| # | Phase | Size |
| :--- | :--- | :--- |
| 21 | **HTTP/1.1 complete** — redirects, `Set-Cookie` → jar (exists — wire it), auth, chunked, gzip/br decode. | M |
| 22 | **HTTPS** — TLS with certificate validation (adopt a stack or platform APIs). This phase *is* the project, not a detail. | L–XL |
| 23 | **Fetch/XHR/WebSocket policy** — same-origin + CORS incl. preflight and credentials; mixed-content blocking; unlocks `https:` (two open tasks land here). | M |
| 24 | **Encoding Standard complete** — all WHATWG encodings (`Shift_JIS`, `EUC-*`, …), `TextEncoder`/`TextDecoder`. **Decoding done (Phase 47)**: 40 encodings and 228 labels from `whatwg/encoding`, `<meta charset>` prescan, `document.characterSet`, XML `[81] EncName`, `@charset`, `data:` URLs, XHR and subresources (WPT 7,968/8,009 across 103 files, the XML suite 1,718/1,718). Open: `TextDecoder`/`TextEncoder` in script, encoders, and streaming. | M |
| 25 | **Frames/iframes with documents** — `iframe.contentDocument`, `srcdoc`/`sandbox`, XML/SVG-as-a-page (`litebird feed.xml` / `.xhtml` script execution). **Done (Phase 42)**: nested browsing contexts (`HTMLIFrameElement`, per-frame `JsInterpreter`/`Realm`/`WindowProxy`, `src`/`srcdoc`/`sandbox`, cross-origin `WindowProxy` access checks, `postMessage`, subdocument layout/rasterization, and `.xhtml` WPT execution; all 495 `frames` WPT failures resolved, raising WPT to 5,421/5,463 across 87 files). | M–L |
| 26 | **External scripts/stylesheets + module fetching** over the real network. | M |
| 27 | **Form submission** — GET/POST navigation, file upload, `multipart/form-data`, downloads. | M |

## Wave 5 — DOM everywhere

| # | Phase | Size |
| :--- | :--- | :--- |
| 28 | **Processing instructions in the HTML parser** (`<?target data>`) + migrate html5lib→WPT corpus. | M |
| 29 | ~~**`Attr` is a `Node`**; one qualified name in two namespaces; `NamedNodeMap`/`HTMLCollection` own-props~~ (Phase 51). | M |
| 30 | **Shadow DOM complete** — slots, `::part`, event retargeting, constructable stylesheets. | M–L |
| 31 | **Selection + `contenteditable` stack** — `Selection` API, ranges, caret, input events. The authoring gap nobody notices until they try to edit rich text. | L–XL |
| 32 | **SVG completeness + MathML** — text/gradients/viewBox transforms in render; MathML parse-only. | M |

## Wave 6 — What makes Chromium Chromium

| # | Phase | Size |
| :--- | :--- | :--- |
| 33 | **Security story** — CSP, `Referrer-Policy`, `X-Content-Type-Options`, mixed content, iframe `sandbox`; then **multi-process isolation / renderer sandbox** (Linux seccomp at minimum). | XL |
| 34 | **Accessibility** — a11y tree, ARIA reflection, focus order, roles; screen-reader smoke tests. Today, approximately zero. | L |
| 35 | **Performance engineering** — incremental layout/paint, HTTP cache (ETag/`Cache-Control`), parsed-stylesheet and image caches; a frozen benchmark set in CI. | L |
| 36 | **Media** — `<video>/<audio>` demux + decode (adopt a libav-class stack), a/v sync, controls. | XL |

## Wave 7 — Measurement & interop (the house style, extended)

| # | Phase | Size |
| :--- | :--- | :--- |
| 37 | **WPT everywhere** — extend the vendored runner beyond `dom/nodes`+`domparsing` to `html/`, `url/`, `fetch/`, `encoding/`, `css-*`, same "listed failures" protocol. **`encoding/` landed in Phase 47** (16 files, 2,543 subtests). | M |
| 38 | **Render conformance** — reftest / screenshot-diff harness against a reference engine; today *pixels* are unmeasured. | L |
| 39 | **Interop dashboard + CI benches** — extend `bench-css`: JS, layout, memory, network fixtures. | M |
| 40 | **Fuzzing expansion** — layout/render fuzzers (parser/XML ones exist) + a mock-server network fuzzer. | M |

## Deliberate non-goals

Service Workers, WebRTC, WebAssembly, WebGPU, DRM/EME, sync/accounts — each is
a project of its own, not a phase. If one ever becomes a goal, it gets its own
wave and its own rows in `AGENTS.md`.

## Rough order of attack

Waves 1, 2 and 4 make a real page *look* and *work* right; waves 3 and 5 make it
*complete*; 6 and 7 make it *trustworthy and honest about speed*. Dependencies
run mostly downward: shaping before text rendering, TLS before HTTPS fetch
policy, the VM before JS performance work, the network before external scripts
and modules, frames before their WPT rows go green. Within a wave, pick freely —
the rows differ in size, not in merit.

Sources of truth: `ARCHITECTURE.md` → *Known gaps*, Phase 27 §12, Phase 28 §10,
Phase 23 §10; `AGENTS.md` → *Open tasks*; `AUDIT.md`.
