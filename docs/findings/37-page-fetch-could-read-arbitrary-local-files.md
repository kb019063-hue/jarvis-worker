# 37. ✅ Page fetch could read arbitrary local files

> Moved out of `AUDIT.md` — written there as a `## 37. ✅ Page fetch could read arbitrary local files` section.
> The text below is the section as it stood, unchanged.


**The hole.** `ResourceLoader::load` took any string that did not start with
`data:` or `http://` as a filesystem path. `fetch()` passed the script's URL
straight to it without the document's address, so a page at
`https://example.test/` could read `/home/user/notes.txt` or `file:///…`.
`new Worker()` used the same loader and, on failure, returned the path itself
as inline script. The loader was not told where the page came from.

**The fix.** Page loads now require a `ResourceRequestContext`
(`document_url` for capability, `base_url` for resolution) and go through
`ResourceLoader::load_for_page`. URLs are parsed before dispatch. Web
documents cannot read `file:` URLs or local paths; a `<base href="file:///…">`
does not grant file access. Opaque/empty contexts are `data:` only. Documents
the embedder loaded from `file:` keep local-file access for the CLI/TUI.
Trusted navigation is a separate `load_navigation` entry point used by the
shell. `data:` uses the standard processor already shared with XHR. Worker
loads inherit the creator's context; a failed load is an error event, not
source. `Response.url` is the resolved address without a fragment.

**How verified.** `make test` passes 132 suites, including
`resource_loader_page_requests_are_origin_aware`,
`fetch_and_workers_use_the_document_origin`, and
`fetch_relative_requests_use_the_document_base_url`. `make security-fetch`
generates a marker file and prints `3 checks; 0 failure(s).` Repro:
`printf 'LOCAL_SECRET\n' > /tmp/secret.txt; printf '<script>fetch("/tmp/secret.txt").then(r=>r.text()).then(console.log, e=>console.log("ERR:"+e));</script>' > /tmp/page.html; ./build/litebird /tmp/page.html --url=https://example.test/page.html`.
Before: `LOCAL_SECRET`. After: no marker; the request is refused or resolved
as a web URL. The conformance scoreboards are unchanged (html5lib 1781/1792,
JS 840/845, DOM 629/629, URL 1150/1188, XML 1702/1702, WPT 4640/5192);
diff-trees/diff-xml have 0 differing, deep has 1332 runs/0 failed, ASan has
132/132 suites and 882 deep runs/0 failed. A generated base/history probe run
through `compare_with_jsdom.py` printed `4 lines, 4 the same` before/after.

**What is still open.** CORS/same-origin network policy and
`XMLHttpRequest` over the network are the next row; `https:` is not
implemented; `blob:`/`about:`/custom schemes are not fetched; cross-origin
HTTP `fetch()` is not CORS-checked. A `file:` document is a local embedder
context and can read local files, so opening untrusted local HTML remains a
trust decision. Worker MIME checks, module workers, and external
`<script src>`/`<link>` loading are unchanged.
---
