# Phase 90 — WebSocket TLS (`wss://`), Fetch policy, API validation, and RFC 6455 framing

Task: ROADMAP Wave 4 #23 (`Fetch/XHR/WebSocket policy`) / `AGENTS.md` WebSocket security and API row. Finding 96.

## What was wrong

Phase 83 stopped `wss://` from sending plaintext over port 443 by failing
closed, and validated HTTP 101 upgrade responses, leaving four classes of gaps:

1. **No `wss://` TLS transport.** Secure WebSockets could not connect at all.
2. **No Fetch request context or mixed-content policy.** `WebSocket` did not
   carry `document_url`, `base_url`, or `opaque_origin`, never resolved relative
   URLs (`new WebSocket('/ws')`), never sent the serialized `Origin` request
   header, and never enforced W3C mixed-content blocking (`https:` pages
   opening non-loopback `ws://`).
3. **Incomplete WHATWG WebSocket API and validation.** Calling `WebSocket(...)`
   without `new` did not throw `TypeError`; invalid URLs/schemes/fragments/
   credentials and invalid/duplicate subprotocols did not throw
   `DOMException("SyntaxError")` synchronously; `send()` in `CONNECTING` did not
   throw `InvalidStateError`; `close()` did not enforce close-code (`1000` or
   `3000..4999`, `InvalidAccessError`) or UTF-8 reason length (`<= 123` bytes,
   `SyntaxError`); and `url`, `readyState`, `protocol`, `extensions`,
   `bufferedAmount`, and `binaryType` were static values rather than live
   accessors.
4. **Incomplete RFC 6455 framing and teardown races.** `Sec-WebSocket-Key` and
   mask keys were fixed/predictable, frame decoding lacked full validation
   (server masking bit, RSV bits, reserved opcodes, fragmented/oversized
   control frames, RFC 3629 UTF-8 checking, invalid close codes), blocking
   `recv()` had no wake pipe, and worker/page exit raced with `OPENSSL_cleanup`
   and post-Close `SSL_read` EOF handling.

## What changed

- **Authenticated `wss://` TLS (`src/net/WebSocket.cpp`, `include/litebird/net/WebSocket.hpp`):**
  `WsOpenSslApi` loads `libssl.so.3`/`libcrypto.so.3` at run time via
  `dlopen`/`dlsym`, trusts system CA paths plus `LITEBIRD_SSL_CA_FILE` /
  `WebSocketObject::set_ca_bundle_for_testing`, enforces `SSL_VERIFY_PEER`,
  sets SNI via `SSL_set_tlsext_host_name` for DNS names, and verifies peer
  identity via `SSL_set1_host` (DNS) or `X509_VERIFY_PARAM_set1_ip_asc` (IPv4/
  IPv6 literals). Untrusted CAs, expired certificates, wrong SANs, and plaintext
  peers on `wss://` fail closed with `0` HTTP upgrades.
- **Fetch `Origin`, mixed-content, and relative URLs:** Added
  `WebSocketContext` (`document_url`, `base_url`, `opaque_origin`) and wired it
  from `JsInterpreter`, `VM`, and `Worker`. Relative WebSocket URLs resolve
  against `base_url` via `UrlParser` (`ws:`/`wss:` scheme mapping, opaque
  bases like `file:` and `data:` throw `SyntaxError`, `about:srcdoc` uses its
  HTML fallback base URL). `Origin` serializes as `<scheme>://<host>[:port]`
  or `null` for opaque origins (`file:`, sandboxed `srcdoc`/`data:` iframes).
  Secure/HTTPS origins block non-loopback `ws://` (`SecurityError`) while
  allowing potentially-trustworthy `localhost`, `127.0.0.1`, and `[::1]`.
- **WHATWG API parity and RFC 6455 framing:** Added live accessors via
  `define_accessor` (`url`, `readyState`, `bufferedAmount`, `extensions`,
  `protocol`, `binaryType`, `on*` handlers), synchronous `DOMException`/
  `TypeError` checks on constructor/`send`/`close`, OpenSSL `RAND_bytes` (with
  `/dev/urandom` fallback) for `Sec-WebSocket-Key` and client frame masking,
  fragment reassembly, Ping/Pong/Close handling, and strict RFC 6455/3629
  validation (`1002` protocol error, `1007` invalid UTF-8).
- **Non-blocking `poll()` I/O loop and deterministic exit teardown:** Replaced
  blocking `recv()` with `poll()` + `m_wake_fds` and `WebSocketEventSink`
  (tracking `owner_alive` across document and worker realms). Pre-`atexit`
  warmup of `Heap::default_heap()`, `main_ws_event_sink()`, CA path strings,
  and OpenSSL contexts plus `atexit_shutdown_websockets()` joining all live
  `WebSocketObject` threads via `join_for_shutdown()` prevents `OPENSSL_cleanup`
  teardown crashes; checking `SSL_pending() == 0` and draining `frame_buf`
  before reporting a transport read error prevents spurious `1006` after a
  clean `wss://` Close frame.

## Verification

- `make test`: **259 / 259** engine test suites + **114 / 114** cross-origin
  checks passed, including `websocket_handshake_security_and_secure_transport`
  and `websocket_tls_origin_mixed_content_and_framing`.
- `make security-websocket` (`tools/security/verify_websocket.py`): **29 / 29**
  end-to-end CLI checks passed against real OpenSSL 3 CA/SAN/IP/expired TLS
  servers and HTTP/HTTPS/Worker/sandboxed-iframe/`file:` origins, including
  **60 / 60** concurrent `4x wss://` startup/exit stress runs.
- `make security-worker`: **17 / 17** (`60 / 60` stress runs);
  `make security-https`: **8 / 8**; `make security-fetch`: **3 / 3**.
- `make conformance`: **1967 / 1973 (99.7%)**; `make js-conformance`:
  **1137 / 1156 (98.4%)**; `make dom-conformance`: **678 / 678 (100.0%)**;
  `make url-conformance`: **1188 / 1188 (100.0%)**; `make wpt`: **9567 / 9824**
  subtests across 190 files (257 listed, 0 unlisted).
- ASan + UBSan (`build-asan`, `ASAN_OPTIONS=detect_leaks=0`): **259 / 259**
  unit suites, **114 / 114** cross-origin checks, `verify_websocket.py --stress-runs 20`
  (**29 / 29**, **20 / 20** stress runs), `verify_https.py` (**8 / 8**),
  `verify_worker_policy.py --stress-runs 20` (**17 / 17**),
  `run_js_conformance.py` (**1136 / 1156**), `run_dom_conformance.py`
  (**678 / 678**), `run_url_conformance.py` (**1188 / 1188**), and `run_wpt.py`
  (**9567 / 9824**) passed with zero sanitizer findings.

## Still open

- Binary frames via `ArrayBuffer`, typed arrays, and `Blob` (`send()` and
  `binaryType = "arraybuffer"` message delivery) await Wave 2 #8
  (`ArrayBuffer`/typed arrays).
- `WebSocket.prototype` is tracked alongside other host constructors in the
  `AGENTS.md` function/prototype row; `BytecodeCompiler` emits `OpCall` for
  `NewExpr`, so `VM` constructor context is verified through `js_construct()`.
- Upgrade `Cookie`/`Set-Cookie` integration, HTTP proxies, `permessage-deflate`,
  and standalone `CloseEvent`/`MessageEvent` constructors remain unimplemented.
