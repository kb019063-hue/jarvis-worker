# Phase 159: WebSocket binary frames (`ArrayBuffer`, typed arrays, `DataView`, and `Blob`)

## 1. What was wrong

`WebSocketObject` already framed RFC 6455 binary messages (`opcode 0x02`) in C++,
and `ArrayBuffer`, typed arrays, `DataView`, and `Blob` all existed in the
engine — yet script could neither send nor receive binary WebSocket frames:

1. **`WebSocket.prototype.send(data)` coerced every argument with `to_string()`.**
   Passing an `ArrayBuffer`, `Uint8Array`, `DataView`, or `Blob` sent a UTF-8
   **text** frame (`opcode 0x01`) containing `"[object ArrayBuffer]"`,
   `"0,1,2"`, or `"[object Blob]"`, and never called `send_binary()`.
2. **`dispatch_message_on_owner` discarded `is_binary` (`(void)is_binary;`).**
   Every incoming binary frame (`0x02` single or fragmented) was delivered as a
   JS string regardless of `ws.binaryType` (`"blob"` or `"arraybuffer"`).
3. **`new Blob([part])` did not extract bytes from `BufferSource` parts.**
   `bytes_of_parts()` in `src/js/DomFileBindings.cpp` only checked for `Blob`
   and called `to_str()` on everything else, turning `new Blob([new Uint8Array([0, 255])])`
   into `"0,255"` instead of the two raw bytes `0x00 0xFF`.
4. **Workers and standalone scripts did not expose `Blob`/`File`**, and
   `TypedArrayRealm` in `src/js/TypedArray.cpp` was a single process-wide static
   struct, so a concurrent Worker calling `install_typed_array_builtins()`
   overwrote the main thread's `ArrayBuffer.prototype` pointer.

## 2. What changed

- **Binary `TypedArray` helpers (`include/litebird/js/TypedArray.hpp`,
  `src/js/TypedArray.cpp`):**
  - Added `is_buffer_source(value)` (true for `ArrayBuffer`, any typed-array
    view, or `DataView`, even if detached or out of bounds) so callers can
    distinguish a detached buffer source from an ordinary stringifiable object.
  - Added `array_buffer_from_bytes(bytes)` to build a fixed-length `ArrayBuffer`
    directly in the current realm.
  - Keyed `TypedArrayRealm` per `JsIntrinsics*` (with a 1-entry `thread_local`
    fast cache and raw non-owning pointers rooted by each realm's global
    environment) so main pages, iframes, and Workers keep isolated
    `ArrayBuffer`/`TypedArray` prototypes without racing.
- **Blob raw-byte bridge (`include/litebird/js/DomBindings.hpp`,
  `src/js/DomFileBindings.cpp`, `src/js/JsInterpreter.cpp`):**
  - Added `dom::blob_bytes(value, out)` to copy raw bytes (including embedded
    NULs) out of any `Blob` or `File`.
  - Updated `bytes_of_parts()` in `DomFileBindings.cpp` to extract raw bytes
    from `BufferSource` parts (`ArrayBuffer`, `TypedArray` subarrays, `DataView`)
    and throw `TypeError` on detached or out-of-bounds buffer sources.
  - Exposed `Blob` and `File` alongside `URL`/`URLSearchParams` in `!m_document`
    contexts (`Worker`s and `--run-js`) because `WebSocket`'s default
    `binaryType` is `"blob"`.
- **WebSocket binary send and receive (`include/litebird/net/WebSocket.hpp`,
  `src/net/WebSocket.cpp`):**
  - Added `WebSocketObject::send_binary(const uint8_t* data, size_t len)` (with
    the existing `std::vector<uint8_t>` overload delegating to it).
  - Updated the JS `send(data)` binding: checks argument count (`TypeError`)
    and `CONNECTING` state (`InvalidStateError`) first; if `is_buffer_source(arg)`,
    extracts the live byte window via `typed_array_bytes` (throwing `TypeError`
    when detached or out of bounds) and synchronously snapshots/enqueues an
    RFC 6455 binary frame (`opcode 0x02`) when `OPEN`; if `dom::blob_bytes(arg)`,
    enqueues a binary frame with the Blob/File bytes; otherwise converts to
    string and sends a text frame (`opcode 0x01`).
  - Recorded the creating `dom::Realm` id on `JsOwnerState` and updated
    `dispatch_message_on_owner(payload, is_binary)` to restore that realm on the
    owner thread and deliver:
    - a JS string when `!is_binary` (regardless of `binaryType`),
    - a fresh fixed-length `ArrayBuffer` (`array_buffer_from_bytes`) when
      `is_binary && binary_type() == "arraybuffer"`,
    - a `Blob` with `type === ""` (`dom::make_blob`) when
      `is_binary && binary_type() == "blob"` (default).
    Because `binary_type()` is read on the owner thread at dispatch time,
    changing `ws.binaryType` inside `onopen` or `onmessage` takes effect for
    subsequent binary messages.

## 3. How it was verified

- **Differential verification vs Node.js v22.22.3** over a shared RFC 6455
  loopback server (7 frames: `ArrayBuffer` with embedded NUL & non-UTF-8 bytes +
  immediate post-`send` mutation, `Uint8Array` subarray, `DataView` slice, `Blob`
  with `BufferSource` + NUL string parts, empty `Uint8Array(0)`, 3-fragment
  binary reply, coerced text frame `null`, plus detached `ArrayBuffer`/`DataView`
  `TypeError` and `CONNECTING` `InvalidStateError`): **wire frames identical
  (7/7, all masked, exact opcodes and payloads) and JS outputs identical**.
- **Unit test suite:** `make test` → **318/318** engine suites (new
  `websocket_binary_frames_arraybuffer_typedarray_and_blob` using `TestPage`) +
  **114/114** cross-origin checks.
- **End-to-end TLS & Worker WebSocket verifier:** `make security-websocket` →
  **35/35** checks (up from 32; adds `wss://` binary roundtrip for `ArrayBuffer`,
  `Uint8Array` subarray, `DataView`, `Blob`, empty frame, detached `TypeError`,
  wire `0x02` payload verification, and Worker `wss://` binary `ArrayBuffer` +
  `Blob` delivery).
- **Sanitizers (ASan + UBSan):**
  `ASAN_OPTIONS=detect_leaks=0 python3 tools/security/verify_websocket.py --binary build-asan/litebird --stress-runs 20` → **35/35** checks, 0 findings;
  ASan binary roundtrip (`ArrayBuffer`, `Blob`, `File`, 3-fragment binary) → `ASAN_OK:4`, 0 findings.
- **Regression scoreboards:**
  - `make js-conformance`: **1,144/1,158 (98.8%)**, everyday **90/90**
  - `make dom-conformance`: **678/678 (100.0%)**, everyday **68/68**
  - `make url-conformance`: **1,188/1,188 (100.0%)**
  - `make wpt`: **9,567/9,824** subtests in 190 files, 0 unlisted failures
  - `make gc-oracle`: **PASS**

## 4. What is still open

- HTTP proxies for WebSocket (`CONNECT`), `permessage-deflate`, and standalone
  `CloseEvent` / `MessageEvent` constructors (`WebSocket.prototype` itself is
  tracked in the host-constructor row).
- `FileReader.prototype.readAsArrayBuffer()` is separate from `Blob.arrayBuffer()`
  (which already resolves an `ArrayBuffer`) and was not added here.
