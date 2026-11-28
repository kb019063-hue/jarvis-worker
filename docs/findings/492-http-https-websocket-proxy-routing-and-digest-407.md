# Finding 492: HTTP/HTTPS and WebSocket proxy routing, and Digest 407 nonce isolation

**Resolved by Phase 492** ([`docs/phases/492-http-https-websocket-proxy-routing-and-digest-407.md`](../phases/492-http-https-websocket-proxy-routing-and-digest-407.md)).

## 1. What was found

Two gaps remained after Phase 242 and Phase 434:

1. **Proxy routing only existed for `wss://`**, leaving `http://`, `https://`, and plain `ws://` unable to route through `HTTP_PROXY`, `HTTPS_PROXY`, or `ALL_PROXY`. As a result, `407 Proxy Authentication Required` could not occur against an explicit HTTP/HTTPS proxy, and `WebSocket` (`ws://` and `wss://`) had no proxy `407` retry loop at all.
2. **Proxy Digest authentication lacked request-context and nonce isolation.** `auth_proxy_authorization_for` passed empty `method` and `request_target` strings to `auth_digest_value` (which breaks RFC 7616 Digest calculations over `CONNECT <host>:<port>` and absolute-form `http://` targets), shared `g_nonce_store` space with origin credentials if authorities collided, omitted `:80` host-alias normalization and `Proxy-Authentication-Info: nextnonce`, and left `Set-Cookie` ingestion active on `407` responses.

## 2. What was done

- **Environment proxy routing (`src/net/ResourceLoader.cpp`, `src/net/WebSocket.cpp`)**:
  - `ResourceLoader` now honors `HTTP_PROXY`/`http_proxy` (absolute-form for `http://`), `HTTPS_PROXY`/`https_proxy` (`CONNECT` tunnel + post-tunnel TLS verification for `https://`), `ALL_PROXY`/`all_proxy`, and `NO_PROXY`/`no_proxy`, failing closed on malformed proxy URLs without direct fallback and stripping caller `Proxy-Authorization` before the inner TLS handshake.
  - `WebSocket` now routes plain `ws://` through `HTTP_PROXY`/`ALL_PROXY` `CONNECT` tunnels (ignoring `HTTPS_PROXY`) while keeping `wss://` on `HTTPS_PROXY`/`ALL_PROXY`.
- **Shared Digest `407` engine (`include/litebird/net/ResourceLoader.hpp`, `src/net/ResourceLoader.cpp`)**:
  - Exposed `resolve_proxy_connect_407` and `note_proxy_connect_success` alongside updated `retry_http_407_if_authorized` so HTTP absolute-form requests, CORS preflights, HTTPS `CONNECT`, and `ws://`/`wss://` `CONNECT` all compute Digest over the true wire method/request-target, honor `stale=true` once, store `nextnonce`, reject `Set-Cookie` on `407`, isolate proxy nonces under a `\x01proxy:` prefix, and erase both credential and proxy nonce on non-stale refusal.

## 3. Evidence

- `make test` passes **533/533 engine suites** and **114/114 cross-origin checks**, including `tests/suites/492-http-websocket-proxy-routing.cpp`.
- `make security-https` passes **33/33** (differentially verified against Python `http.client.HTTPSConnection.set_tunnel` and `urllib.request.proxy_bypass`), `make security-websocket` passes **64/64**, `make security-fetch` passes **3/3**, `make security-worker` passes **17/17**, and `conformance`/`js-conformance`/`dom-conformance`/`url-conformance`/`xml-conformance`/`wpt` all pass at/above baseline.
- `build-asan/litebird` passes `verify_https.py` (33/33), `verify_websocket.py --stress-runs 10` (64/64), `check_fetch_origin.py` (3/3), `verify_worker_policy.py --stress-runs 10` (17/17), and suites `242`, `434`, and `492` under ASan+UBSan (`ASAN_OPTIONS=detect_leaks=0`).

## 4. What is still open

Interactive proxy credential prompts (`407` without `set_proxy_credentials`), non-UTF-8 Digest charsets, and TLS connections directly *to* an `https://` proxy remain unimplemented; `WebSocket.prototype` remains tracked in the host-constructor row.
