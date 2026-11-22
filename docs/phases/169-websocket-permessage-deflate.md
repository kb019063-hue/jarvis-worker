# Phase 169: WebSocket `permessage-deflate` compression extension (RFC 7692)

## 1. What was wrong

Litebird's WebSocket client (`src/net/WebSocket.cpp`) advertised no
`Sec-WebSocket-Extensions` header during the HTTP/1.1 Upgrade handshake,
hardcoded `ws.extensions` to `""`, and rejected any `101 Switching Protocols`
response carrying a `Sec-WebSocket-Extensions` header (as well as every frame
with `RSV1` set) with a fatal protocol error:

1. **No `permessage-deflate` offer or RFC 7692 §7.1 parameter negotiation.**
   Real-world WebSocket servers (Node.js `ws`, Python `websockets`, Go
   `gorilla/websocket`, nginx, Cloudflare) negotiate `permessage-deflate` by
   default; Litebird could never compress or decompress WebSocket traffic and
   always reported `ws.extensions === ""`.
2. **No streaming RFC 1951 DEFLATE/INFLATE context across messages.**
   `include/litebird/net/Deflate.hpp` only exposed one-shot `inflate_raw()`,
   `inflate_zlib()`, and `inflate_gzip()` helpers with no encoder and no
   persistent sliding window for RFC 7692 context takeover or `max_window_bits`
   enforcement (`9..15`, with `8` normalized to `9` per RFC 7692 §7.1.2).
3. **No RFC 7692 §7.2 framing or decompression-bomb bounds.**
   `WebSocketObject` rejected `RSV1=1` unconditionally, did not strip/append the
   RFC 7692 §7.2.1/§7.2.2 four-byte `Z_SYNC_FLUSH` trailer (`0x00 0x00 0xFF 0xFF`),
   and had no inflated-size cap distinguishing a `1009` ("Message Too Big")
   decompression bomb from a `1002` ("Protocol Error") corrupt DEFLATE stream.

## 2. What changed

- **Zero-dependency streaming RFC 1951 DEFLATE/INFLATE (`include/litebird/net/Deflate.hpp`,
  `src/net/Deflate.cpp`):**
  - Added `Deflate::InflateStatus` (`Ok`, `InvalidStream`, `OutputLimitExceeded`)
    and `Deflate::InflateStream` supporting persistent LZ77 sliding windows
    (`window_bits` 9..15, with 8 normalized to 9), multi-block streams (stored,
    fixed Huffman, dynamic Huffman), `BFINAL=1` and `BFINAL=0` blocks,
    exact `window_bits` distance enforcement (rejecting back-references beyond
    `1 << window_bits`), RFC 7692 trailing-bit handling after `0x00 0x00 0xFF 0xFF`,
    and strict output-size caps that fail fast with `OutputLimitExceeded` before
    allocating unbounded buffers.
  - Added `Deflate::DeflateStream` implementing zero-dependency LZ77 + fixed
    Huffman encoding with a 3-byte hash chain, `window_bits`-bounded search
    distance, persistent sliding window for context takeover (`reset()` for
    `client_no_context_takeover`), RFC 7692 §7.2.1 `Z_SYNC_FLUSH` trailer
    stripping (`0x00 0x00 0xFF 0xFF`), empty-payload emission (`0x00`), and
    automatic stored-block fallback (chunked at 65,535 bytes) when Huffman
    encoding expands incompressible data.
- **RFC 7692 §7.1 handshake negotiation & `ws.extensions`
  (`include/litebird/net/WebSocket.hpp`, `src/net/WebSocket.cpp`):**
  - Sends `Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits`
    on every upgrade request (matching Chromium and Node.js `WebSocket`).
  - Parses comma-separated extension elements and semicolon-separated
    `token [ "=" ( token / quoted-string ) ]` parameters across one or more
    `Sec-WebSocket-Extensions` response headers.
  - Validates RFC 7692 §7.1 parameters fail-closed: accepts `permessage-deflate`
    with optional `server_no_context_takeover`, `client_no_context_takeover`,
    `server_max_window_bits=8..15`, and `client_max_window_bits=8..15` (unquoted
    or quoted, no leading zeros, no duplicate parameters, no values on
    no-context-takeover flags, no unknown extensions/parameters, and no
    duplicate `permessage-deflate` elements).
  - Reflects the negotiated response header value on `ws.extensions` (empty
    string when the server declines compression).
- **RFC 7692 §7.2 message compression, decompression, and error codes
  (`include/litebird/net/WebSocket.hpp`, `src/net/WebSocket.cpp`):**
  - Moved outbound compression onto the WebSocket I/O thread via
    `OutboundMessage { opcode, payload }` so `DeflateStream` and `InflateStream`
    are owned exclusively by the I/O thread with zero lock contention and strict
    FIFO context-takeover ordering while `ws.bufferedAmount` remains accurate
    for application-level payload bytes.
  - Compresses outbound text (`0x01`) and binary (`0x02`) messages with `RSV1=1`
    when `permessage-deflate` is active; leaves control frames (`Ping`, `Pong`,
    `Close`) uncompressed with `RSV1=0`.
  - Validates `RSV1`/`RSV2`/`RSV3` per RFC 6455 & RFC 7692 §6: `RSV2`/`RSV3`
    always fail with `1002`; `RSV1=1` is rejected with `1002` when compression
    was not negotiated, on any control frame, or on any continuation frame
    (`0x00`); uncompressed data messages (`RSV1=0`) are accepted alongside
    compressed ones even when `permessage-deflate` is active.
  - Decompresses `RSV1=1` messages (single-frame or multi-frame fragmented,
    including interleaved control frames) with a 16 MiB decompressed cap:
    `OutputLimitExceeded` closes with `1009` ("Message Too Big"), corrupt
    DEFLATE streams close with `1002` ("Protocol Error"), and invalid UTF-8
    after decompressing a text message closes with `1007` ("Invalid frame
    payload data").
  - Resets `DeflateStream` / `InflateStream` after each compressed message when
    `client_no_context_takeover` / `server_no_context_takeover` was negotiated.

## 3. How it was verified

- **Independent oracle (Python 3 `zlib` & Node.js v22.22.3):**
  - Confirmed Node.js v22.22.3's built-in `WebSocket` sends
    `Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits` and
    reflects the negotiated header on `ws.extensions`.
  - Exercised Litebird live over TLS (`wss://`) and loopback against Python 3's
    independent `zlib.compressobj` / `zlib.decompressobj` (`wbits=-9..-15`)
    across: default context takeover (`"Hello"` shrinking from 7B to 4B on the
    second message; 1,200B repeated text shrinking to 27B), empty payloads
    (`0x00`), compressible and incompressible binary payloads (verifying
    stored-block fallback), 3-fragment compressed messages with interleaved
    `Ping` (verifying `Pong` has `RSV1=0`), `server_no_context_takeover` +
    `client_no_context_takeover` + `max_window_bits=9`, quoted window-bits
    parameters, invalid extension responses failing closed, and a 20 MiB
    decompression bomb (~20 KiB on wire) rejected with Close `1009`.
- **WebSocket security & protocol verifier:** `make security-websocket` expanded
  from 35 → **38/38** checks (adding 3 live `wss://` `permessage-deflate`
  end-to-end checks against Python `zlib`), passing in both release and
  ASan+UBSan (`--stress-runs 20`, 0 sanitizer findings).
- **Unit test suite:** `make test` → **324/324** engine test suites (new
  `websocket_permessage_deflate_rfc7692` using `TestPage` covering RFC 7692
  §7.2 codec vectors, `window_bits=9` distance enforcement, context takeover vs
  reset, >70 KiB multi-block stored fallback, bomb `OutputLimitExceeded`, §7.1
  negotiation accept/fail-closed matrix, `RSV1`/corrupt/UTF-8 protocol errors
  `1002`/`1007`, and live JS roundtrip) + **114/114** cross-origin checks;
  `python3 tools/ci/check_suites.py` → `ok`.
- **Regression scoreboards:**
  - `make js-conformance`: **1,145/1,158 (98.9%)**, everyday **90/90**.
  - `make dom-conformance`: **678/678 (100.0%)**, everyday **68/68**.
  - `make url-conformance`: **1,188/1,188 (100.0%)**.
  - `make wpt`: **9,567/9,824** subtests in 190 files (**0** unlisted failures).
  - `make gc-oracle`: **PASS** (`461` alive `+0`, 7 collections).

## 4. What is still open

- HTTP proxy tunneling (`CONNECT`) for `ws://` / `wss://`.
- `WebSocket.prototype` completeness (tracked in the host-constructor row of
  `AGENTS.md`).
- WebSocket `message`/`close` delivery now uses real `MessageEvent` /
  `CloseEvent` instances (Phase 187); WebSocket `open`/`error` and Worker
  `message` delivery remain unchanged.
- Dynamic Huffman block emission in `DeflateStream` (currently uses LZ77 +
  fixed Huffman with automatic stored-block fallback, which is fully RFC 1951 /
  RFC 7692 compliant and interoperable with zlib/browsers).
