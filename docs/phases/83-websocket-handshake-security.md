# Phase 83 — WebSocket handshake security

Task: the WebSocket policy slice of roadmap item 23. Finding 89.

## What was wrong

The WebSocket client parsed `wss://` as port 443 but then sent its HTTP
upgrade over a raw TCP socket: it did not negotiate TLS, verify a certificate,
or reject the connection. Its response check searched for `101` and the
accept key anywhere in the response, so an HTTP 200 or an unrelated header
could be mistaken for a valid upgrade. URL and subprotocol strings were
inserted into the HTTP request without checking for header injection.

## What changed

`wss://` now **fails closed** before opening a socket. This is deliberately a
refusal, not TLS support. The URL parser determines host, port, path and query;
raw controls, credentials, fragments, unsupported schemes and invalid or
duplicate protocol tokens are rejected before networking. The response needs
an HTTP/1.1 101 status, `Upgrade: websocket`, a `Connection` upgrade token,
and exactly one correctly named `Sec-WebSocket-Accept` whose value matches.
Unrequested extensions and unoffered or missing selected protocols are
rejected. Response headers are limited to 64 KiB and a five-second deadline.
A failure dispatches error and abnormal-close events, including listeners.

The specification oracle is [RFC 6455 §4.1](https://www.rfc-editor.org/rfc/rfc6455#section-4.1)
and the [WHATWG WebSockets opening handshake](https://websockets.spec.whatwg.org/#opening-handshake),
including the requirement for a selected protocol when one was requested.

## Verification

- `make test`: **235 suites** and **114 cross-origin/channel checks** passed
  on base `84d1dc9` with this change. The new
  `websocket_handshake_security_and_secure_transport` uses local TCP peers:
  17 scenarios include a genuine upgrade, counterfeit 101/accept headers,
  duplicate accept, missing upgrade tokens, unsolicited extensions and
  protocols, oversized headers, a secure URL, and injected request strings;
  a script case checks asynchronous error and close dispatch. No external
  network service or certificate is needed.
- `make js-conformance`: **1128/1152**, 0 crashes, 0 timeouts.
- `make wpt`: **9567/9824**, 257 failures listed, zero unlisted.
- `make url-conformance`: **1188/1188**; `make security-fetch`: **3/3**.

## Still open

Secure WebSockets need a real authenticated TLS transport; `wss` currently
fails. The WebSocket implementation does not carry a document request
context, send `Origin`, enforce mixed-content policy, integrate cookies/CSP,
or implement the full Fetch handshake. Relative URLs, synchronous constructor
exceptions and selected `protocol` exposure are incomplete. Frame validation,
mask randomness, `bufferedAmount`, and full close/event behavior are also not
claimed. The five-second bound covers header reception, not DNS or TCP connect.
Those limitations mean this client is not a complete browser security boundary.

Local `make asan-core` also passed all 235 suites, the conformance corpora
and mutation fuzzers before integrating the direct-eval change from main.
The combined tree registers 236 suites; CI verifies that combined tree.
