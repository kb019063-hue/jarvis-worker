# Finding 246 — WebSocket could not use an HTTP proxy

**Status:** Resolved in Phase 242 (`docs/phases/242-websocket-connect-tunneling.md`).

A `wss://` request connected directly to its destination. In networks where
secure traffic must use an HTTP proxy, WebSocket connections could not open,
and there was no explicit bypass list. The TLS client and its hostname checks
were already correct; the missing step was establishing an HTTP `CONNECT`
tunnel before beginning TLS.

## What changed

Phase 242 makes `wss://` select an HTTP proxy from `HTTPS_PROXY` or
`https_proxy`, with `ALL_PROXY` / `all_proxy` as fallback, and honors
`NO_PROXY` / `no_proxy` host, domain-suffix, wildcard, and optional-port
entries. It validates the CONNECT response before using the socket as a tunnel;
non-2xx responses (including 407) fail closed. The original URL hostname still
controls SNI and certificate identity checks after the tunnel is established.

Plain `ws://`, proxy authentication, HTTPS-protected proxy endpoints, and
Fetch/XHR proxy support remain outside this fix.

## Verification

`make security-websocket` covers successful CONNECT, uppercase and lowercase
environment variables, `ALL_PROXY` fallback, port-specific bypass, a 407
refusal, and the existing TLS/Origin/Upgrade regressions. The suite
`websocket_invalid_http_proxy_fails_closed` checks that an invalid configured
proxy cannot turn into a direct connection.
