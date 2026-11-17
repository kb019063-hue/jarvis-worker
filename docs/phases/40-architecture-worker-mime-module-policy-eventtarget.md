# Phase 40 Architecture: Worker MIME/Module Policy, EventTarget Dispatch & Lifecycle Safety

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 40 Architecture: Worker MIME/Module Policy, EventTarget Dispatch & Lifecycle Safety` section.
> The text below is the section as it stood, unchanged.


Phase 40 completes the remaining Web Worker policy and lifecycle gaps in
`include/litebird/js/Worker.hpp` and `src/js/Worker.cpp` while preserving the
Phase 33 `ResourceLoader::load_for_page` local-file security boundary.

### 1. Strict JavaScript MIME Policy & HTTP Status Checks

- `WorkerObject::is_javascript_mime_type(std::string_view)` implements WHATWG
  MIME Sniffing §4.3: it strips MIME parameters after `;`, trims ASCII HTTP
  whitespace, ASCII-lowercases the essence, and accepts only the 16 standard
  JavaScript MIME types (`text/javascript`, `application/javascript`,
  `application/ecmascript`, `text/javascript1.0`–`1.5`, etc.).
- Both top-level `new Worker(url, options)` fetches and worker-side
  `importScripts(...urls)` route through `load_worker_script()`, requiring
  `ResourceLoader::load_for_page` success, an HTTP `2xx` status code, a
  non-empty `Content-Type`, and a valid JavaScript MIME essence (no MIME
  sniffing). `data:text/plain`, default `data:,...`, HTTP `text/plain`,
  `text/html`, `application/json`, missing `Content-Type`, HTTP `3xx`/`4xx`,
  and local `.txt` files are refused cleanly with an error event.

### 2. `WorkerOptions` Validation, Module Policy & Worker Global Scope

- `new Worker(...)` validates construction (`new` required, at least 1
  argument, `options` nullish or object, `options.type` in `{"classic",
  "module"}` else synchronous `TypeError`).
- `{ type: "module" }` is no longer silently executed as classic JavaScript:
  `start()` refuses execution and queues an `ErrorEvent` delivered to both
  `worker.onerror` and `worker.addEventListener("error", ...)`.
- Inside `DedicatedWorkerGlobalScope`, `self` and global bindings expose
  `self.name` (`options.name`), `self.location` (parsed via `Url::parse`),
  `self.postMessage`, `self.close`, `self.addEventListener`,
  `self.removeEventListener`, `self.importScripts`, `self.fetch`, and
  `self.Worker`.

### 3. Complete `EventTarget` Dispatch & Owner-Thread State Isolation

- Page-side `Worker` (`attach_js_instance`, shared by both `create_constructor`
  and `create_js_instance`) and worker-side `self` share a `ListenerMap`
  supporting function and `{ handleEvent }` listeners, duplicate suppression
  via `strict_equals`, `removeEventListener`, `{ once: true }`,
  `preventDefault()`, `stopPropagation()`, and `stopImmediatePropagation()`.
- Error events populate `type`, `message`, `filename`, `lineno`, `colno`,
  `error`, `cancelable: true`, `defaultPrevented`, `target`, and
  `currentTarget`, and invoke both `onerror` and all `"error"` listeners.
  Uncaught worker errors first notify `self.onerror` and `self`'s `"error"`
  listeners; if not canceled, they forward to the parent worker's `"error"`
  listeners and `onerror`.
- All owner-thread `JsValue` handles (`js_instance`, `onmessage`, `onerror`,
  listener lists) live in `WorkerOwnerJsState` and are only read, mutated, or
  released on the owner thread so non-atomic `JsObject::handle_count` never
  races across threads. Releasing `WorkerOwnerJsState` on `terminate()` or
  after the worker's final exit task breaks the previous `inst` ↔ `WorkerObject`
  reference cycle.

### 4. Concurrent Startup, Nested Workers & Exit-Safe Shutdown

- `worker_thread_main()` serializes `JsInterpreter` construction behind a
  process-wide mutex (skipping startup if already terminated) and attaches each
  parent worker's `EventLoop` via a lifetime-guarded `WorkerOwnerLoopState`
  with a wake callback so nested child workers wake their parent loop without
  polling and are always terminated and joined before the parent's `EventLoop`
  is destroyed.
- Every live worker registers a `std::weak_ptr<WorkerObject>` in an immortal
  `WorkerRegistry`. On first `WorkerObject::start()`, after warming up static
  tables used by workers (`JsLexer`, `JsOps`'s `symbol_registry()`, and
  `DomBindings`'s interface table), `std::atexit` registers
  `atexit_shutdown_workers()`, which wakes and joins every active worker thread
  before C++ tears down static containers.

### 5. Verification & what remains open

- `make test`: **157/157** unit suites pass in both Release (`-O2`) and
  ASan+UBSan (`build-asan/test_suite`).
- `make security-worker`: **17/17** end-to-end checks pass against a live
  loopback HTTP server (including **60/60** concurrent 4-worker startup/exit
  CLI runs in both Release and ASan+UBSan; additionally verified
  **200/200** consecutive runs with 0 crashes, vs 4/30–7/60 `SIGSEGV` before).
- Remaining gaps: ES module worker execution (requires an engine-wide ES module
  loader), structured clone beyond JSON (`ArrayBuffer`/transferables/cyclic
  objects), `SharedWorker`/`MessagePort`, and heap-allocating `symbol_registry()`
  in `src/js/JsOps.cpp` and `s_registry` in `src/js/DomBindings.cpp` as
  defense-in-depth (see `AUDIT.md`, finding 45).

---
