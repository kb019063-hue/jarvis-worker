# Finding 96 — WebSocket lacked `wss://` TLS, Fetch `Origin`/mixed-content policy, API validation, and RFC 6455 frame validation (Phase 90)

## What was wrong

After Phase 83 made `wss://` fail closed, `WebSocket` still could not open TLS
connections, had no Fetch request context (`Origin`, `base_url`, mixed-content
blocking, or opaque sandbox origins), used fixed keys and unvalidated frames,
lacked synchronous `DOMException`/`TypeError` validation and live accessors,
and did not coordinate I/O thread shutdown across document and worker exits.

## What changed

- Added runtime-loaded OpenSSL 3 TLS transport (`WsOpenSslApi`) with system/test
  CA trust, `SSL_VERIFY_PEER`, SNI, and DNS/IP SAN verification in
  `src/net/WebSocket.cpp` and `include/litebird/net/WebSocket.hpp`.
- Wired `WebSocketContext` (`document_url`, `base_url`, `opaque_origin`) and
  `WebSocketEventSink` into `JsInterpreter`, `VM`, and `Worker`, implementing
  relative WebSocket URL resolution, exact `Origin` emission (including
  `Origin: null` for `file:`, sandboxed `srcdoc`, and `data:` origins), and
  W3C mixed-content blocking (`https:` -> non-loopback `ws:`).
- Added live JS property accessors, synchronous constructor/`send`/`close`
  validation, CSPRNG `Sec-WebSocket-Key`/masking via OpenSSL `RAND_bytes`,
  full RFC 6455/3629 frame/UTF-8/close validation, and a `poll()`/`m_wake_fds`
  I/O loop.
- Fixed two bugs exposed by `tools/security/verify_websocket.py`:
  1. Exit `SIGSEGV` (`3/60` stress runs) caused by OpenSSL `atexit(OPENSSL_cleanup)`
     running while WebSocket threads were still using `SSL_CTX`/locks; fixed by
     warming OpenSSL/heap/sink state before registering `atexit_shutdown_websockets()`
     and joining all live `WebSocketObject` threads via `join_for_shutdown()`.
  2. Spurious `wss://` close code `1006` caused by `SSL_read` hitting TCP FIN
     before draining `frame_buf` Close frames; fixed by breaking when
     `SSL_pending() == 0` and checking `transport_read_err` after parsing
     buffered Close frames.

## Verification

- `make test`: **249 / 249** suites + **114 / 114** cross-origin checks.
- `make security-websocket`: **29 / 29** checks (**60 / 60** concurrent `4x wss://`
  startup/exit stress runs).
- ASan + UBSan (`build-asan`): **249 / 249** unit suites, **114 / 114**
  cross-origin checks, `verify_websocket.py --stress-runs 20` (**29 / 29**),
  `verify_worker_policy.py --stress-runs 20` (**17 / 17**), JS (**1130 / 1152**),
  DOM (**678 / 678**), URL (**1188 / 1188**), and WPT (**9567 / 9824**) all passed cleanly.

## Still open

`ArrayBuffer`/`Blob` binary frame exposure (blocked on typed arrays),
`WebSocket.prototype` (tracked in the `AGENTS.md` host-constructor row), upgrade
cookies, HTTP proxies, `permessage-deflate`, and standalone `CloseEvent`/
`MessageEvent` constructors; see
[Phase 90](../phases/90-websocket-tls-fetch-policy-and-frames.md).
