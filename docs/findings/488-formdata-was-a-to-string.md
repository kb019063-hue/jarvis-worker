# 488. A FormData body was the string "[object FormData]"

Every body path that took a `FormData` stringified it: `extract_body_init`
(`fetch()` and `new Request()`, and `fetch(Request)`), the `Response`
constructor, and `XMLHttpRequest.send()` fell through to
`js_to_string(fd)` — the literal text `[object FormData]` — and XHR's send
installed the `text/plain;charset=UTF-8` default on top. No boundary, no
parts, no `multipart/form-data`: a page uploading a form by script sent the
server a four-word English sentence it could not parse.

Phase 488 serializes the entries per the Fetch Standard's
"multipart/form-data encoding algorithm" (RFC 7578): one boundary per body,
`Content-Disposition: form-data; name="…"` with `%` `"` CR LF percent-encoded
in names and filenames, `filename="blob"` for nameless Blobs and empty-named
Files, `Content-Type` from the Blob/File (`application/octet-stream` when it
carries none), CRLF line endings and the closing `--boundary--\r\n`. A
script-set `Content-Type` still wins everywhere; XHR installs the multipart
default only when the script set none. File bytes reach the wire through the
same `blob_bytes` the WebSocket and image paths use.

Verified by `tests/suites/488-formdata-bodies-are-multipart.cpp` (`make test`:
all 524 engine suites pass, this pull request adds the 524th) and by
`tools/conformance/verify_formdata_body.py`: the Litebird CLI POSTs to a raw
TCP loopback server — `CHECK:fetch=200`, `CHECK:xhr=200`, both requests
`PASS` on structure, part order, escaping, per-part `Content-Type`, and
`Content-Length`. Before the change the same suite prints
`content-type is multipart: not truthy` and the body as `"[object FormData]"`.
See [Phase 488](../phases/488-formdata-bodies-are-multipart.md).

Still open: streaming `ReadableStream` bodies, HTTP proxy support for
Fetch/XHR, and transcoding of filenames beyond the four percent-encoded
characters.
