# Finding 174: WebSocket advertised no extensions and rejected every `permessage-deflate` handshake and `RSV1` frame

**Status:** Resolved in Phase 169 (`docs/phases/169-websocket-permessage-deflate.md`).

## 1. What was wrong

Before Phase 169, Litebird's WebSocket client (`src/net/WebSocket.cpp`) could
not negotiate or speak RFC 7692 `permessage-deflate`:

1. The upgrade request omitted `Sec-WebSocket-Extensions`, `ws.extensions` was
   always `""`, and any `101` response containing `Sec-WebSocket-Extensions`
   aborted the connection.
2. `include/litebird/net/Deflate.hpp` only had one-shot decompression helpers
   (`inflate_raw`/`inflate_zlib`/`inflate_gzip`) with no DEFLATE compressor and
   no stateful sliding-window inflater supporting `max_window_bits` enforcement
   or RFC 7692 context takeover.
3. The frame parser rejected every `RSV1=1` frame unconditionally and had no
   RFC 7692 §7.2 decompression path or bomb-protection limit mapping inflated
   overflows to Close code `1009`.

## 2. What changed

- Added zero-dependency streaming `Deflate::DeflateStream` (LZ77 + fixed Huffman
  with stored-block fallback, `window_bits`-bounded back-references, context
  takeover/reset, and RFC 7692 `Z_SYNC_FLUSH` trailer stripping) and
  `Deflate::InflateStream` (`window_bits` distance enforcement, context
  takeover/reset, `BFINAL=0/1` multi-block support, and `OutputLimitExceeded`
  bomb cap) in `include/litebird/net/Deflate.hpp` and `src/net/Deflate.cpp`.
- Updated `include/litebird/net/WebSocket.hpp` and `src/net/WebSocket.cpp` to:
  - Offer `Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits`
    and validate RFC 7692 §7.1 server responses fail-closed, reflecting the
    negotiated header on `ws.extensions`.
  - Keep `DeflateStream` and `InflateStream` exclusively on the WebSocket I/O
    thread via `OutboundMessage`, compressing data messages (`RSV1=1`) in strict
    FIFO order while leaving control frames uncompressed (`RSV1=0`).
  - Decompress single-frame and fragmented `RSV1=1` messages (including
    interleaved control frames) with a 16 MiB cap (`1009` on overflow, `1002` on
    corrupt DEFLATE or illegal `RSV1` on control/continuation frames, `1007` on
    post-decompression invalid UTF-8 for text messages).

## 3. How it was verified

- Differential verification against Python 3 `zlib` (`wbits=-9..-15`) and
  Node.js v22.22.3's `WebSocket` across context takeover, `no_context_takeover`,
  `max_window_bits=9..15`, quoted parameters, fragmented messages with
  interleaved `Ping`/`Pong`, empty & incompressible binary payloads, invalid
  extension fail-closed responses, and a 20 MiB decompression bomb (`1009`).
- `make security-websocket`: **38/38** live `wss://`/`ws://` checks (3 new
  `wss://` `permessage-deflate` checks), passing in release and ASan+UBSan
  (`--stress-runs 20`, 0 sanitizer findings).
- `make test`: **321/321** engine test suites (including
  `websocket_permessage_deflate_rfc7692`) + **114/114** cross-origin checks;
  `python3 tools/ci/check_suites.py` → `ok`.

## 4. What is still open

HTTP proxy tunneling (`CONNECT`) and standalone `CloseEvent`/`MessageEvent`
constructors.
