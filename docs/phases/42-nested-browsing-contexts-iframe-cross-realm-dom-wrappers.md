# Phase 42 — Nested Browsing Contexts (`<iframe>`), Cross-Realm DOM Wrappers, Origin Isolation, and `.xhtml` Execution

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 42 — Nested Browsing Contexts (`<iframe>`), Cross-Realm DOM Wrappers, Origin Isolation, and `.xhtml` Execution` section.
> The text below is the section as it stood, unchanged.


### 1. Per-Frame `JsInterpreter`, `WindowProxy`, and Load Ordering

- Connecting an HTML `<iframe>` (`DomWindowBindings.cpp::note_iframe_connected`)
  synchronously initializes a child browsing context (`Document` +
  `JsInterpreter` + `dom::Realm`, nesting depth cap `8`) on an initial
  `about:blank` document inheriting the parent document's effective origin and
  `Document::browsing_realm_id()`.
- Asynchronous navigation runs through `JsInterpreter::navigate_document(...)`
  for `srcdoc` (`about:srcdoc` with `fallback_base_url` set to the parent
  document's base URL) and `src` (`http:`/`https:`/`file:`/`data:`/`about:blank`,
  dispatching HTML vs. XML/XHTML/SVG MIME types). `JsInterpreter::init_builtins()`
  reuses `m_global_object` across navigations so `WindowProxy` object identity
  (`before === iframe.contentWindow`) is preserved while resetting JS/DOM realm
  bindings.
- Recursive `delaying_load_count` holds parent `window` `load` events until
  nested frame navigations finish and dispatches `load` (or `error` on HTTP
  `>= 400` / network failure) on the `<iframe>` element. Detaching an `<iframe>`
  stamps own property `closed = true` onto the detached `WindowProxy` before
  shutting down the child realm so cyclic parent↔child references reclaim to
  zero leaked heap cells on GC.

### 2. Multi-Realm DOM Wrappers, Constructors, and `WindowProxy` Security

- `DomBindings` tracks a single wrapper per DOM `Node` keyed via
  `Document::browsing_realm_id()` with RAII `JsIntrinsics` guards in
  `JsInterpreter`, `make_function`, and `Realm::proto`, ensuring cross-realm
  calls never corrupt active intrinsics or duplicate wrappers.
- Constructor bindings (`Document`, `DOMParser`, `Comment`, `Text`, `Range`,
  `MutationObserver`) resolve their target realm through `caller_realm_or(...)`
  (`current_callable_realm_id()`), and `DomParserState` captures the relevant
  document so `new childWin.DOMParser().parseFromString(...)` inherits the child
  document's URL, fallback base URL, and `history.pushState` URL updates.
- `HTMLIFrameElement` exposes `src`, `srcdoc`, `name`, `width`, `height`,
  `sandbox` (`DOMTokenList`), `contentWindow`, `contentDocument`, and
  `getSVGDocument()`. `sandbox` enforces opaque origins (unless
  `allow-same-origin`) and disables script execution in `load_page` /
  `load_xml_page` (unless `allow-scripts`).
- `WindowExotic::get` and `WindowExotic::set` enforce HTML §7.2.3 cross-origin
  `WindowProxy` property checks before consulting `realm->global_scope` or the
  prototype chain, throwing `DOMException("SecurityError")` for non-safelisted
  cross-origin accesses while allowing `window`, `self`, `frames`, `length`,
  `top`, `parent`, `opener`, `closed`, `postMessage`, `close`, `focus`, `blur`,
  `location`, symbols, and indexed child frames. `window.postMessage` validates
  `targetOrigin` (`"*"`, `"/"`, or exact origin; `"null"` never matches opaque
  origins) and queues `MessageEvent` (`data`, `origin`, `source`) on the target
  realm.

### 3. `<iframe>` Subdocument Layout/Rasterization, Origin Mapping, and `.xhtml` WPT

- `LayoutTree` lays out `<iframe>` as a `300×150` replaced `ImageNode` (overridden
  by `width`/`height` attributes or CSS) and recursively lays out and rasterizes
  `element->content_document()` (`BitmapRenderer` -> `0xAARRGGBB` `ImageData`,
  depth cap `8`, bounded by `RasterBudget::allows` and `RasterReservation`).
- `ResourceLoader` supports containment-checked test origin mapping
  (`set_origin_root_for_testing`, `LITEBIRD_ORIGIN_MAP`, and CLI
  `--map-origin=ORIGIN=DIR`, returning HTTP `404` on `weakly_canonical` root
  escape), and `JsInterpreter::load_xml_page` + `src/main.cpp` execute `<script>`
  elements in XML/XHTML/SVG documents.
- Verification: `make test` passes **165/165** suites (Release and ASan+UBSan),
  and `make wpt` passes **5,421/5,463** subtests across **87** files (`WPT_FLOOR`
  raised from `4,658` to `5,421`, **0** `frames` failures remaining).

---
