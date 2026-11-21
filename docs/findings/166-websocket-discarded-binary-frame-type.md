# Finding 166: WebSocket discarded `is_binary` and stringified every `send()` argument

**Status:** Resolved in Phase 161 (`docs/phases/161-websocket-binary-frames.md`).

## 1. What was wrong

Although `src/net/WebSocket.cpp` already had RFC 6455 binary framing (`opcode 0x02`)
and a C++ `send_binary()` method, its JavaScript bindings could neither send nor
deliver binary data:

1. `attach_ws_js_instance` implemented `ws.send(arg)` as `ws->send(sargs[0].to_string())`,
   converting `ArrayBuffer`, `TypedArray`, `DataView`, and `Blob` inputs into
   text frames (`opcode 0x01`) such as `"[object ArrayBuffer]"`.
2. `WebSocketObject::dispatch_message_on_owner(payload, is_binary)` began with
   `(void)is_binary;` and always set `ev.data = JsValue::make_string(payload)`,
   ignoring `ws.binaryType` (`"blob"` or `"arraybuffer"`).
3. `src/js/DomFileBindings.cpp` converted `BufferSource` parts in `new Blob([part])`
   via `to_str(part)` instead of extracting raw bytes, and standalone/Worker
   interpreters did not expose `Blob`/`File` globals even though `WebSocket`'s
   default `binaryType` is `"blob"`.
4. `TypedArrayRealm` in `src/js/TypedArray.cpp` was a single process-wide static
   object rather than per-`JsIntrinsics`, so a Worker initializing typed arrays
   could overwrite another realm's `ArrayBuffer.prototype` pointer.

## 2. What changed

- Wired `WebSocket.prototype.send(data)` to dispatch `BufferSource` (`ArrayBuffer`,
  any `TypedArray`, `DataView`) and `Blob`/`File` as masked RFC 6455 binary
  frames (`opcode 0x02`) via `send_binary(const uint8_t*, size_t)` with
  synchronous byte snapshotting, rejecting detached or out-of-bounds buffer
  sources with `TypeError` (after the `CONNECTING` `InvalidStateError` check).
- Wired `dispatch_message_on_owner(payload, is_binary)` to restore the
  WebSocket's own `dom::Realm` and construct an `ArrayBuffer` when
  `binaryType === "arraybuffer"` or a `Blob` (`type === ""`) when
  `binaryType === "blob"`, while keeping text frames as JS strings.
- Added `is_buffer_source()` and `array_buffer_from_bytes()` in `TypedArray.cpp`,
  keyed `TypedArrayRealm` per `JsIntrinsics*` (with a `thread_local` cache),
  added `dom::blob_bytes()` and `BufferSource` part support to `DomFileBindings.cpp`,
  and exposed `Blob`/`File` in `!m_document` contexts (`JsInterpreter.cpp`).

## 3. How it was verified

- Differential probe against Node.js v22.22.3 over a shared loopback RFC 6455
  server: 7/7 wire frames (opcodes, masking, raw payloads) and all JS results
  identical.
- `make test`: **318/318** engine suites (including
  `websocket_binary_frames_arraybuffer_typedarray_and_blob`) + **114/114**
  cross-origin checks.
- `make security-websocket`: **35/35** checks (normal build and ASan+UBSan
  `--stress-runs 20`), covering `wss://` page and Worker binary `ArrayBuffer`
  and `Blob` roundtrips and detached-buffer `TypeError`.

## 4. What is still open

HTTP proxy tunneling (`CONNECT`), `permessage-deflate`, standalone
`CloseEvent`/`MessageEvent` constructors, and `FileReader.prototype.readAsArrayBuffer()`.
