# Finding 155 — the response had bytes, but no ArrayBuffer path

Resolved by [Phase 150](../phases/150-binary-response-bodies.md).

## What was found

The engine already had `ArrayBuffer`, typed arrays, `TextDecoder`, and response bytes in memory, but the web-facing body APIs stopped at text:

```js
const response = await fetch(url);
response.arrayBuffer             // undefined

const blob = new Blob(["bytes"]);
blob.arrayBuffer                 // undefined

const xhr = new XMLHttpRequest();
xhr.responseType = "arraybuffer";
// xhr.response was null after a successful request
```

That left a gap between a network response and every consumer that needs raw bytes. Going through text is not safe: a NUL is easy to lose in a C-style path, and invalid text bytes can be replaced during decoding.

## Why it was easy to miss

The readable paths (`Response.text()`, `Response.json()`, `Blob.text()`, and XHR's text response) all operated on strings, so text-only probes looked complete. XHR even accepted the `"arraybuffer"` enum value, which made the missing response factory branch look like an empty response rather than an unimplemented binary path.

The key probe therefore used bytes that are not ordinary printable ASCII: `A`, NUL, and the two UTF-8 bytes for `é`. Every returned buffer must report four bytes and preserve them as `65, 0, 195, 169`.

## What fixed it

Phase 150 wires `Response.arrayBuffer()` and `Blob.arrayBuffer()` to promises resolving with fresh buffers, and adds the missing XHR `arraybuffer` response branch. Each path copies the exact stored bytes into the existing `Uint8Array`/`ArrayBuffer` representation; no decoder participates.

## Verification

`make test` passes **308 engine suites** and **114** cross-origin Window/Location/channel checks. The fetch/response, Blob, and XHR regression cases assert `instanceof ArrayBuffer`, byte length, and every byte through `Uint8Array`.

## What remains open

The body stream and one-shot `bodyUsed` semantics are not implemented; `Response` construction does not yet accept BufferSource/Blob bodies; Blob streams and BufferSource parts remain absent. WebSocket's binary send and receive bindings are a separate task, now that ArrayBuffer and typed arrays are available.
