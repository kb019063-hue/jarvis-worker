# Phase 242 — WebSocket HTTP CONNECT tunneling

A `wss://` socket used to resolve and connect straight to the URL's host. That
works on an open network, but not where secure traffic must pass through an
HTTP proxy. The WebSocket code already owned TLS and the Upgrade handshake; it
had no step that could ask a proxy to open the byte tunnel between them.

## What changed

For `wss://` only, the receiver now selects a configured proxy from
`HTTPS_PROXY` / `https_proxy`, falling back to `ALL_PROXY` / `all_proxy`. The
value must be an `http://` proxy URL with a host and an optional port (default
80); URL credentials, paths, queries, fragments, and other proxy schemes are
rejected. A malformed selected proxy fails the connection rather than
silently connecting directly.

`NO_PROXY` / `no_proxy` bypasses the proxy when an entry is `*`, an exact host,
a dot-bounded domain suffix (including a leading-dot form), or a matching
host-and-port entry. IPv4 and IPv6 literals are matched as addresses, not as
arbitrary domain suffixes. A bypass connects directly to the WebSocket target.

Otherwise the client resolves and connects to the proxy with the existing
non-blocking connect timeout and wake-pipe handling. It sends only
`CONNECT host:port HTTP/1.1` and the corresponding `Host` header. The bounded
response parser accepts a syntactically valid HTTP/1.0 or HTTP/1.1 response
with a 2xx status; a refusal such as 407, an invalid response, a timeout, or a
closed tunnel fails closed without attempting TLS or sending a WebSocket
Upgrade. Once CONNECT succeeds, the existing TLS handshake still uses the
original WebSocket hostname/IP for SNI and certificate identity verification,
then performs the existing RFC 6455 Upgrade over that tunnel.

Plain `ws://` remains direct. Proxy authentication (`Proxy-Authorization`),
TLS to the proxy itself (`https://` proxy URLs), and proxying Fetch/XHR are not
implemented by this phase.

## Verification

- `make -j2 build/litebird` — passed.
- `python3 -m py_compile tools/security/verify_websocket.py` — passed.
- `make security-websocket` — **50 checks, 0 failures**, including 60/60 concurrent startup/exit runs; proxy coverage includes uppercase and lowercase variables, `ALL_PROXY` fallback, host/port `no_proxy` bypass, unchanged target TLS/Origin/Upgrade behavior, plain `ws://` bypass, and fail-closed 407 with no `Proxy-Authorization` or target Upgrade.
- `python3 tools/security/verify_websocket.py --binary build/litebird --stress-runs 1` — **50 checks, 0 failures**.
- `python3 tools/ci/check_suites.py` — passed; `make test` — **384 engine suites and 114 cross-origin checks, all passing**, including `websocket_invalid_http_proxy_fails_closed`.
