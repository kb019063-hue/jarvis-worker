# Phase 488 — FormData bodies reach the wire as multipart/form-data

## What was wrong

Every body path that took a `FormData` turned it into a string. In
`src/net/Fetch.cpp`, `extract_body_init` — used by `fetch()`, by
`new Request(input, init)`, and by `fetch(request)` — handled `ArrayBuffer`/
`ArrayBufferView`, `Blob` and `URLSearchParams`, and everything else fell
through to `js_to_string(body_val)`. A `FormData` answered the global
`Object.prototype.toString` question: the literal text
`[object FormData]`. A script that uploaded a form reached the server with

```
Content-Type: text/plain;charset=UTF-8

[object FormData]
```

`XMLHttpRequest.send(fd)` had its own copy of the same hole in
`src/js/DomWindowBindings.cpp` (`state.request_body = to_str(args[0])` with the
same `text/plain;charset=UTF-8` default it installs for any non-GET/HEAD
body), and the `Response` constructor in `Fetch.cpp` stringified the first
argument the same way. No boundary was ever chosen; no part was ever emitted.

## What changed

One serializer, in `src/js/DomFormBindings.cpp`, next to the `FormDataState`
it reads: `form_data_body_init(value, bytes_out, content_type_out)`, declared
in `include/litebird/js/DomBindings.hpp` alongside `is_blob`/`is_file`/
`blob_bytes`, together with a predicate `is_form_data` that reads the hidden
`JsExotic` state rather than anything a script can set. It writes the Fetch
Standard's "multipart/form-data encoding algorithm" (RFC 7578):

- one boundary per serialized body, from eighteen OS-random bytes
  (`/dev/urandom`), hex-encoded with a fixed `----LitebirdFormData` prefix —
  38 characters, far under the 70 RFC 7578 allows, chosen per body rather
  than baked in or derived from the content;
- for each entry, in order: `--boundary`, CRLF,
  `Content-Disposition: form-data; name="…"`, then either
  - a string value: CRLF, an empty line, the value, CRLF; or
  - a `Blob`/`File` value: `; filename="…"` on the same line, CRLF,
    `Content-Type: <type|application/octet-stream>`, CRLF, an empty line, the
    blob's bytes, CRLF;
- the name and the filename are percent-encoded before they are quoted
  (`%` → `%25`, `"` → `%22`, CR → `%0D`, LF → `%0A`);
- `filename` is the File's name, or `blob` for a nameless Blob or a
  File with an empty name (what Chrome and Firefox send);
- the body ends with `--boundary--\r\n`, and the content type handed back is
  `multipart/form-data; boundary=…` (no space, as browsers send it).

Wire-up, three call sites (and the workers, which share `FetchAPI::fetch`,
get it for free):

1. `extract_body_init` (`src/net/Fetch.cpp`): `FormData` is serialized
   before the string fallback, and the `content-type` header is still filled
   from the body's MIME type unless the script set one — so `fetch()`,
   `fetch(Request)` and `new Request(url, {body: fd})` all send the
   multipart bytes with the boundary in the header.
2. The `Response` constructor (`src/net/Fetch.cpp`): a `FormData` body
   serializes the same way, and the response gains a
   `content-type: multipart/form-data; boundary=…` header unless the script
   set one in `init.headers`.
3. `XMLHttpRequest.send()` (`src/js/DomWindowBindings.cpp`): a `FormData`
   first argument serializes the same way; the `Content-Type` the send path
   installs by default (and only when the script set none) is the multipart
   value with the boundary rather than `text/plain;charset=UTF-8`. An
   explicit script `Content-Type` wins in all three places.

## Evidence

Before the fix, the suite below prints the old answer:

```
formdata_bodies_are_multipart_form_data... content-type is multipart: not truthy
starts with the boundary: got "[object FormData]", want "--<boundary>\r\n"
```

(run with the fix stashed; built and re-run to be sure). After it:

```
Running test: formdata_bodies_are_multipart_form_data... PASSED
All 532 engine test suites passed (after merging origin/main).
```

`python3 tools/conformance/verify_formdata_body.py --binary build/litebird`
starts a raw TCP loopback server, loads generated pages in the CLI, and
inspects the exact bytes of both POSTs:

```
CHECK:fetch=200
CHECK:xhr=200
PASS fetch
PASS xhr
PASS FormData bodies arrive as multipart/form-data (fetch and XMLHttpRequest)
```

It checks the method, the multipart content type, the boundary's shape, that
`Content-Length` equals the body's byte count, that the body opens and closes
with the boundary, the names/filenames/`Content-Type`s of the three parts
(plain string, File with type, nameless Blob), the file's bytes, and that
`name=`, `filename=` and CR/LF are percent-encoded.
The suite also pins: the per-body randomness of the boundary, the `%22`/`%0D`/
`%0A`/`%25` escaping, part order, `filename="blob"`, the
`application/octet-stream` fallback, an explicit script `Content-Type`
winning (as `text/csv`), the ordinary string-body default
(`text/plain;charset=UTF-8`), and the `Response` constructor's serialization
and header.

`python3 tools/ci/check_suites.py` ok own-file suites, including
`tests/suites/488-formdata-bodies-are-multipart.cpp`.

Not measured against the wire for `file:` pages hitting non-loopback hosts
(they are blocked by policy, not by this change), and not re-fuzzed.

## Still open

- Streaming `ReadableStream` bodies (a `FormData` itself is fully buffered
  here), and HTTP proxy support for `fetch()`/XHR — both listed as open in
  `ROADMAP.md`.
- Filename bytes beyond ASCII are emitted as the UTF-8 bytes they are, with
  the four characters percent-encoded; RFC 7578 leaves the encoding of
  non-ASCII names to stack-specific conventions (RFC 5987-style
  `filename*=` is what browsers converged on for headers, not for
  multipart), and this engine does not transcode.
- HTML form *navigation* (`<form enctype=multipart/form-data>` submitted by a
  user action) does not build a multipart body either; that is the
  submission path, not the script path, and is a separate row.
- A File with an empty name serializes as `filename="blob"`, matching
  Chromium and Firefox; the spec's wording was not re-audited for this edge.
