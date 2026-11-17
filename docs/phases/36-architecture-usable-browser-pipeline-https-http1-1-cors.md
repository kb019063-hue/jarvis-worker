# Phase 36 Architecture: Usable Browser Pipeline, HTTPS/HTTP1.1/CORS, Subresources, Live Reflow, TrueType & Web GUI

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 36 Architecture: Usable Browser Pipeline, HTTPS/HTTP1.1/CORS, Subresources, Live Reflow, TrueType & Web GUI` section.
> The text below is the section as it stood, unchanged.


Before Phase 36, Litebird's parsers and DOM passed thousands of conformance
cases, yet opening a real multi-file web page or web app failed immediately:
`https:` did not exist, HTTP/1.1 `Transfer-Encoding: chunked` and
`Content-Encoding: gzip`/`deflate` failed, `XMLHttpRequest` rejected every
non-`data:` URL, `<link rel="stylesheet">`, `@import`, and `<script src>` were
ignored, `<img src>` could only read raw uncompressed BMP/PPM files (and
bypassed document-origin checks — AUDIT.md, finding 44), `getBoundingClientRect()`
returned `0×0` inside scripts, `<a href="#">x<p>y</p></a>` dropped `<p>y</p>`,
the CLI parsed `.xml`/`.svg` files as HTML, text rendered only as unscaled 8×8
blocks, and there was no graphical browser UI beyond terminal ANSI or offline
`.bmp` dumps.

Phase 36 wires those missing browser subsystems end-to-end while keeping zero
compile-time library link flags (`Makefile` links no `-lssl`, `-lz`, or
`-lfreetype`).

### 1. Pure-C++20 DEFLATE / zlib / gzip (`Deflate.hpp`, `Deflate.cpp`)

Implements RFC 1951 (stored, fixed Huffman, dynamic Huffman), RFC 1950 (zlib
header + Adler-32 verification), and RFC 1952 (gzip headers/trailers + CRC-32
verification, including concatenated gzip members), plus `compress_zlib_stored`
for valid PNG encoding. Decompression enforces a configurable output ceiling
(default 64 MiB) so zip/gzip bombs abort deterministically without exhausting
heap memory.

### 2. HTTPS/TLS, HTTP/1.1 Transport, and CORS (`ResourceLoader`, `Fetch`, `DomWindowBindings`)

- **Runtime `dlopen` TLS (`OpenSslDynamic`)**: Loads `libssl.so.3` /
  `libcrypto.so.3` on demand via `dlopen`/`dlsym`, enables SNI
  (`SSL_ctrl(..., SSL_CTRL_SET_TLSEXT_HOSTNAME, ...)`), verifies server
  certificates against the system trust store (`SSL_CTX_set_default_verify_paths`
  + `X509_VERIFY_PARAM_set1_host`), and degrades cleanly with an explicit error
  if no OpenSSL runtime is installed on the host.
- **HTTP/1.1 Wire Framing**: Non-blocking POSIX socket connect across IPv4/IPv6
  (`getaddrinfo` + `poll`), reserved `.test`/`.invalid` fast-fail so offline
  conformance suites never stall on DNS, `Transfer-Encoding: chunked`
  reassembly, transparent `gzip`/`deflate` body decompression, and up to 5
  `301`/`302`/`303`/`307`/`308` redirects (`303` and `301`/`302` `POST`
  rewritten to `GET`, cross-origin `Authorization` stripped).
- **CORS & Same-Origin Enforcement**: `ResourceLoader::is_same_origin` and
  `check_cors_access_control` enforce `Access-Control-Allow-Origin` (`*` or
  exact origin match) on cross-origin `fetch()` and `XMLHttpRequest` responses
  while preserving Phase 33's local-file isolation (`xml_http_request_answers_data_urls_and_nothing_else`
  and `make security-fetch`). `xhr.responseType = 'arraybuffer'` returns `null`
  cleanly until `ArrayBuffer` exists.

### 3. External Stylesheets, `<base href>`, `@import`, and Classic/Deferred `<script src>`

- **`DocumentStyles` (`DocumentStyles.hpp`, `DocumentStyles.cpp`)**: Collects
  author stylesheets in tree order across inline `<style>`, `<link rel="stylesheet">`
  (honouring `disabled`, `media` queries against the viewport, and `<base href>`
  via `Document::base_uri()`), and XML `<?xml-stylesheet?>` processing
  instructions. Leading `@import` rules are expanded recursively up to depth 5
  with URL cycle detection and per-session `StyleSheetCache` deduplication.
- **`JsInterpreter::load_page`**: Fetches and executes classic `<script src>`
  elements synchronously at parser-insertion points (`document.currentScript`
  set during execution, `error` event dispatched on failed loads without
  executing HTTP 4xx/5xx error pages as JS), and queues external `<script src defer>`
  scripts to run in document order right before `DOMContentLoaded`.

### 4. Origin-Safe PNG/SVG `<img>` Subresources (`ImageDecoder`, `LayoutTree`)

`ImageDecoder` adds full 8-bit/16-bit grayscale, RGB, palette (`PLTE`+`tRNS`),
and RGBA non-interlaced + Adam7 interlaced PNG decoding (`decode_png`), PNG
encoding (`encode_png` / `save_png`), and SVG subresource rasterization.
`LayoutTree` fetches `<img src>` exclusively through `ResourceLoader::load_for_page`
with per-document caching and `0×0` unloaded sizing (AUDIT.md, finding 44).

### 5. Live DOM Geometry, Block-in-Inline Splitting & Layout Reflow

- **Zero-overhead box storage on `Element`**: `Element` stores `m_has_layout_box`
  in an existing padding byte (`sizeof(Element)` unchanged) and border widths on
  `Element::Rare`, keeping per-node memory flat on 200,000-node stress pages.
- **On-demand reflow (`Document::ensure_layout`)**: `getBoundingClientRect()`,
  `getClientRects()`, `offsetWidth`/`offsetHeight`/`offsetLeft`/`offsetTop`/`offsetParent`,
  `clientWidth`/`clientHeight`/`clientLeft`/`clientTop`, and `scrollWidth`/`scrollHeight`
  compare `Document::tree_version()` against the last laid-out version and
  rebuild style + layout on demand when DOM/attributes/styles/viewport changed.
  Detached or `display: none` nodes immediately return `0` / empty rects / `null`
  `offsetParent`.
- **Layout fixes**: `LayoutTree` splits `InlineNode` boxes around block-level
  descendants (`<a href="#">x<p>y</p></a>`) in linear time and unions fragment
  rects back onto the inline element, copies only CSS inherited properties into
  `TextNode` boxes (`inherited_only_style`, fixing doubled background/border/padding
  on text children), synthesizes `InlineBlock` boxes with `FormState` labels/values
  for `<button>`/`<input>`/`<select>`/`<textarea>`, shrink-wraps `inline-block`
  auto widths, and accounts for child padding+border in flex/grid item advances.

### 6. TrueType (`.ttf`) Font Parser & Anti-Aliased Rasterizer (`FontRenderer`)

`FontRenderer` parses binary TrueType tables (`cmap` format 4 & 12, `head`,
`hhea`, `hmtx`, `maxp`, `loca`, `glyf` simple + compound glyphs) in pure C++20,
flattens quadratic Bézier contours, and scanline-rasterizes 4×4 supersampled
coverage bitmaps with UTF-8 codepoint decoding and an LRU glyph cache. It
auto-discovers a system sans-serif `.ttf` (e.g. `DejaVuSans.ttf`,
`LiberationSans-Regular.ttf`) when present and falls back cleanly to the
built-in 8×8 bitmap font when none exists. Both `LayoutBox` text measurement
and `BitmapRenderer::fill_text` share `FontRenderer` metrics so wrapped lines
and painted glyphs align.

### 7. CLI XML/SVG Loading, `--render-png`, and Embedded `--gui` Browser (`WebBrowserServer`)

- `src/main.cpp` and `BrowserShell` load `http://`/`https://` URLs via
  `ResourceLoader::load_navigation`, route `.xml`/`.xhtml`/`.svg` paths and
  `application/xml` / `text/xml` / `*+xml` responses through `parse_xml_document`,
  and add `--render-png <file>`.
- `litebird --gui [--port=8080] [url]` starts `WebBrowserServer`, an embedded
  multi-threaded HTTP/1.1 browser UI bound to `0.0.0.0:<port>` with serialized
  engine access, live PNG viewport rendering, address bar, Back/Forward/Reload,
  viewport resize, click/scroll/keyboard/form forwarding (`hit_test` -> focus ->
  3-phase DOM `click` -> `<a href>` navigation), and DOM/Console/Network
  DevTools inspector tabs, plus `/demo/index.html` (`examples/usable_browser_demo/`).

### 8. Verification

```bash
make -j$(nproc) && make test
./build/litebird examples/usable_browser_demo/index.html --render-png /tmp/demo.png
./build/litebird --gui --port=8080
```

- `make test`: **153/153** unit suites pass after merging `main` (`build-asan/test_suite` verified cleanly).
- Conformance & security: HTML **1781/1792** (`diff-trees` **6,000/6,000** equal),
  JS **907/921** (Everyday **90/90**), DOM **640/640** (Everyday **68/68**;
  out-of-corpus `compare_with_jsdom.py` probe **5/5** equal), URL **1188/1188**,
  XML **1702/1702**, WPT **4658/5192** (0 unlisted), `make security-fetch`
  **3/3**, `make bench-css` **20/20**, `make bench-attributes` **6/6**,
  `make verify-media` **35/35**, `make fuzz-media` **1800/1800**,
  `make diff-strings` **95** (`<= 95`), `make tsan` pass, `make deep`
  **1332/1332** (0 failed), `make asan-deep` **882/882** (0 failed).

### 9. What is still open

- **Frames (`<iframe>` / `<frame>`)**: `iframe.contentDocument` /
  `contentWindow` and nested browsing contexts do not exist yet (495 listed WPT
  subtests).
- **Additional image & font formats**: PNG, JPEG, GIF, WebP (lossless, Phase
  54), SVG, BMP, and PPM are decoded through `ImageDecoder`; lossy `VP8`,
  animated WebP, AVIF, `@font-face` remote font loading, WOFF/WOFF2, and
  complex script shaping (HarfBuzz/bidi) remain open.
- **Networking & CSSOM corners**: CORS preflight `OPTIONS` requests,
  credentialed cross-origin cookies, HTTP/2, `ArrayBuffer`/binary XHR bodies,
  and CSSOM `sheet.cssRules` remain open.

---
