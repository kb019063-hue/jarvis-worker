# Phase 150 — binary response bodies return ArrayBuffers

Task row: **The four interfaces typed arrays unblocked** in `AGENTS.md`. Finding 155.

## 1. What was wrong

`ArrayBuffer` and the typed-array views have existed since Phase 106, but the body APIs did not connect to them:

- Fetch `Response` instances had `text()` and `json()`, but no `arrayBuffer()`.
- `Blob` had `text()`, but no `arrayBuffer()`.
- `XMLHttpRequest` accepted `responseType = "arraybuffer"`, but its response factory treated that type as unsupported and returned `null`.

The bytes were already present in C++ strings. Decoding them as text is not a substitute: it can replace invalid sequences and cannot preserve arbitrary binary data as bytes.

## 2. What changed

The three paths now create a `Uint8Array` view over a copy of the stored bytes using the existing `typed_array_from_bytes()` helper, then return that view's `buffer`:

- `Response.arrayBuffer()` returns a promise resolving to an `ArrayBuffer`, for both `new Response(...)` and fetched responses.
- `Blob.arrayBuffer()` returns a promise resolving to a fresh `ArrayBuffer` copy of the Blob's immutable bytes.
- `XMLHttpRequest.responseType = "arraybuffer"` exposes and caches an `ArrayBuffer` containing the response bytes instead of returning `null`.

No text decoding or re-encoding is performed in these paths. An embedded NUL and the UTF-8 bytes for `é` are preserved as `65, 0, 195, 169`.

## 3. How it was verified

`make test` passes **308 engine suites** and the **114-check** cross-origin Window/Location/channel suite. The regression coverage is in `fetch_api_and_headers` and `xml_http_request_answers_data_urls_and_nothing_else`; it checks that the results are actual `ArrayBuffer` objects and reads back every byte through `Uint8Array` for Fetch, a constructed `Response`, a `Blob`, and XHR.

The same build reports `make check-numbers` with the 308 registered suites matching the README count; `python3 tools/ci/check_docs.py` validates the documentation layout.

## 4. What is still open

- `Response` does not yet implement the standard's stream/body-consumption model (`body`, `bodyUsed`, or one-shot reads); the existing text and JSON methods remain repeatable too.
- `Response` construction still treats its body as text rather than accepting `ArrayBuffer`, typed-array, Blob, or stream bodies. The current Fetch response path preserves the network bytes it already receives.
- Blob `stream()` and BufferSource parts are not implemented.
- WebSocket cannot yet send `ArrayBuffer`/typed-array/Blob data or expose received binary frames under `binaryType = "arraybuffer"`; that remains a separate binding and framing task.
