# Phase 73 — HTTP/1.1 `CookieJar` wiring, RFC 9111 HTTP cache, and CORS preflight

## What was wrong

Litebird already had sockets, runtime-loaded OpenSSL, chunked transfer decoding,
`gzip`/`deflate` decompression, redirects, a `CookieJar` class, and `fetch()` —
yet four pieces between `CookieJar`, `ResourceLoader`, and `Fetch` were either
disconnected or incomplete:

1. **`CookieJar` never touched the network.** `Storage.cpp` stored a flat vector
   of `(name, value, path, secure, http_only)` entries with no `Domain`, no
   `SameSite`, no `Expires` date parser, no real `Max-Age` wall-clock deadline,
   no `__Secure-`/`__Host-` prefix rules, no size bounds, and no mutex. Worse,
   `ResourceLoader.cpp` never read `Set-Cookie` from HTTP responses and never
   sent `Cookie` on HTTP requests. Multiple `Set-Cookie` headers collapsed with
   `, ` could not even be split safely because `Expires=Wed, 21 Oct ... GMT`
   contains a comma inside the date.
2. **Every HTTP request hit the socket unconditionally.** `ResourceLoader` had
   no HTTP cache, so repeated stylesheets, scripts, icons, or `fetch()` calls
   ignored `Cache-Control: max-age`, `immutable`, `Expires`, `ETag`, and
   `Last-Modified`, never sent `If-None-Match` / `If-Modified-Since`, and never
   merged `304 Not Modified` responses. In addition, `load_http_single_hop`
   treated `204 No Content` and `304 Not Modified` like normal body-bearing
   responses unless `Content-Length: 0` happened to be present.
3. **Cross-origin `fetch()` had no `OPTIONS` preflight or mixed-content check.**
   `ResourceLoader::load_for_page` verified `Access-Control-Allow-Origin` after
   sending the request directly — so a cross-origin `PUT`, `DELETE`, or `POST`
   with `Content-Type: application/json` or `X-Api-Key` went straight to the
   origin without an `OPTIONS` preflight, and an `https:` document could issue
   plain `http://` subresource requests to non-loopback hosts. Cross-origin
   redirects also forwarded caller-supplied `Authorization` headers to the new
   origin.
4. **`fetch(url, { headers: new Headers(...) })` serialized method names instead
   of headers.** `Fetch.cpp` inspected `init.headers` with `obj->properties`,
   which on a `Headers` JS wrapper holds `.append`, `.delete`, `.get`, `.has`,
   `.set`, and `.forEach` — while the actual header map lived inside the closure
   of `HeadersObject::to_js` with no exotic unwrap hook. `fetch()` also ignored
   `credentials` and `cache` options, skipped forbidden-method checks
   (`CONNECT`/`TRACE`/`TRACK`), and exposed raw cross-origin response headers
   (including `Set-Cookie`) to script without checking
   `Access-Control-Expose-Headers`.

## What changed

### 1. Thread-safe RFC 6265 / 6265bis `CookieJar` (`Storage.hpp`, `Storage.cpp`)

`CookieJar` is now protected by an internal `std::mutex` and implements:

- **Domain & host-only matching**: leading-dot normalization, host-only flag
  when `Domain` is omitted, suffix boundary checks (`example.com` matches
  `sub.example.com` and never `badexample.com`), rejection of single-label TLD
  public suffixes (`Domain=com`) and IP-literal subdomain spoofing.
- **RFC 6265 §5.1.4 default-path & §5.4 ordering**: default path derived from
  the request URI directory; `Cookie:` serialization sorted longest-path-first,
  then earliest `creation_time` (preserved across overwrites of the same
  `(name, domain, path)` key).
- **Real wall-clock expiry**: `Max-Age` (`<= 0` expires immediately, `Max-Age`
  takes precedence over `Expires`) and RFC 6265 §5.1.1 flexible HTTP-date
  parsing (`Expires=Wed, 21 Oct 2037 07:28:00 GMT` and common 2/4-digit-year
  variants) mapped to `std::chrono::system_clock` via civil-calendar conversion.
- **Security attributes & prefixes**:
  - `HttpOnly` cookies are sent in HTTP `Cookie:` headers, omitted from
    `get_document_cookie()`, cannot be created via `set_document_cookie()`, and
    cannot be overwritten or deleted by script.
  - `Secure` cookies require a trustworthy scheme (`https:`/`wss:`) both when
    set and when transmitted.
  - `SameSite=Strict|Lax|None` (defaulting to `Lax` per RFC 6265bis): `None`
    without `Secure` is rejected; `Strict` is withheld on all cross-site
    requests; `Lax` is sent cross-site only on top-level safe (`GET`/`HEAD`)
    navigations.
  - `__Secure-` requires `Secure` from a secure origin; `__Host-` additionally
    requires `host_only == true` (no `Domain` attribute) and `path == "/"`.
- **Resource bounds**: 4,096 bytes per cookie, 180 cookies per domain, and 3,000
  cookies total with oldest-last-access eviction. Zero-argument
  `get_document_cookie()` / `set_document_cookie(str)` overloads remain intact
  for `DomDocumentBindings` and headless DOM conformance tests without a URL.

### 2. HTTP `Cookie` / `Set-Cookie` wiring & redirect hygiene (`ResourceLoader`)

- `ResourceResponse::header_list` preserves individual raw header lines without
  comma folding so every `Set-Cookie` header on `200` and `3xx` responses is
  ingested intact before following `Location`.
- `load_for_page` and `load_http` attach `Cookie:` per redirect hop when
  credentials apply (same-origin when `credentials != "omit"`, cross-origin only
  when `include_credentials` is set) and ingest `Set-Cookie` **only after**
  `allow_response_fn` (CORS verification) passes — so a rejected cross-origin
  response never mutates `CookieJar`.
- Cross-origin redirects strip caller-supplied `Authorization` headers, and
  `301`/`302`/`303` POST-to-GET rewrites strip `Content-*` entity headers.
- `load_http_single_hop` treats `1xx`, `204 No Content`, `304 Not Modified`, and
  `HEAD` responses as bodyless per RFC 9112 §6.3 so keep-open sockets never
  stall waiting for EOF on a bodyless `204`/`304`.

### 3. Bounded RFC 9111 HTTP cache (`ResourceLoader.hpp`, `ResourceLoader.cpp`)

`execute_http_hop_with_cache` wraps `GET`/`HEAD` network hops in a thread-safe
bounded LRU cache (256 entries / 16 MiB body budget, 2 MiB max single entry):

- **Directives & freshness**: parses `Cache-Control` (`no-store`, `no-cache`,
  `max-age`, `s-maxage`, `must-revalidate`, `immutable`), `Pragma: no-cache`,
  `Expires`, `Date`, and `Age`. Only `200 OK` responses with explicit freshness
  (`max-age`, `immutable`, or `Expires`) or validators (`ETag` /
  `Last-Modified`) are stored, preventing unannotated dynamic endpoints from
  being cached unexpectedly.
- **Conditional revalidation**: stale entries (or `Cache-Control: no-cache` /
  `CacheMode::NoCache`) synthesize `If-None-Match` and `If-Modified-Since`; on
  `304 Not Modified`, response headers are merged into the stored entry,
  `response_time` and freshness metadata are refreshed, and the cached `200`
  body is returned (`from_cache = true`, `revalidated = true`).
- **Isolation, `Vary`, and invalidation**:
  - Partitioned by credentialed vs anonymous mode and CORS origin;
  - `Set-Cookie` / `Set-Cookie2` are stripped before storage so cache hits never
    replay cookie mutations;
  - `Vary: *` refuses reuse while header-list `Vary` compares the recorded
    request header values case-insensitively;
  - Unsafe methods (`POST`, `PUT`, `DELETE`, `PATCH`) that receive `2xx`/`3xx`
    invalidate matching URL entries per RFC 9111 §4.4;
  - `CacheMode` (`Default`, `NoStore`, `Reload`, `NoCache`, `ForceCache`,
    `OnlyIfCached`) backs the `fetch()` `cache` option.

### 4. CORS `OPTIONS` preflight, mixed-content blocking, and `Fetch` completion

- **W3C Mixed Content + Secure Contexts**: subresource requests from `https:`
  documents to non-potentially-trustworthy `http:`/`ws:` URLs are blocked on
  initial request and across redirects, while `127.0.0.0/8`, `[::1]`,
  `localhost`, and `*.localhost` remain permitted per W3C Secure Contexts §3.2.
- **WHATWG Fetch §4.5/§4.9 CORS preflight**: cross-origin `RequestMode::Cors`
  requests using non-safelisted methods (anything outside `GET`/`HEAD`/`POST`)
  or non-CORS-safelisted request headers automatically send an anonymous
  `OPTIONS` preflight carrying `Origin`, `Access-Control-Request-Method`, and
  byte-lowercased sorted `Access-Control-Request-Headers`. Preflight verifies
  `2xx` status, `Access-Control-Allow-Origin`, `Access-Control-Allow-Credentials`
  (forbidding `*` when credentialed), `Access-Control-Allow-Methods`, and
  `Access-Control-Allow-Headers` (requiring `authorization` to be listed
  explicitly rather than matched by `*`), and caches approvals per
  `(origin, url, credentials)` for `Access-Control-Max-Age` (capped at 86,400 s).
- **`Fetch.hpp` / `Fetch.cpp`**: `HeadersExotic` (`JsExotic`) attaches the
  underlying `HeadersObject` directly to `new Headers(...)` instances so
  `HeadersObject::extract_headers_init` unwraps `Headers` objects, `[[k, v]]`
  sequences, and plain header records uniformly; `fetch()` normalizes
  `delete`/`get`/`head`/`options`/`post`/`put`, rejects forbidden methods
  (`CONNECT`/`TRACE`/`TRACK`), honors `credentials` (`omit`/`same-origin`/
  `include`) and `cache` modes, strips `Set-Cookie`/`Set-Cookie2` from script-
  visible `Response.headers`, and filters cross-origin `Response.headers` via
  `Access-Control-Expose-Headers`.

## How it was verified

- `make test` — **218 / 218** engine suites + **114 / 114** cross-origin checks
  pass, including four new suites in `tests/test_suite.cpp`:
  - `http_cookie_jar_rfc6265_domain_path_security_and_redirects`
  - `http_cache_rfc9111_freshness_etag_304_vary_and_invalidation`
  - `cors_preflight_options_max_age_authorization_and_mixed_content`
  - `fetch_headers_instances_cache_modes_credentials_and_exposed_headers`
- AddressSanitizer + UndefinedBehaviorSanitizer (`build-asan/test_suite` and
  `build-asan/cross_origin_test`): **218 / 218** + **114 / 114**, zero sanitizer
  findings.
- Conformance & regression checks: HTML **1,792 / 1,792**, JS **1,124 / 1,150**,
  DOM **669 / 669** (`document.dat` 32/32, `xhr.dat` 19/19), URL **1,188 /
  1,188**, WPT **9,550 / 9,744** (0 unexpected), `verify_worker_policy.py`
  **17 / 17**, `diff_trees.py` **500 / 500** (0 diffs vs `main`), `deep_pages.py`
  **462 / 462** runs.

## What remains open

- HTTP/1.1 persistent `keep-alive` socket pooling (requests still close per hop)
  and Brotli (`br`) content-coding.
- `Request` JS constructor and `ReadableStream` / `ArrayBuffer` `Response`
  bodies (tracked under Tier 1 item #6, Typed arrays).
- Public Suffix List beyond single-label TLDs and common 2-letter ccTLD
  second-level domains.
