# Phase 173: WHATWG Fetch `Request` constructor and `fetch(Request, init)`

## 1. What was wrong

Litebird exposed `fetch()`, `Headers`, and `Response`, but had no `Request`
constructor anywhere in script (`typeof Request === "undefined"` in pages,
Workers, and the bytecode VM):

1. **No `Request` class or `Body` mixin.** Libraries and modern web apps that
   construct `new Request(url, init)`, pass `Request` instances into `fetch()`,
   clone requests via `req.clone()`, inspect `req.url`/`req.method`/`req.headers`
   /`req.signal`/`req.bodyUsed`, or consume request bodies via
   `text()`/`json()`/`arrayBuffer()`/`bytes()`/`blob()` failed immediately with
   `ReferenceError: Request is not defined`.
2. **`fetch()` only accepted raw strings and skipped WHATWG `Request` normalization.**
   Passing a `Request` object (or `URL` instance, `BufferSource`, `Blob`, or
   `URLSearchParams` body) did not follow WHATWG Fetch §5.4–§5.5: URL
   credentials were not rejected, `GET`/`HEAD` bodies were not rejected, invalid
   `mode`/`credentials`/`cache`/`redirect`/`referrerPolicy`/`signal` values were
   silently ignored, `bodyUsed` disturbance did not exist, and `Headers`/`Response`
   lacked real `.prototype` objects (`r instanceof Response` and `h instanceof Headers`
   both returned `false`).
3. **No browser request-header guards.** `Headers` had no `HeadersGuard::Request`
   or `HeadersGuard::RequestNoCors` filtering for forbidden request headers
   (`Host`, `Origin`, `Cookie`, `Sec-*`, `Proxy-*`) or `no-cors` safelisted
   request headers.

## 2. What changed

- **`RequestObject` and `HeadersGuard` (`include/litebird/net/Fetch.hpp`, `src/net/Fetch.cpp`):**
  - Added `HeadersGuard` (`None`, `Request`, `RequestNoCors`), RFC 9110 header
    name validation (`is_valid_header_name`), HTTP whitespace normalization,
    WHATWG Fetch §2.2.2 forbidden request-header stripping (`Host`, `Origin`,
    `Cookie`, `Referer`, `Sec-*`, `Proxy-*`, and forbidden method-override
    headers), and `no-cors` CORS-safelisted header enforcement.
  - Added per-realm `.prototype` objects with `Symbol.toStringTag` and
    `constructor` for `Headers`, `Response`, and `Request` (rooted via
    `set_payload_captured` and registered per `JsIntrinsics*`), making
    `req instanceof Request`, `req.headers instanceof Headers`,
    `req.signal instanceof AbortSignal`, and `res instanceof Response` all
    `true`.
  - Implemented `RequestObject` backed by a GC-traced `RequestExotic` (tracing
    `headers_js` and `signal_js` in `RequestExotic::trace(Heap&)`) with full
    WHATWG Fetch §5.4 construction and validation:
    - Enforces `new`-only invocation, arity, `RequestInit` object type,
      `window: null` check, URL parsing against the document/base/worker URL,
      and credential-in-URL rejection.
    - Validates HTTP token methods, rejects forbidden methods (`CONNECT`,
      `TRACE`, `TRACK`), normalizes `DELETE`/`GET`/`HEAD`/`OPTIONS`/`POST`/`PUT`,
      enforces `mode`/`credentials`/`cache`/`only-if-cached`/`redirect`/
      `referrer`/`referrerPolicy`/`no-cors` constraints, and validates `headers`
      and `signal`.
    - Extracts bodies from strings, `URLSearchParams`, `Blob`/`File`, and
      `BufferSource` (`ArrayBuffer`, `TypedArray`, `DataView`, rejecting
      detached/OOB buffers), assigns default `Content-Type` when unset, rejects
      bodies on `GET`/`HEAD`, and tracks `bodyUsed` disturbance across
      `new Request(req)`, `fetch(req)`, `clone()`, and body consumers.
    - Links dependent `AbortSignal`s through the DOM realm's `AbortSignal.any`
      (with `signal: null` unlinking and fallback for document-less realms).
    - Implements prototype getters (`url`, `method`, `headers`, `destination`,
      `referrer`, `referrerPolicy`, `mode`, `credentials`, `cache`, `redirect`,
      `integrity`, `keepalive`, `signal`, `bodyUsed`) and methods (`clone()`,
      `text()`, `arrayBuffer()`, `bytes()`, `json()`, `blob()`), with illegal
      receiver checks.
  - Wired `FetchAPI::fetch(args, context)` through `RequestObject::construct()`,
    turning constructor `JsThrow` errors into rejected Promises per WebIDL,
    propagating resolved method/headers/body/mode/credentials/cache to
    `ResourceLoader::load_for_page()`, and cancelling pre-aborted or in-flight
    fetches through `dom::on_abort` and legacy `AbortSignal`.
- **Global exposure across realms (`src/js/JsInterpreter.cpp`, `src/js/VM.cpp`,
  `src/js/Worker.cpp`):**
  - Installed `Request` on the page global environment (with `<base href>` /
    `document.URL` context), the bytecode VM global table, and
    `DedicatedWorkerGlobalScope` (`env` and `self.Request` with `worker_context`).

## 3. How it was verified

- **Differential conformance against Node.js v22.22.3**
  (`tools/conformance/verify_fetch_request.py`): **10/10** checks pass in both
  release (`build/litebird`) and ASan+UBSan (`build-asan/litebird`), covering
  defaults/identity/prototypes, URL inputs & credentials, method normalization
  & rejection, `RequestInit` constraints, headers init/override, all body types
  & consumers, Request-from-Request & `clone()` & `bodyUsed`, `AbortSignal`
  linking/unlinking, live loopback HTTP `fetch(Request)` / `fetch(Request, init)`,
  and browser security guards + `<base href>` resolution.
- **Engine C++ unit suites (`tests/test_suite.cpp`):** added
  `fetch_request_constructor_validation_body_clone_and_signal` (including cyclic
  `Request <-> Headers <-> AbortSignal` GC reclamation and bytecode `VM`
  invocation) and `fetch_request_loopback_http_worker_and_abort` (loopback HTTP,
  binary `Uint8Array` override, pre-aborted and late-aborted `AbortController`,
  and Web Worker `new Request` + `fetch(req)`): **328/328** suites pass.
- **Existing suites:** `make security-fetch` (3/3), `make security-worker`
  (17/17), `make js-conformance` (1145/1158), and `make dom-conformance`
  (678/678) all remain green.

## 4. What remains open

- Streaming `ReadableStream` request/response bodies (`req.body`, `res.body`,
  `duplex: "half"`) and `FormData` body serialization (tracked separately from
  the `Request` constructor).
- `Response` body disturbance (`Response.prototype.bodyUsed`) and `Response.prototype.blob()`
  alignment.
- `prototype` objects on the remaining four host constructors (`Worker`,
  `WebSocket`, `IntersectionObserver`, `ResizeObserver`).
