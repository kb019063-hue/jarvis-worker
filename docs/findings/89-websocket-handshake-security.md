# Finding 89 — secure WebSocket URLs silently used plaintext (Phase 83)

## What was wrong

`wss://` was routed to port 443 but the bytes sent were an ordinary HTTP
upgrade on a TCP socket, without TLS. The client also accepted arbitrary
responses containing the digits `101` and its expected accept hash, and
allowed URL/protocol strings into request headers without validation.

## What changed

Secure URLs fail before opening a socket until an authenticated TLS transport
exists. Plain WebSocket upgrades are checked by status and named headers;
unrequested extensions and invalid subprotocol selections fail. Request
components are parsed/validated and handshake header reception is bounded.
Error and abnormal-close events are dispatched on connection failure.

## Verification

`make test` passes 235 suites, including 17 local adversarial handshake cases
and script-visible failure events. The expected handshake is derived from
[RFC 6455 §4.1](https://www.rfc-editor.org/rfc/rfc6455#section-4.1) and the
[WebSockets Standard](https://websockets.spec.whatwg.org/#opening-handshake).
`make wpt` reports 9567/9824 with all 257 failures listed, none unlisted.

## Still open

This is a fail-closed mitigation, not TLS, Origin or mixed-content enforcement.
The remaining API, frame and networking gaps are described in
[Phase 83](../phases/83-websocket-handshake-security.md).
