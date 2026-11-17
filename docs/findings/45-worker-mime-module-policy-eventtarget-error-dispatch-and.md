# 45. ✅ Worker MIME/module policy, EventTarget error dispatch, and shutdown `SIGSEGV`

> Moved out of `AUDIT.md` — written there as a `## 45. ✅ Worker MIME/module policy, EventTarget error dispatch, and shutdown `SIGSEGV`` section.
> The text below is the section as it stood, unchanged.


**What was wrong.**
1. **No MIME or HTTP-status check on worker scripts:** `src/js/Worker.cpp` ran
   any response returned by `ResourceLoader::load_for_page` as classic
   JavaScript regardless of its `Content-Type` (`text/plain`, `text/html`,
   `application/json`, default `data:,...`, missing `Content-Type`, or HTTP
   `3xx` responses where `resp.success` was true).
2. **`WorkerOptions` ignored:** `new Worker(url, { type: "module" })` silently
   ignored the options argument and executed the script as classic JavaScript;
   invalid `type` values did not throw `TypeError`; `options.name` was dropped;
   calling `Worker(...)` without `new` did not throw `TypeError`.
3. **Broken `EventTarget` error dispatch:** `WorkerObject::add_event_listener`
   stored `"error"` listeners in `m_error_listeners`, but `post_error()` only
   ever called `m_onerror`, silently dropping all `addEventListener("error")`
   listeners. There was no `removeEventListener`, deduplication, `{ once }`,
   `handleEvent`, `preventDefault()`, `stopImmediatePropagation()`,
   `self.onerror`/`self.location`/`self.name`, or `importScripts(...)`, and
   `WorkerObject::create_js_instance()` did not wire `onmessage`/`onerror`.
4. **Exit-time `SIGSEGV` (4/30 runs on `main`) & reference-cycle leak:**
   `create_constructor()` stored `[inst, worker]` inside `worker->m_onmessage`
   while `inst` stored `worker` inside its method closures, creating a strong
   reference cycle. Because `Heap::default_heap()` is intentionally never
   destroyed at exit, active `WorkerObject` threads were never terminated or
   joined before `main()` returned. Reproducing with
   `for (var i = 0; i < 4; i++) new Worker("data:text/javascript,postMessage(1)");`
   crashed with `SIGSEGV` (`rc = -11`) in **4 of 30 runs** on `main`; a
   captured backtrace showed worker threads still inside
   `WorkerObject::worker_thread_main` → `JsInterpreter::JsInterpreter` →
   `install_object_builtins` → `js_symbol_for_key` dereferencing
   `symbol_registry()`'s `static std::unordered_map` after `__run_exit_handlers`
   had already destroyed it. Nested workers also had no parent-loop wake
   mechanism or child teardown before the parent's `EventLoop` was destroyed.

**What changed.**
- `include/litebird/js/Worker.hpp` and `src/js/Worker.cpp`:
  - Added `WorkerObject::is_javascript_mime_type()` implementing WHATWG MIME
    Sniffing §4.3 (the 16 JavaScript MIME essences, parameter stripping, ASCII
    HTTP whitespace trimming, case-insensitive matching, no MIME sniffing) and
    required HTTP `2xx` + a JavaScript MIME type in `load_worker_script()` for
    both `new Worker(...)` and `importScripts(...)`.
  - Added `WorkerType` / `WorkerOptions` parsing (`new` check, `type` enum
    validation, `{ type: "module" }` rejected cleanly via an `ErrorEvent`
    instead of running classic JS, `name` exposed on `self.name` and global
    `name`, plus `self.location` and `importScripts(...)`).
  - Replaced ad-hoc callbacks with unified `ListenerMap` / `EventControl`
    dispatch on both `Worker` and `self` (supporting `onerror`/`onmessage` +
    `addEventListener` + `removeEventListener` + deduplication + `{ once }` +
    `{ handleEvent }` + `preventDefault()` + `stopImmediatePropagation()` +
    `ErrorEvent` metadata, and shared across `create_constructor` and
    `create_js_instance`).
  - Isolated owner-thread `JsValue` state into `WorkerOwnerJsState` (released
    on `terminate()` and on the worker's final exit task to eliminate both
    cross-thread non-atomic `handle_count` races and the `inst` ↔ `WorkerObject`
    cycle).
  - Added a lifetime-guarded `WorkerOwnerLoopState` with parent-wake support
    for nested workers and guaranteed child teardown before parent `EventLoop`
    destruction.
  - Serialized worker `JsInterpreter` initialization and registered an immortal
    `WorkerRegistry` with a `std::atexit` hook (installed after warming up
    `JsLexer`'s keyword table, `JsOps`'s `symbol_registry()`, and
    `DomBindings`'s interface table) so all active worker threads are woken
    and joined cleanly before static destructors run.
- Added 4 unit test suites in `tests/test_suite.cpp` (suites 154–157) and
  `tools/security/verify_worker_policy.py` (`make security-worker`, wired into
  CI `build-and-test` and `make asan-core`).

**How it was verified.**
- `make test`: **157/157** unit suites pass in Release (`-O2`) and
  **157/157** under ASan+UBSan (`build-asan/test_suite`, `ASAN_OPTIONS=detect_leaks=0`).
- `make security-worker`: **17/17** checks pass in Release (loopback HTTP
  `text/javascript`, `text/plain`, `text/html`, `application/json`, missing
  `Content-Type`, HTTP `404`, `data:` MIME checks, `{type:"module"}` refusal,
  `{type:"classic", name}`, invalid `type` `TypeError`, `importScripts` MIME
  and Phase 33 local-file boundary, and **60/60** concurrent 4-worker
  startup/exit runs) and **17/17** under ASan+UBSan (**60/60** stress runs).
- Extended startup/exit stress check: **200/200** consecutive CLI runs of
  4 concurrent `new Worker(...)` with immediate exit pass (**0 crashes**,
  down from 4/30 `SIGSEGV` before; also resolves the 7/60 `SIGSEGV` /
  `heap-use-after-free` worker startup exit bug noted in finding 41).
- Existing conformance suites unchanged on latest `main`: `make conformance`
  1781/1792, `make js-conformance` 906/921, `make dom-conformance` 640/640,
  `make url-conformance` 1188/1188, `make wpt` 4658/5192, `make security-fetch`
  3/3, `make verify-media` 35/35, `make bench-attributes` 6/6, `make bench-css`
  20/20.

**What is still open.**
- **ES module workers:** `{ type: "module" }` is now explicitly refused with an
  error event instead of silently running classic JS; actual module worker
  execution waits for an ES module loader in the JS engine.
- **Structured clone & messaging ports:** `postMessage` still serializes via
  JSON (no `ArrayBuffer`, transferables, cyclic object graphs, `MessagePort`,
  or `SharedWorker`).
- **Defense-in-depth for process-wide registries:** `symbol_registry()` in
  `src/js/JsOps.cpp` and `s_registry` in `src/js/DomBindings.cpp` are still
  destroyed at static teardown; Phase 40 warms them before registering
  `atexit_shutdown_workers()` so worker threads are always joined first, and a
  small follow-up can heap-allocate both like `Heap::default_heap()`.

---
