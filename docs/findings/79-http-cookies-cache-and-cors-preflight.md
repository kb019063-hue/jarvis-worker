# Finding 79 — `CookieJar` was unwired from HTTP, `new Headers()` sent its own method names, and cross-origin `fetch()` skipped preflight

**Severity:** high — authentication cookies set over HTTP disappeared across
redirects, `fetch(url, { headers: new Headers(...) })` corrupted outbound
request headers, cross-origin non-simple requests hit servers without `OPTIONS`
preflight, and `https:` pages could fetch non-loopback `http:` subresources.

**Found by:** reading `src/js/Storage.cpp`, `src/net/ResourceLoader.cpp`, and
`src/net/Fetch.cpp` together while implementing `ROADMAP.md` Tier 1 #7 and
Wave 4 #21/#23.

## What was wrong

1. **`CookieJar` existed only for `document.cookie`.** `grep` for `CookieJar`
   matched `DomDocumentBindings.cpp` and `Storage.cpp` — never
   `ResourceLoader.cpp` or `Fetch.cpp`. An HTTP `302` login response carrying
   `Set-Cookie: sid=...; HttpOnly` dropped the cookie on the floor, and even in
   `document.cookie`, `set_document_cookie` allowed script to overwrite an
   existing `HttpOnly` cookie and ignored `Domain`, `SameSite`, `Expires`,
   wall-clock `Max-Age`, and `__Secure-`/`__Host-` prefixes.
2. **`fetch(url, { headers: new Headers(...) })` iterated the wrapper's JS
   methods.** `HeadersObject::to_js` stored the header map inside lambda
   captures and attached six native methods (`append`, `delete`, `get`, `has`,
   `set`, `forEach`) to `obj->properties`. When `install_fetch` read
   `init.headers`, it iterated `obj->properties` directly — ignoring the closure
   map and serializing the method properties instead.
3. **Cross-origin `RequestMode::Cors` checked `Access-Control-Allow-Origin` only
   after performing the actual method.** A cross-origin `DELETE` or JSON `PUT`
   executed on the server before CORS rejected the response, `Authorization`
   survived cross-origin redirects, `Response.headers` exposed `Set-Cookie` and
   unlisted cross-origin headers to script, and `204`/`304` responses without
   `Content-Length: 0` waited for EOF before returning.
4. **No HTTP cache existed.** Every subresource and `fetch()` opened a fresh
   socket even when `Cache-Control: max-age=...` or `ETag` was present.

## What fixed it

Phase 73:

- Rebuilt `CookieJar` as a thread-safe RFC 6265 / 6265bis jar (`Domain` /
  host-only, `Path`, wall-clock `Max-Age` & `Expires`, `Secure`, `HttpOnly`
  script isolation, `SameSite=Lax|Strict|None`, `__Secure-`/`__Host-`, and per-
  cookie/per-domain/total bounds), wired `Cookie` and raw `header_list`
  `Set-Cookie` across HTTP hops **after** CORS verification, and stripped
  cross-origin redirect `Authorization` headers.
- Added a bounded RFC 9111 LRU HTTP cache with `Cache-Control`, `Expires`,
  `Age`, `ETag`/`If-None-Match`, `Last-Modified`/`If-Modified-Since`, `304 Not
  Modified` header merging, `Vary`, `Set-Cookie` stripping before storage,
  credential/CORS partitioning, unsafe-method invalidation, and `CacheMode`.
- Added WHATWG Fetch CORS `OPTIONS` preflight + `Access-Control-Max-Age` cache,
  W3C Mixed Content blocking (allowing loopback/localhost per Secure Contexts
  §3.2), `HeadersExotic` unwrapping for `new Headers(...)` and `[k, v]` pairs,
  forbidden-method rejection, `credentials`/`cache` fetch options, and
  `Access-Control-Expose-Headers` + forbidden response-header filtering.

## Subtle interaction caught by the new tests

`WebBrowserServer` emits `Cache-Control: no-store` on its built-in HTTP
responses before appending route headers. When the new RFC 9111 cache suite
initially used `WebBrowserServer` with `Cache-Control: max-age=120`, the wire
response carried both `Cache-Control: no-store` and `Cache-Control: max-age=120`,
and the cache obeyed RFC 9111 §5.2.2.5 (`no-store` wins across combined header
lines) by refusing to store the response. Using a dedicated raw socket server
(`LoopbackHttpTestServer`) verified both behaviors independently: `max-age` /
`ETag` caching when `no-store` is absent, and strict non-storage when `no-store`
is present.

## How it was verified

Four new suites in `tests/test_suite.cpp` (`218` suites total + `114` cross-
origin checks, normal and ASan+UBSan builds), plus full HTML/JS/DOM/URL/WPT
conformance harnesses.
