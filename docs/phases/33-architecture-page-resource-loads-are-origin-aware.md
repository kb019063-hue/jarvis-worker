# Phase 33 Architecture: Page Resource Loads Are Origin-Aware

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 33 Architecture: Page Resource Loads Are Origin-Aware` section.
> The text below is the section as it stood, unchanged.


### 1. What was wrong

`ResourceLoader::load` treated any URL that did not begin with `data:` or
`http://` as a path on this machine. `FetchAPI::fetch` called it with the
script's string and no document context, so a page loaded from
`https://example.test/` could read `/home/user/notes.txt`, `file:///…`, or a
relative path that happened to name a local file. `WorkerObject` had the same
loader and, when loading failed, returned the URL/path itself as script source.
The loader was not told where the page came from.

### 2. The boundary

`ResourceLoader.hpp` now has two entry points:

- `load_for_page(url, context, …)` is the only page-initiated path. The
  `ResourceRequestContext` carries `document_url` (the capability) and
  `base_url` (relative resolution only). A `<base href>` may change the base,
  but it cannot change the document's origin. The URL is parsed with the
  WHATWG parser before dispatch; there is no string-prefix fallback to disk.
- `load_navigation(url)` is embedder-only (CLI/TUI address bar, history,
  reload). A bare path is a local file there. It is not exposed to script.

Policy in `load_for_page`:

| Document address | Request | Result |
| :--- | :--- | :--- |
| `http:` / `https:` | `data:` | allowed; self-contained |
| `http:` / `https:` | resolved `http:` | network loader (HTTPS target is not implemented) |
| `http:` / `https:` | `file:`, local path, other scheme | refused before disk/network |
| `file:` | `file:` / relative file | allowed; local CLI/TUI capability |
| `about:blank`, empty/invalid | `data:` only | opaque context |

`data:` goes through the Fetch Standard processor already used by
`XMLHttpRequest` (`process_data_url`), so percent-encoding and forgiving
base64 are shared. `ResourceResponse::url` records the resolved address
without a fragment, and `fetch()` reports it as `Response.url`.

### 3. Callers

`JsInterpreter` installs `fetch` and `Worker` with a context provider read at
call time: the current document URL and the document's `<base href>`. An
alive guard keeps a retained native closure from touching a destroyed realm.
The bytecode `VM` uses its document address as the context. A worker inherits
the creator's capability; a classic worker's base URL becomes its loaded
script URL, while a `data:` worker keeps the creator's base. A failed worker
load posts an error event instead of executing the refused path as source.
`BrowserShell` navigation/history/reload use `load_navigation`.

### 4. Verification

`make test` passes 132 suites. The three new suites are
`resource_loader_page_requests_are_origin_aware`,
`fetch_and_workers_use_the_document_origin`, and
`fetch_relative_requests_use_the_document_base_url` (a loopback server proves
`../secret.txt` from a web document goes to the resolved HTTP path, not disk).
`make security-fetch` writes a marker file and runs the CLI against generated
pages: web document, web document with a `file:` base, and local `file:`
document. It prints `3 checks; 0 failure(s).` The full release run also kept
html5lib 1781/1792, JS 840/845, DOM 629/629, URL 1150/1188, XML 1702/1702,
WPT 4640/5192, diff-trees/diff-xml 0 differing, deep 1332 runs/0 failed,
ASan 132/132 (JS 839/845), and ASan deep 882 runs/0 failed. Outside the DOM
corpus, `tools/conformance/compare_with_jsdom.py` on a generated base/history
probe printed `4 lines, 4 the same` before and after; jsdom 31 has no
fetch/Worker, so the resource policy itself is checked by the unit suites and
`make security-fetch`, not recorded as a jsdom expectation.

Repro:

```bash
printf 'LOCAL_SECRET\n' > /tmp/secret.txt
printf '<script>fetch("/tmp/secret.txt").then(r=>r.text()).then(console.log, e=>console.log("ERR:"+e));</script>' > /tmp/page.html
./build/litebird /tmp/page.html --url=https://example.test/page.html
make security-fetch
```

Before the change the web page printed `LOCAL_SECRET`; after it the request is
refused/resolved as a web URL and the marker is not printed.

### 5. What is still open

This is the local-file disclosure boundary, not a complete Fetch/XHR network
stack. Phase 36 subsequently added `https:` (via runtime `dlopen` of OpenSSL 3),
HTTP/1.1 chunked + gzip/deflate decoding, CORS enforcement for cross-origin
`fetch()` and `XMLHttpRequest`, external `<script src>` / `<link rel="stylesheet">`,
and origin-checked `<img src>` loading (Finding 44). Still open: CORS preflight
`OPTIONS` requests and credentialed CORS cookies, `blob:`/`about:` URL fetching,
and worker script MIME/module policy (`Worker.cpp`). A `file:` document is still
treated as a local embedder context and can read local files, so opening
untrusted local HTML remains a trust decision.

---
