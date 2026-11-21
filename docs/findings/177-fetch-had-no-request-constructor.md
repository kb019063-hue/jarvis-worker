# Finding 177: Fetch had no `Request` constructor, `fetch(Request, init)` support, or request-header guards

**Status:** Resolved in Phase 173 (`docs/phases/173-fetch-request-constructor.md`).

## 1. What was wrong

Before Phase 173, Litebird implemented `fetch()`, `Headers`, and `Response`,
but omitted the WHATWG Fetch `Request` interface:

1. `Request` was undefined in pages, Web Workers, and the bytecode VM, so
   `new Request(url, init)` threw `ReferenceError`.
2. `FetchAPI::fetch()` only parsed a raw URL string and a partial options bag;
   it did not accept `Request` objects, did not enforce WHATWG Fetch §5.4
   validation (credentials in URLs, forbidden/invalid methods, `GET`/`HEAD`
   bodies, `mode`/`credentials`/`cache`/`only-if-cached`/`redirect`/
   `referrerPolicy`/`window`/`signal` rules), did not extract `BufferSource`/
   `Blob`/`URLSearchParams` bodies, and did not track `bodyUsed`.
3. `Headers` had no request-header guard (`HeadersGuard::Request` /
   `HeadersGuard::RequestNoCors`), and `Headers`/`Response` had no `.prototype`
   objects (`r instanceof Response` and `h instanceof Headers` returned `false`).

## 2. What changed

- Added `RequestObject`, `RequestExotic` (with `trace(Heap&)` GC marking for
  `headers_js` and `signal_js`), `HeadersGuard` (`None`, `Request`,
  `RequestNoCors`), and per-realm `.prototype` objects with `Symbol.toStringTag`
  for `Headers`, `Response`, and `Request` in `include/litebird/net/Fetch.hpp`
  and `src/net/Fetch.cpp`.
- Implemented full WHATWG Fetch §5.4 `Request` construction, `clone()`,
  `Body` consumers (`text()`, `arrayBuffer()`, `bytes()`, `json()`, `blob()`),
  and dependent `AbortSignal` linking via the DOM realm's `AbortSignal.any` /
  `dom::on_abort`.
- Routed `FetchAPI::fetch(input, init)` through `RequestObject::construct()`,
  converting constructor validation errors into rejected Promises and wiring
  `Request` into `src/js/JsInterpreter.cpp`, `src/js/VM.cpp`, and
  `src/js/Worker.cpp`.

## 3. How it was verified

- Differential verification against Node.js v22.22.3 via
  `python3 tools/conformance/verify_fetch_request.py`: **10/10** checks pass on
  both `build/litebird` and ASan+UBSan `build-asan/litebird` (including live
  loopback HTTP `fetch(Request)`/`fetch(Request, init)` parity and browser
  header-guard + `<base href>` checks).
- Unit suites `fetch_request_constructor_validation_body_clone_and_signal` and
  `fetch_request_loopback_http_worker_and_abort` in `tests/test_suite.cpp`:
  **328/328** engine test suites pass; `make security-fetch` (3/3),
  `make security-worker` (17/17), `make js-conformance` (1145/1158), and
  `make dom-conformance` (678/678) remain green.
