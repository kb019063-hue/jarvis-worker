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

WPT-grade HTML/DOM/XML parsing for the covered corpora (tree construction
1,973/1,973; `make wpt` 9,567/9,824 subtests across 190 files); bidirectional
text by UAX #9 (`make bidi-conformance` 861,948/861,948 of Unicode's own
suites), without `direction` or `unicode-bidi`; JavaScript
interpreted on a tree-walking evaluator (plus a half-finished bytecode VM),
1,143<!--metrics:js.pass/num-->/1,158<!--metrics:js.total/num--> on its recorded corpus and 31,220<!--metrics:t262.full.pass/num-->/47,967<!--metrics:t262.full.total/num--> of test262; TrueType
rasterization with an 8×8 bitmap fallback when no `.ttf` exists on the host;
a terminal/ANSI renderer; `http://` **and `https://`** (OpenSSL loaded at run
time, certificate and hostname verified); `fetch()` and `XMLHttpRequest` over
HTTP/HTTPS with CORS; `ws://` and `wss://` WebSockets (OpenSSL TLS verified,
Fetch `Origin` and mixed-content policy enforced); no typed arrays, `Proxy`, BigInt or ES
modules; pixels are **unmeasured** by any scoreboard.
The long honest form of the list: `ARCHITECTURE.md` → *Known gaps*.

*(This baseline was allowed to go stale once: it still said "no TLS at all"
and "XHR for `data:` URLs only" after both had landed. Re-read it against the
code before trusting it.)*

## What "usable" means — three tiers

The waves below say what a Chromium-class engine needs. They do not say which
of it stands between Litebird and something a person could actually browse
with. This does. **Tier 1 is the difference between an engine and a browser.**

### Tier 1 — a browser someone could use

| # | What | Row | Why it is here |
| :--- | :--- | :--- | :--- |
| 1 | **Font fallback, `font-family` selection, `@font-face`, `font-style`, `font-weight`, `kern`** | 1 | **Fallback done (Phase 64)**, **`font-family` selection done (Phase 67)** and **`@font-face` done (Phase 74):** a family list is resolved against the faces the host declares, per element, and a face is chosen and loaded for it; a page may also bring its own typeface, fetched from the `src` list the first time it is wanted. `pre { font-family: monospace }` now measures differently from a heading in the default face, which it did not before — and did not after Phase 67 either, because the style a text box is built with did not carry `font-family` (Phase 74 fixed that too). **`font-style: italic` done (Phase 78):** `em`, `i`, `cite`, `dfn`, `var` and `address` are italic; a family that has a slanted face is asked for it, and a family that has none — which on this host is all of them — is sheared 14°, as CSS asks. **`font-weight` done (Phase 87):** a face's weight and slant come from `OS/2` and `head`, not from the words in its subfamily name, and the weight a page asks for reaches the registry — it used to be flattened to `bold`/`not` before a face was chosen, so a family's 300 face could not be asked for. Matching follows CSS §5.2.1's order, `bolder`/`lighter` are relative to the parent. **`kern` done (Phase 91):** the font's own pair table is read and applied — 2,727 pairs in this host's DejaVu Sans, `AV` 6.4 px closer at 100 px, and `AA` 2.8 px *wider*, because the values are signed both ways — with measurement and painting applying the same adjustment to the same face. Still open: no WOFF/WOFF2, no `unicode-range`, no `oblique <angle>`, no small caps, no `font-stretch`, no variable fonts, no `ctx.fontKerning`, no `font-variant-ligatures` (bidi landed in Phase 109). **`font-kerning` done (Phase 97):** `none` now reaches the pen — carried from the computed style to the text box, to the `DrawText` command, to the glyph positions — and `auto`/`normal` kern, which is what `auto` means. Adding it exposed a cascade bug worth its own line: the `font` shorthand was applied in a pass of its own, ahead of the rest of the declaration block, so `font-family: monospace; font: 16px serif` came out monospace and `font-kerning: none; font: 16px serif` kept `none` (finding 103, fixed here). **`GDEF` & `GPOS` kerning and marks done (Phase 99, finding 105):** `GDEF` glyph classes / mark-attachment sets and `GPOS` `SinglePos`, `PairPos` (fmt 1/2), `MarkToBase`, `MarkToLigature`, `MarkToMark`, and `ExtensionPos` (Type 9) now position kerning pairs, zero monospaced combining-mark advances, and anchor/stack diacritics across layout, `FontRenderer`, and `Canvas2D`. |
| 2 | **Shaping** — complex scripts, contextual `GSUB`/`GPOS` | 1 | `kern` pairs (Phase 91), `GSUB` `liga`/`rlig` (Phase 93), `GDEF`/`GPOS` `SinglePos`, `PairPos`, `MarkToBase`, `MarkToLigature`, and `MarkToMark` positioning (Phase 99, finding 105), and **contextual `GSUB` (`Type 1..7`), `GPOS` `CursiveAttachment` (`Type 3`) / `ContextPos` / `ChainContextPos` (`Type 7`/`8`), multi-script itemization, Unicode Arabic joining (`isol`/`init`/`medi`/`fina`, `ZWNJ`/`ZWJ`, `Lam–Alef`), and cluster-safe visual RTL shaping (Phase 119, finding 124)** are implemented; Indic syllable reordering and `LangSys` language tags remain open. |
| 3 | **Bidi** (UAX #9) | 1 | **Done (Phase 109):** the algorithm is implemented — P2 to L2, isolating run sequences, bracket pairs, mirroring — and the inline formatting context reorders each line by it, so Hebrew reads the way it is written and an Arabic sentence with a number in it puts the number where the language does. `make bidi-conformance` runs Unicode's own two suites: **861,948 / 861,948 cases**. Still open: `direction` and `unicode-bidi` are not CSS properties here, so a paragraph's direction comes from P2/P3 alone. (Arabic joining and RTL mark anchoring landed in Phase 119.) |
| 4 | **A windowed shell** | 3 | `BrowserShell` renders to a terminal. No window, no mouse, no scrolling. |
| 5 | **Automatic GC** | 11 | Collection runs only on `gc()`. A long-running page grows. **The collector's timing is oracle-checked since Phase 103:** `make verify-gc-stress` collects at a seeded allocation count between statements and requires a program to print what it prints with nothing collected — 254,174 collections over 1,186 JavaScript programs and 11,176 over 678 DOM pages, no case differing, with a negative control that must be seen to fail first. The policy is still open: nothing calls the collector on its own (finding 109). |
| 6 | **Typed arrays + ES modules** | 8 | `ArrayBuffer` is **not exposed to script**. Blocks `fetch()` bodies, WebSocket binary frames, Canvas `ImageData`. |
| 7 | **HTTP/1.1 complete + cache** | 21, 35 | **Done (Phases 36, 73, 85):** RFC 6265bis `CookieJar` wired to HTTP `Set-Cookie`/`Cookie` across redirects, bounded RFC 9111 HTTP cache (`Cache-Control`, `ETag`/`If-None-Match`, `Last-Modified`/`If-Modified-Since`, `304`, `Vary`, unsafe invalidation), CORS `OPTIONS` preflight + `Max-Age` cache, mixed-content blocking, chunked + `gzip`/`deflate` + **RFC 7932 Brotli (`br`)** `Content-Encoding`, and **HTTP/1.1 + HTTPS (`SSL*`) `keep-alive` socket pooling** (Phase 85, finding 91). **HTTP authentication challenges done (Phase 113, finding 117):** RFC 7617 `Basic` and RFC 7616 `Digest` (MD5, SHA-256, `-sess`, `qop=auth`), nothing sent pre-emptively, one retry with credentials for that origin and realm, and the challenge reported on the response. Still open: `auth-int`, `SHA-512-256`, `userhash`, `Authentication-Info`/`nextnonce`, `407`/`Proxy-Authenticate`. |
| 8 | **Render conformance** | 38 | **Pixels are measured since Phase 120** (finding 121): `make render-conformance` renders 10 pages and compares 30 assertions against the pixels, read back by a PNG decoder that shares no code with the engine's writer, and the harness is shown to fail (a one-column bug in `fill_rect` reddens 9 of 10). **Still open:** flexbox, grid, floats, `transform`, SVG, canvas, border radius and images are unmeasured, no WPT reftests are vendored, glyph shape is not compared, and there is no two-build pixel diff. |

### Tier 2 — real pages work

Multicol and fragmentation (18); `overflow: auto` and `position: sticky` (19);
Selectors L4 `:has()` and container queries (14, 15); `Proxy`, BigInt, `Intl`
(9, 10); finish the bytecode VM — the tree-walker is **50–100× slower than
V8** on loops and calls (7); `getComputedStyle`, `document.write`;
`TextDecoder`/`TextEncoder` (24); extend WPT to `html/`,
`url/`, `fetch/` and the rest of `css-*` (37).

### Tier 3 — Chromium-class

Multi-process isolation and a renderer sandbox (33) — untrusted input is
parsed in-process today; accessibility, which is *"approximately zero"* (34);
`<video>`/`<audio>` (36); performance engineering (35); Shadow DOM complete
(30); Selection and `contenteditable` (31).

### How fast this goes — and where it stops going fast

Every phase so far has been spec-conformance work against an oracle that
already existed: WPT, html5lib, the W3C XML suite, test262, the CSS Color
rational oracle. That is the shape of problem the agents are fastest at.

Count the oracles in Tier 1. Typed arrays has test262. Shaping has WPT
`css-text`, partly. **A windowed shell has none. Automatic GC has almost
none. An HTTP cache has none.** Three of the eight items that make it a
browser are the ones with nothing to check the answer against — and that is
where the speed goes.

Tier 1 is roughly 15–25 phases of work. At the rate phases have been landing
that is arithmetically a few days and realistically two to four weeks,
because the last third of it has no oracle to converge on.

## Wave 1 — The browser can actually see (rendering & text)

| # | Phase | Size |
| :--- | :--- | :--- |
| 1 | **Fonts and shaping** — system font loading, `@font-face` web fonts, real metrics, complex-script shaping (adopt HarfBuzz-class shaping), bidi/RTL text, OpenType basics. The single biggest visual gap. (**Per-glyph fallback across the discovered system faces landed in Phase 64** — a codepoint the primary face lacks no longer paints nothing. `@font-face` landed in Phase 74, `kern` pairs in Phase 91, `GSUB` common ligatures in Phase 93, `GDEF`/`GPOS` kerning and mark anchoring in Phase 99, UAX #9 bidi in Phase 109, and contextual `GSUB` `Type 1..7`, `GPOS` `CursiveAttachment` / contextual positioning `Type 3/7/8`, Arabic joining, and visual RTL cluster shaping in Phase 119; Indic syllable reordering and CSS `direction`/`unicode-bidi` remain open.) | XL |
| 2 | **Rasterizer upgrade** — anti-aliased, scaled glyphs replace the 8×8 bitmap; image resampling; transforms at raster time. | L |
| 3 | **Windowed shell** — a real window with keyboard/mouse/touch, focus, scrolling, cursor; terminal renderer kept as a fallback. | L |
| 4 | **Image codecs** — PNG, GIF (animated), WebP (JPEG optional, AVIF later). Today: BMP/PPM only. Oracle: WPT `image/`. | M–L |
| 5 | **Compositor / tiling + damage tracking** — tile raster, repaint regions, scroll compositing; GPU path (Skia/ANGLE) or CPU tiles first. | L–XL |
| 6 | **Canvas 2D completion** — path/hit-test gaps vs WPT `canvas/`. The text gap (`fillText`/`strokeText`/`measureText` and the `font`/`textAlign`/`textBaseline` state) is done — Phase 46. | M |

## Wave 2 — JavaScript is a platform

| # | Phase | Size |
| :--- | :--- | :--- |
| 7 | **Finish the bytecode VM** (`VM.cpp`, `BytecodeCompiler.cpp` exist) as the single engine; tree-walker stays as a differential oracle. | L |
| 8 | **Language tier 1** — `ArrayBuffer` + typed arrays + `DataView` (unblocks everything), BigInt. (**`ArrayBuffer` and all nine typed arrays landed in Phase 106** — `Int8Array` … `Float64Array`, `Uint8Array`'s 258 is 2 and `Uint8ClampedArray`'s 300 is 255, one object per buffer so two views of it are `===`, `subarray` shares its bytes and `slice` copies them, `@@toStringTag` names the kind, and a subclass of one is one. What is left of this row: **`DataView`**, the higher-order half of `%TypedArray%.prototype` (`map`/`filter`/`sort`/`reduce`), detaching by `transfer()`, `SharedArrayBuffer`, and **BigInt**. The four interfaces that were blocked on it — `fetch()` bodies, WebSocket binary frames, `ImageData`, and `TextDecoder`/`TextEncoder` — are now possible and are rows of their own. Array-hole semantics landed in Phase 81: `delete a[1]`, `new Array(3)`, a grown `length` and an elision in a literal all make holes, and every built-in treats an index as an own property or a hole. Iterative heap-stack RegExp bytecode VM landed in Phase 37; complete RegExp flags `d`/`u`/`v`, ES2025 modifiers `(?ims-ims:...)`, duplicate named capturing groups, `RegExp.escape()`, and `Symbol.{match,matchAll,replace,search,split,species}` landed in Phase 55.) | M |
| 9 | **Language tier 2** — Proxy, generators/async generators at the top level, ES modules + import maps + dynamic `import()`. Async-generator delegation and return resumption landed in Phase 77; other generator and job-order gaps remain. | M |
| 10 | **Unicode / `Intl`** — ICU-grade data (the IDNA approach, generalized): `DateTimeFormat`/`NumberFormat`/`Segmenter`/`Intl.Collator` and locale-specific collation tailorings. (Unicode 17.0 RegExp property escapes — General_Category, Binary Properties, Script, Script_Extensions, Properties of Strings — and Unicode/legacy RegExp case folding landed in Phase 55; Unicode 17.0 `String.prototype.normalize()`, full + locale-sensitive `tr`/`az`/`lt`/`el` case mapping, CLDR 48 / UCA 17.0 root `localeCompare()` with ECMA-402 validation landed in Phase 60, taking `make diff-strings` to **50,580 / 50,580 = 0 diffs**.) | L |
| 11 | **Automatic GC + leak audit** — collection only runs on `gc()` today; incremental sizing, CI memory benchmarks. | M–L |

## Wave 3 — Real pages (layout & CSS)

| # | Phase | Size |
| :--- | :--- | :--- |
| 12 | **Two known-wrong layout bugs** — block-in-inline is split correctly (Phase 36); sized floats are placed by a band-edge sweep, landing on the exact bottom edge of the float in the way (10,000 sized floats in seconds; nothing escapes the box). **Done.** | M |
| 12a | **Sized-float performance** — index nonempty bands for placement without re-sorting every prior band per float; preserve the already-correct exact-edge placement (Phase 48). **Done**; bands spanning many distinct intervals still require updating those intervals. | S |
| 13 | **Flexbox/Grid** — **parsing done; layout implemented but unmeasured.** `layout_flex` (`src/layout/LayoutBox.cpp:459`) and `layout_grid` (`:707`–`928`) are in the box tree, not only the parser: `flex-direction`, `flex-wrap`, `flex-grow`, line breaking and shrink-to-fit; grid track sizing. But all **87** vendored WPT files (27 `css-flexbox`, 60 `css-grid`) are under **`parsing/`** — they check that a declaration computes and serialises, not where a box lands. The layout has no oracle. See *Known gaps* 7 and row 38. | M–L |
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
| 21 | **HTTP/1.1 complete** — redirects, `Set-Cookie` → jar, chunked, `gzip`/`deflate`/Brotli (`br`), cross-origin redirect `Authorization` stripping, RFC 9111 HTTP cache (`Cache-Control`/`ETag`/`304`/`Vary`), and HTTP/1.1 + TLS `keep-alive` socket pooling **done (Phases 36, 73, 85)**. Open: HTTP auth challenges. | M |
| 22 | **HTTPS** — **Done.** `src/net/ResourceLoader.cpp` loads OpenSSL at run time with `dlopen`/`dlsym`, so nothing links `-lssl` and the engine builds without it; then `SSL_CTX_set_default_verify_paths` (system CA store), `SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER)`, `SSL_set1_host` (RFC 6125 hostname check) and `SSL_get_verify_result`, so a certificate that does not verify closes the connection. Port 443 by scheme. Phase 89 adds IP SAN identity verification and eight local trusted/untrusted DNS/IP fixtures (`make security-https`); Phase 85 already tested TLS keep-alive success. Phase 100 adds chains, dates, IPv6 and SNI fixtures (twenty cases, Python-cross-checked). Open: OCSP/CRL revocation, pinning and TLS 1.3 resumption. | L–XL |
| 23 | **Fetch/XHR/WebSocket policy** — **Fetch & loader policy done (Phase 73):** same-origin + CORS `OPTIONS` preflight + `Access-Control-Max-Age` cache, `credentials` (`omit`/`same-origin`/`include`), `Access-Control-Expose-Headers`, and W3C mixed-content blocking (`https:` → non-loopback `http:`/`ws:`). **WebSocket TLS, policy, and framing done (Phase 90):** `wss://` OpenSSL TLS with CA/SNI/DNS+IP SAN verification, `WebSocketContext` (`Origin` + mixed-content blocking + relative URL resolution across pages/Workers/sandboxed iframes/`file:`), live accessors, synchronous `DOMException`/`TypeError` checks, CSPRNG masking, RFC 6455/3629 framing/UTF-8 checks, and `poll()`/exit join (`make security-websocket`). Upgrade cookies done (Phase 117): the `Cookie` header is the jar's answer for the URL (`ws` as `http`, `wss` as `https`) with the page URL as site-for-cookies, and `Set-Cookie` on the `101` is stored. Open: `Request` constructor, `ArrayBuffer`/`Blob` WebSocket binary exposure (blocked on typed arrays), and `permessage-deflate`. | M |
| 24 | **Encoding Standard complete** — all WHATWG encodings (`Shift_JIS`, `EUC-*`, …), `TextEncoder`/`TextDecoder`. **Decoding done (Phase 47)**: 40 encodings and 228 labels from `whatwg/encoding`, `<meta charset>` prescan, `document.characterSet`, XML `[81] EncName`, `@charset`, `data:` URLs, XHR and subresources (WPT 7,968/8,009 across 103 files, the XML suite 1,718/1,718). Open: `TextDecoder`/`TextEncoder` in script, encoders, and streaming. | M |
| 25 | **Frames/iframes with documents** — `iframe.contentDocument`, `srcdoc`/`sandbox`, XML/SVG-as-a-page (`litebird feed.xml` / `.xhtml` script execution). **Done (Phase 42)**: nested browsing contexts (`HTMLIFrameElement`, per-frame `JsInterpreter`/`Realm`/`WindowProxy`, `src`/`srcdoc`/`sandbox`, cross-origin `WindowProxy` access checks, `postMessage`, subdocument layout/rasterization, and `.xhtml` WPT execution; all 495 `frames` WPT failures resolved, raising WPT to 5,421/5,463 across 87 files). | M–L |
| 25a | **Cross-origin `Window`/`Location` reflection, child navigation and message ports** — `[[OwnPropertyKeys]]` (indices + table + `then`) for keys/descriptors, spread, rest, `for…in`, `JSON`; `Location` as a security exotic; define/delete/integrity/prototype refusal; permitted non-fragment child navigation; same-agent `MessageChannel`/`MessagePort`. **Done (Phases 56 & 61)**. Transfer lists, structured cloning, cross-agent ports and process isolation remain open. | M |
| 26 | **External scripts/stylesheets + module fetching** over the real network. | M |
| 27 | **Form submission** — GET/POST navigation, file upload, `multipart/form-data`, downloads. | M |

## Wave 5 — DOM everywhere

| # | Phase | Size |
| :--- | :--- | :--- |
| 28 | **Processing instructions in the HTML parser** (`<?target data>`) + migrate html5lib→WPT corpus. | M |
| 29 | ~~**`Attr` is a `Node`**; one qualified name in two namespaces; `NamedNodeMap`/`HTMLCollection` own-props~~ (Phase 51). | M |
| 30 | **Shadow DOM complete** — slots, `::part`, event retargeting, constructable stylesheets. | M–L |
| 31 | **Selection + `contenteditable` stack** — `Selection` API, ranges, caret, input events. The authoring gap nobody notices until they try to edit rich text. | L–XL |
| 32 | **SVG completeness + MathML** — gradients and viewBox transforms in render; text is done, `<tspan>` since Phase 82 and the position lists, chunk anchors, `<textPath>` and paint servers on glyphs since Phase 107 (`docs/phases/107-svg-text-positions-chunks-textpath-and-paint.md`), which leaves filters, `textLength`/`letter-spacing`, bidi and MathML beyond parse-only. | M |

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
