# 48. ✅ Nested browsing contexts (`<iframe>`), cross-realm DOM wrappers, cross-origin `WindowProxy` isolation, and `.xhtml` script execution (Phase 42)

> Moved out of `AUDIT.md` — written there as a `## 48. ✅ Nested browsing contexts (`<iframe>`), cross-realm DOM wrappers, cross-origin `WindowProxy` isolation, and `.xhtml` script execution (Phase 42)` section.
> The text below is the section as it stood, unchanged.


**The hole.** Four interlocking gaps kept Litebird single-realm on the main
thread and accounted for 495 of the 534 expected WPT failures (`frames` in
`tests/wpt/expected-failures.txt`), plus prevented running WPT `.xhtml` files
and the multi-origin `DOMParser-parseFromString-url*` suite:

1. **No `HTMLIFrameElement` or nested browsing context.** `<iframe>` was a
   plain `HTMLElement`: `contentWindow`, `contentDocument`, `getSVGDocument()`,
   `sandbox`, `srcdoc`, `window.frames`, `window.length`, `window[i]`,
   `window.parent`, `window.top`, `window.frameElement`, `window.closed`, and
   `window.postMessage` were absent or single-window stubs (`window.top ===
   window`, `window.length === 0`). Connecting an `<iframe>` created no initial
   `about:blank` document, navigated neither `src` nor `srcdoc`, and delayed no
   parent `window` `load` event.
2. **Single-realm assumptions across DOM/JS bindings.** `DomBindings::realm_if_any`
   fell back to the last-constructed `Realm` when a `Document` had no realm
   stamp; `JsIntrinsics::current()` and `DomBindings::current_realm()` could be
   clobbered whenever a nested `JsInterpreter` initialized or executed inside a
   parent script call; `new childWin.Comment()`, `new childWin.Text()`,
   `new childWin.Document()`, `new childWin.DOMParser()`, `new childWin.Range()`,
   and `new childWin.MutationObserver()` bound to the caller's active realm
   rather than the constructor's realm (`caller_realm_or`); and creating a DOM
   wrapper from a parent realm vs. a child realm risked either wrong prototype
   chains (`node instanceof childWin.HTMLElement`) or split wrapper identity.
3. **Cross-origin & `sandbox` boundary bypasses.** Even once child realms
   existed, `JsInterpreter::init_builtins()` defines `"document"` on
   `m_global_env`, and `WindowExotic::get` consulted `realm->global_scope`
   before prototype accessors — letting `iframe.contentWindow.document` bypass
   `Window.prototype.document`'s same-origin check on cross-origin or opaque
   `sandbox` frames unless `WindowExotic::get` and `WindowExotic::set` enforce
   HTML §7.2.3 cross-origin `WindowProxy` property filters directly. Similarly,
   `HtmlParser::parse_into(..., scripting=false, on_script_end)` still invoked
   `on_script_end`, so sandboxed frames without `allow-scripts` needed explicit
   `scripting=false` enforcement in `load_page` and `load_xml_page`.
4. **No `<iframe>` subdocument layout/painting, no origin-root mapping, and no
   `.xhtml` script runner.** `LayoutTree` skipped `<iframe>` completely instead
   of laying out a `300×150` replaced box and rasterizing `content_document()`
   under `RasterBudget`; CLI XML/XHTML loads (`src/main.cpp`) parsed XML trees
   without executing `<script>` elements; and `ResourceLoader` had no containment-
   checked test origin mapper (`--map-origin=ORIGIN=DIR`) for multi-origin WPT
   fixtures (`{{hosts[][www1]}}`, `{{hosts[alt][]}}`).

**The fix.**

- **Per-frame `JsInterpreter`, `WindowProxy` preservation, and zero-leak GC:**
  Connecting an HTML `<iframe>` synchronously initializes an initial
  `about:blank` `Document` + `JsInterpreter` child realm (`Document::browsing_realm_id`,
  inherited origin, nesting depth cap of `8`) and queues asynchronous `srcdoc`
  (`about:srcdoc`, fallback base URL = parent document base URL) or `src`
  navigation (`http:`/`https:`/`file:`/`data:`/`about:blank`, HTML and XML/XHTML/SVG
  MIME dispatch) via `JsInterpreter::navigate_document(...)`. `init_builtins()`
  reuses `m_global_object` so `WindowProxy` object identity survives across
  navigations (`before === iframe.contentWindow`), and detaching stamps own
  property `closed = true` onto the detached `WindowProxy` before tearing down
  the child realm so cyclic parent↔child references collect cleanly to zero
  leaked heap cells. Recursive `delaying_load_count` holds parent `window`
  `load` until nested frame loads complete.
- **Cross-realm wrappers, constructors, and HTML §7.2.3 `WindowProxy` security:**
  `DomBindings` maintains single-wrapper identity across realms keyed by
  `Document::browsing_realm_id()` with RAII `JsIntrinsics` guards in
  `JsInterpreter`, `make_function`, and `Realm::proto`. Constructor bindings use
  `caller_realm_or(...)` (`current_callable_realm_id()`) and `DomParserState`
  captures the relevant document (`URL`, `documentURI`, fallback base URL, and
  `pushState`-updated URLs). `HTMLIFrameElement.sandbox` exposes a `DOMTokenList`
  enforcing opaque origins (unless `allow-same-origin`) and blocking script
  execution (unless `allow-scripts`); `contentDocument` / `getSVGDocument()`
  return `null` cross-origin, and `WindowExotic::get` / `WindowExotic::set`
  restrict cross-origin `WindowProxy` access to the HTML §7.2.3 safelist
  (`window`, `self`, `frames`, `length`, `top`, `parent`, `opener`, `closed`,
  `postMessage`, `close`, `focus`, `blur`, `location`, symbols, and indexed
  child frames), throwing `DOMException("SecurityError")` otherwise.
  `window.postMessage(message, targetOrigin)` validates `targetOrigin` (`"*"`,
  `"/"`, or serialized origin; `"null"` never matches opaque origins) and
  queues `MessageEvent` (`data`, `origin`, `source`) on the target realm.
- **Subdocument layout/rasterization, origin-root mapping, and `.xhtml` WPT:**
  `LayoutTree` lays out `<iframe>` as a `300×150` replaced `ImageNode` and
  recursively lays out and rasterizes `element->content_document()` (depth cap
  `8`, guarded by `RasterBudget::allows` + `RasterReservation`) into
  `0xAARRGGBB` `ImageData`. `ResourceLoader` supports containment-checked
  origin mapping (`weakly_canonical` inside root or HTTP `404`), `JsInterpreter::load_xml_page`
  executes XHTML/SVG `<script>` elements in XML mode, and `run_wpt.py` discovers
  `.xhtml` tests (`<![CDATA[...]]>`-safe script staging) and maps WPT multi-host
  origins. Vendored 20 upstream WPT files at pinned commit `f2b7b7add021...`
  (`common/blank.html`, 6 `domparsing/*.xhtml` files, `insert_adjacent_html.js`,
  5 `DOMParser-parseFromString-url*.html` files, and 7 `domparsing/resources/*`).

**How it was verified.**

- `make test`: **165/165** engine unit suites pass in both Release (`-O2`) and
  ASan+UBSan (`build-asan/test_suite`), including three new suites:
  `iframe_browsing_context_hierarchy_and_navigation`,
  `iframe_sandbox_origin_isolation_and_postmessage`, and
  `iframe_cross_realm_wrappers_gc_and_subdocument_rendering` (verifying
  3-round multi-frame zero-leak GC reclamation, symlink-escape `404` containment,
  cross-origin `WindowProxy` `SecurityError` throws, `sandbox` opaque origin +
  script blocking, `postMessage` origin matching, XHTML `<![CDATA[...]]>` script
  execution, and `BitmapRenderer` subdocument pixel output).
- `make wpt`: **5,421/5,463** subtests pass across **87** files (`WPT_FLOOR`
  raised from `4,658` to `5,421`, **+763** passing subtests; expected failures
  reduced from `534` to `42`, with **0** `frames` failures remaining, **211/211**
  `DOMParser-parseFromString-url*.html` passing, and **59/62** `.xhtml` subtests
  passing).
- Full regression & sanitizer matrix clean: html5lib `1781/1792`, DOM `640/640`,
  URL `1188/1188`, XML `1702/1702`, JS `907/921` (`906/921` ASan),
  `security-fetch` `3/3`, `security-worker` `17/17`, `verify-media` `35/35`,
  `asan-core`, `asan-deep` (`882` deep runs, `400` parser + `400` pipeline +
  `600` XML + `1,800` media fuzz runs, `0` findings), and `make deep`.

**What is still open.** Cross-origin `Object.getOwnPropertyDescriptor` /
`Reflect.ownKeys` filtering on `WindowProxy` was closed in Phase 56 (finding
62); cross-origin `Location` navigation is limited to a fragment move,
`MessageChannel` / `MessagePort` / `structuredClone` transferables, `srcdoc`
CSP inheritance, and process-level site isolation remain open.

---
