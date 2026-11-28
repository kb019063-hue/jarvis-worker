# Phase 492: HTTP/HTTPS and WebSocket proxy routing with Digest 407

Task rows:
- **HTTP authentication is whole except for the parts that need a prompt or a proxy** (`AGENTS.md`, `ROADMAP.md` row 7)
- **WebSocket binary frames, `permessage-deflate`, typed event delivery, and HTTP CONNECT tunneling** (`AGENTS.md`, `ROADMAP.md` row 23)

Finding 492 (`docs/findings/492-http-https-websocket-proxy-routing-and-digest-407.md`).

## 1. What was wrong

After Phase 242 and Phase 434, proxy routing and `407` authentication had four concrete gaps across `ResourceLoader` and `WebSocket`:

1. **`http://` and `https://` had no HTTP proxy routing.** `ResourceLoader::load_http_single_hop` always connected directly to the origin host, never honoring `HTTP_PROXY`/`http_proxy`, `HTTPS_PROXY`/`https_proxy`, `ALL_PROXY`/`all_proxy`, or `NO_PROXY`/`no_proxy`. As a consequence, `407` proxy authentication only triggered if an origin or middlebox answered `407` on a direct hop, never through an explicit proxy (`GET http://... HTTP/1.1` absolute-form or `https://` `CONNECT`).
2. **Plain `ws://` bypassed `HTTP_PROXY` and `ALL_PROXY`.** `WebSocket.cpp`'s `websocket_proxy_for` returned immediately when `!secure`, so plain `ws://` connections ignored `HTTP_PROXY`/`ALL_PROXY`, while neither `ws://` nor `wss://` attempted proxy `407` authentication during `CONNECT`.
3. **Proxy Digest authentication reused origin nonce space and left stale proxy nonces behind.** `auth_proxy_authorization_for` passed empty `method` and `request_target` strings to `auth_digest_value` (fine for Basic, broken for Digest `CONNECT`, absolute-form `http://`, or origin-form requests) and keyed `g_nonce_store` without separating proxy authority from origin authority. It also matched only exact `proxy_authority` (`host:port`), missed implicit `:80` aliases, did not clear stale proxy nonces when deleting/clearing proxy credentials, and did not track `Proxy-Authentication-Info: nextnonce="..."` across `407` completions.
4. **`Set-Cookie` on a `407` was ingested.** `load_http_single_hop` ran `CookieJar::ingest_set_cookie_header` even when `status_code == 407`, allowing a proxy challenge response to mutate document cookies before authentication succeeded.

## 2. What changed

- **HTTP/HTTPS environment proxy selection and `NO_PROXY` (`src/net/ResourceLoader.cpp`)**:
  - `http_proxy_for` resolves `HTTP_PROXY`/`http_proxy` for `http://` and `HTTPS_PROXY`/`https_proxy` for `https://`, falling back to `ALL_PROXY`/`all_proxy`, and bypasses direct when `NO_PROXY`/`no_proxy` matches (`*`, IPv4/IPv6/host, leading-dot/wildcard DNS suffix, optional `:port`, never matching suffix boundaries on IP literals).
  - Non-empty invalid proxy URLs (wrong scheme, missing host, embedded userinfo, path, bad port) fail closed with `"Invalid HTTP proxy URL"` and never fall back to a direct connection.
  - `http://` routes to the proxy with absolute-form request-target (`GET http://host[:port]/path?query HTTP/1.1`), distinct proxy keep-alive pool keys (`http-proxy|<proxy>|<target>` vs `https-proxy|<proxy>|<target>|<ca>`), and suppresses `407` `Set-Cookie` ingestion (`net_resp.status_code != 407`).
  - `https://` establishes an HTTP/1.0 or HTTP/1.1 `CONNECT <host>:<port>` tunnel (`validate_connect_response` rejecting control characters and invalid framing, stripping any caller `Proxy-Authorization` from the inner TLS request, and completing TLS + SNI + CA/SAN verification only after `2xx`).
- **Digest `407`, `stale=true`, `nextnonce`, and strict store/nonce separation (`include/litebird/net/ResourceLoader.hpp`, `src/net/ResourceLoader.cpp`)**:
  - Added `ResourceLoader::resolve_proxy_connect_407` and `ResourceLoader::note_proxy_connect_success` so both `HTTPS` and `WebSocket` `CONNECT` handshakes share a bounded 3-attempt (`0` unauthenticated -> `1` auth -> `2` `stale=true` retry) challenge/retry/refusal-erasure path backed strictly by `g_proxy_auth_store`.
  - Isolated proxy Digest nonces from origin Digest nonces (`\x01proxy:` key prefix in `g_nonce_store`), wired exact wire `method` (`CONNECT`, `GET`, `OPTIONS`, ...) and `request_target` (`authority` for `CONNECT`, `absolute-form` for proxied `http://`) into `auth_proxy_authorization_for`, parsed `Proxy-Authentication-Info: nextnonce`, and purged proxy nonces on credential refusal and `clear_proxy_credentials()`.
  - CORS `OPTIONS` preflights and hop requests (`load_for_page`, `load_http`) both retry `407` via `retry_http_407_if_authorized` using absolute-form targets; redirect hops do not receive preemptive `Proxy-Authorization`.
- **Plain `ws://` proxy routing and WebSocket `CONNECT` `407` (`src/net/WebSocket.cpp`)**:
  - Plain `ws://` honors `HTTP_PROXY`/`http_proxy` (falling back to `ALL_PROXY`/`all_proxy`) and `NO_PROXY`/`no_proxy`, ignoring `HTTPS_PROXY`/`https_proxy`.
  - Both `ws://` and `wss://` tunnel through `CONNECT` using a 3-attempt `resolve_proxy_connect_407` / `note_proxy_connect_success` loop before Upgrade (and before TLS for `wss://`), forgetting refused credentials and failing closed with `"WebSocket HTTP proxy CONNECT failed (407)"` on refusal.
- **Unit and differential security harness coverage**:
  - Added standalone suite `tests/suites/492-http-websocket-proxy-routing.cpp` (`net_http_https_websocket_proxy_routing_and_digest_407`) exercising RFC 7616 SHA-256 Digest `407` (`GET`, `OPTIONS`, `CONNECT`), `stale=true`, `Proxy-Authentication-Info: nextnonce`, `Set-Cookie` suppression on `407`, CORS preflights, cross-origin redirect non-leakage, `NO_PROXY` bypass, invalid-proxy fail-closed behavior, inner-TLS `Proxy-Authorization` stripping, refused credential/nonce cleanup, and `ws://`/`wss://` `CONNECT` Digest `407` echo.
  - Extended `tools/security/verify_https.py` (27 -> 33 checks) with differential proxy tests against Python `http.client.HTTPSConnection.set_tunnel` + `urllib.request.proxy_bypass` (`HTTPS_PROXY` `CONNECT`, cert mismatch refusal through tunnel, `407` fail-closed before TLS, `all_proxy` + `no_proxy`, invalid proxy fail-closed, `HTTP_PROXY` absolute-form for `http://`).
  - Extended `tools/security/verify_websocket.py` (61 -> 64 checks) with plain `ws://` `HTTP_PROXY`/`NO_PROXY`, `all_proxy`, and plain `ws://` `407` fail-closed checks (plus `HTTP/1.0`/`HTTP/1.1` CONNECT validation and multi-connection proxy stability).

## 3. Verification

All release verification and focused sanitizer checks below were run on the merged tree (`f2d42e75`, including `origin/main` `4ea7bfbe`) before writing docs:

- `make -j"$(nproc)" all build/test_suite build/cross_origin_test && ./build/test_suite && ./build/cross_origin_test`: **533/533 engine suites** passed (67 s) plus **114/114 cross-origin checks** passed (0 failures).
- `make security-https`: **33/33 HTTPS checks** passed (0 failures), differentially checked against Python `ssl` / `http.client`.
- `make security-websocket`: **64/64 WebSocket checks** passed (0 failures).
- `make security-fetch`: **3/3 Fetch policy checks** passed (0 failures).
- `make security-worker`: **17/17 Worker policy checks** passed (0 failures).
- `make conformance`: **1,973/1,973** HTML5lib tree-construction checks passed across 62 files.
- `make js-conformance`: **1,153/1,158 (99.6%)**, everyday probe **90/90 (100.0%)**, 0 crashes/timeouts.
- `make dom-conformance`: **678/678 (100.0%)**, everyday probe **68/68 (100.0%)**.
- `make url-conformance`: **1,188/1,188 (100.0%)**.
- `make xml-conformance`: **1,718/1,718 (100.0%)** across 2,585 documents.
- `make wpt`: **9,570/9,824** subtests passed across 190 files (254 listed failures, 0 fail unlisted).
- ASan+UBSan (`make -j"$(nproc)" asan-build`, `ASAN_OPTIONS=detect_leaks=0`):
  - `python3 tools/security/verify_https.py --binary build-asan/litebird`: **33/33** passed.
  - `python3 tools/security/verify_websocket.py --binary build-asan/litebird --stress-runs 10`: **64/64** passed.
  - `python3 tools/security/check_fetch_origin.py --binary build-asan/litebird`: **3/3** passed.
  - `python3 tools/security/verify_worker_policy.py --binary build-asan/litebird --stress-runs 10`: **17/17** passed.
  - Standalone ASan+UBSan driver compiling `tests/suites/242-websocket-connect-tunneling.cpp`, `434-proxy-authorization.cpp`, and `492-http-websocket-proxy-routing.cpp` against `build-asan/*/*.o`: all **3/3** proxy/auth suites passed with 0 sanitizer findings.

Local validation limits: on this 2-core / 3.8 GiB sandbox, full `make asan` across all 533 suites and `make deep` were not run; the repository CI workflow runs the full matrix before merge.

## 4. What is still open

- Interactive proxy credential prompts (`407` still requires `set_proxy_credentials` rather than prompting a user), non-UTF-8 Digest charsets, and TLS connections *to* an `https://` proxy (`HTTPS` proxy schemes remain rejected fail-closed).
- `WebSocket.prototype` remains tracked separately in the host-constructor row of `AGENTS.md`.
