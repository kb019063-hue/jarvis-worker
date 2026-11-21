#!/usr/bin/env python3
"""Differential conformance verifier for WHATWG Fetch Request and fetch(Request, init).

Runs identical JavaScript probes against Node.js (undici WHATWG Fetch) and
Litebird (`build/litebird --run-js`), verifying exact parity on:
  - Request constructor defaults, prototype/instanceof/Symbol.toStringTag
  - URL parsing, normalization, URL object input, credentials rejection
  - Method normalization, forbidden methods, invalid tokens, no-cors methods
  - RequestInit validation (mode, credentials, cache, only-if-cached, redirect,
    referrer, referrerPolicy, window, integrity, keepalive, signal)
  - Headers init forms, header replacement, Headers instanceof
  - Body extraction (string, URLSearchParams, Blob, ArrayBuffer, Uint8Array,
    DataView), default Content-Type, GET/HEAD rejection, bodyUsed disturbance
  - clone() isolation and body/signal propagation
  - Body consumers: text(), json(), arrayBuffer(), bytes(), blob()
  - AbortSignal linking, unlinking (signal: null), and abort propagation
  - End-to-end loopback HTTP fetch(Request) and fetch(Request, init)

Also checks browser-only Fetch security invariants in Litebird:
  - Forbidden request header guard (Host, Origin, Cookie, Sec-*, Proxy-*)
  - no-cors header safelist guard
  - Relative URL resolution against document URL and <base href>

Run: python3 tools/conformance/verify_fetch_request.py
"""
import argparse
import http.server
import json
import os
import socketserver
import subprocess
import sys
import tempfile
import threading

HELPERS = r"""
function expectThrow(fn) {
  try {
    fn();
    return "NO_THROW";
  } catch (e) {
    return e && e.name ? e.name : String(e);
  }
}
"""

DIFF_CASES = {
    "defaults_and_identity": HELPERS + r"""
const r = new Request("https://example.com/a/b?x=1");
console.log(JSON.stringify({
  typeofCtor: typeof Request,
  name: Request.name,
  length: Request.length,
  instanceofReq: r instanceof Request,
  instanceofHeaders: r.headers instanceof Headers,
  instanceofSignal: r.signal instanceof AbortSignal,
  ctorRef: Request.prototype.constructor === Request,
  tag: Object.prototype.toString.call(r),
  sameHeaders: r.headers === r.headers,
  sameSignal: r.signal === r.signal,
  url: r.url,
  method: r.method,
  mode: r.mode,
  credentials: r.credentials,
  cache: r.cache,
  redirect: r.redirect,
  referrer: r.referrer,
  referrerPolicy: r.referrerPolicy,
  integrity: r.integrity,
  keepalive: r.keepalive,
  destination: r.destination,
  bodyUsed: r.bodyUsed,
  signalAborted: r.signal.aborted,
}));
""",
    "url_inputs_and_credentials": HELPERS + r"""
const u = new URL("https://example.com/path/../norm?k=v#frag");
const r1 = new Request(u);
const r2 = new Request({ toString() { return "https://example.com/custom"; } });
console.log(JSON.stringify({
  fromUrl: r1.url,
  fromToString: r2.url,
  noNew: expectThrow(() => Request("https://example.com/")),
  noArgs: expectThrow(() => new Request()),
  badUrl: expectThrow(() => new Request("://not a url")),
  relNoBase: expectThrow(() => new Request("/relative/only")),
  credsUserPass: expectThrow(() => new Request("https://user:pass@example.com/")),
  credsUserOnly: expectThrow(() => new Request("https://user@example.com/")),
  badInit: expectThrow(() => new Request("https://example.com/", "nope")),
}));
""",
    "methods_and_validation": HELPERS + r"""
const normalized = ["get", "post", "put", "delete", "head", "options", "patch", "PROPFIND", "custom-method"]
  .map(m => new Request("https://example.com/", { method: m }).method);
const rejected = ["CONNECT", "connect", "TRACE", "trace", "TRACK", "track", "", "BAD METHOD", "GET\r\nX"]
  .map(m => expectThrow(() => new Request("https://example.com/", { method: m })));
console.log(JSON.stringify({ normalized, rejected }));
""",
    "init_options_and_constraints": HELPERS + r"""
const r = new Request("https://example.com/api", {
  method: "post",
  mode: "same-origin",
  credentials: "include",
  cache: "only-if-cached",
  redirect: "manual",
  referrer: "https://example.com/from",
  referrerPolicy: "strict-origin",
  integrity: "sha256-abc",
  keepalive: true,
  window: null,
});
const rNoRef = new Request("https://example.com/", { referrer: "" });
console.log(JSON.stringify({
  ok: {
    method: r.method, mode: r.mode, credentials: r.credentials, cache: r.cache,
    redirect: r.redirect, referrer: r.referrer, referrerPolicy: r.referrerPolicy,
    integrity: r.integrity, keepalive: r.keepalive, noRef: rNoRef.referrer,
  },
  errors: {
    windowObj: expectThrow(() => new Request("https://example.com/", { window: {} })),
    modeNav: expectThrow(() => new Request("https://example.com/", { mode: "navigate" })),
    modeBad: expectThrow(() => new Request("https://example.com/", { mode: "bogus" })),
    credBad: expectThrow(() => new Request("https://example.com/", { credentials: "bogus" })),
    cacheBad: expectThrow(() => new Request("https://example.com/", { cache: "bogus" })),
    onlyIfCachedCors: expectThrow(() => new Request("https://example.com/", { cache: "only-if-cached" })),
    redirBad: expectThrow(() => new Request("https://example.com/", { redirect: "bogus" })),
    rpBad: expectThrow(() => new Request("https://example.com/", { referrerPolicy: "bogus" })),
    noCorsPut: expectThrow(() => new Request("https://example.com/", { mode: "no-cors", method: "PUT" })),
    noCorsDelete: expectThrow(() => new Request("https://example.com/", { mode: "no-cors", method: "DELETE" })),
    noCorsPatch: expectThrow(() => new Request("https://example.com/", { mode: "no-cors", method: "patch" })),
    noCorsPostOk: new Request("https://example.com/", { mode: "no-cors", method: "POST" }).method,
  },
}));
""",
    "headers_init_and_override": HELPERS + r"""
const h = new Headers([["X-A", "1"], ["X-A", "2"], ["X-B", "3"]]);
const r1 = new Request("https://example.com/", { headers: h });
const r2 = new Request("https://example.com/", { headers: [["X-Pair", "p1"], ["X-Pair", "p2"]] });
const r3 = new Request(r1, { headers: { "X-New": "only" } });
const r4 = new Request(r1);
r4.headers.set("X-Mut", "yes");
console.log(JSON.stringify({
  r1A: r1.headers.get("x-a"),
  r1B: r1.headers.get("x-b"),
  r2Pair: r2.headers.get("x-pair"),
  r3New: r3.headers.get("x-new"),
  r3OldGone: r3.headers.get("x-a"),
  r1Isolated: r1.headers.get("x-mut"),
  badHeaderName: expectThrow(() => new Request("https://example.com/", { headers: { "bad name": "1" } })),
  badPairLen: expectThrow(() => new Request("https://example.com/", { headers: [["only-one"]] })),
  badHeadersType: expectThrow(() => new Request("https://example.com/", { headers: 123 })),
}));
""",
    "body_types_content_type_and_consumers": HELPERS + r"""
(async () => {
  const rStr = new Request("https://example.com/", { method: "POST", body: "hello" });
  const rSp = new Request("https://example.com/", { method: "POST", body: new URLSearchParams("a=1&b=two") });
  const rBlob = new Request("https://example.com/", { method: "POST", body: new Blob(["blob-data"], { type: "text/csv" }) });
  const bytes = new Uint8Array([0, 1, 65, 255]);
  const rU8 = new Request("https://example.com/", { method: "POST", body: bytes });
  const rDv = new Request("https://example.com/", { method: "POST", body: new DataView(bytes.buffer, 1, 2) });
  const rExplicit = new Request("https://example.com/", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: '{"k":42}',
  });
  const rEmpty = new Request("https://example.com/");

  const strText = await rStr.text();
  let secondReadErr = "NO_ERR";
  try { await rStr.text(); } catch (e) { secondReadErr = e.name; }

  const spText = await rSp.text();
  const blobOut = await rBlob.blob();
  const ab = await rU8.arrayBuffer();
  const dvBytes = Array.from(await rDv.bytes());
  const jsonObj = await rExplicit.json();
  const emptyText = await rEmpty.text();
  const emptyText2 = await rEmpty.text();

  const rBadJson = new Request("https://example.com/", { method: "POST", body: "{not json" });
  let badJsonErr = "NO_ERR";
  try { await rBadJson.json(); } catch (e) { badJsonErr = e.name; }

  let illegalThisErr = "NO_ERR";
  try { await Request.prototype.text.call({}); } catch (e) { illegalThisErr = e.name; }

  console.log(JSON.stringify({
    ctStr: rStr.headers.get("content-type"),
    ctSp: rSp.headers.get("content-type"),
    ctBlob: rBlob.headers.get("content-type"),
    ctU8: rU8.headers.get("content-type"),
    ctExplicit: rExplicit.headers.get("content-type"),
    strText,
    strUsed: rStr.bodyUsed,
    secondReadErr,
    spText,
    blobSize: blobOut.size,
    blobType: blobOut.type,
    abBytes: Array.from(new Uint8Array(ab)),
    dvBytes,
    jsonK: jsonObj.k,
    emptyText,
    emptyText2,
    emptyUsed: rEmpty.bodyUsed,
    badJsonErr,
    illegalThisErr,
    getBodyErr: expectThrow(() => new Request("https://example.com/", { method: "GET", body: "x" })),
    headBodyErr: expectThrow(() => new Request("https://example.com/", { method: "HEAD", body: "x" })),
  }));
})();
""",
    "request_from_request_clone_and_disturbed": HELPERS + r"""
(async () => {
  const a = new Request("https://example.com/items", {
    method: "POST",
    headers: { "X-Tag": "orig" },
    body: "payload-1",
  });
  const c1 = a.clone();
  c1.headers.set("X-Tag", "cloned");

  // Replacing body leaves source undisturbed
  const replaced = new Request(a, { body: "payload-2" });
  const aUsedAfterReplace = a.bodyUsed;

  // Inheriting body disturbs source
  const inherited = new Request(a);
  const aUsedAfterInherit = a.bodyUsed;
  const reuseErr = expectThrow(() => new Request(a));
  const cloneUsedErr = expectThrow(() => a.clone());

  // Switching used/post request to GET still rejects because body is inherited
  const postToGetErr = expectThrow(() => new Request(c1, { method: "GET" }));
  const postToGetNullErr = expectThrow(() => new Request(c1, { method: "GET", body: null }));
  // Reusing disturbed request with brand-new body succeeds
  const freshFromUsed = new Request(a, { body: "fresh" });

  console.log(JSON.stringify({
    aUsedAfterReplace,
    aUsedAfterInherit,
    reuseErr,
    cloneUsedErr,
    postToGetErr,
    postToGetNullErr,
    c1Tag: c1.headers.get("x-tag"),
    aTag: a.headers.get("x-tag"),
    c1Text: await c1.text(),
    replacedText: await replaced.text(),
    inheritedText: await inherited.text(),
    freshText: await freshFromUsed.text(),
  }));
})();
""",
    "abort_signal_linking": HELPERS + r"""
const ac1 = new AbortController();
const r1 = new Request("https://example.com/", { signal: ac1.signal });
const r2 = new Request(r1);
const rUnlinked = new Request(r1, { signal: null });
const rClone = r1.clone();

const before = [r1.signal !== ac1.signal, r1.signal.aborted, r2.signal.aborted, rClone.signal.aborted, rUnlinked.signal.aborted];
ac1.abort("stop-now");
const after = [r1.signal.aborted, r1.signal.reason, r2.signal.aborted, r2.signal.reason, rClone.signal.aborted, rClone.signal.reason, rUnlinked.signal.aborted];

const ac2 = new AbortController();
ac2.abort("pre-aborted");
const rPre = new Request("https://example.com/", { signal: ac2.signal });

console.log(JSON.stringify({
  before,
  after,
  preAborted: rPre.signal.aborted,
  preReason: rPre.signal.reason,
  badSignal: expectThrow(() => new Request("https://example.com/", { signal: {} })),
}));
""",
}


class EchoHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def _echo(self):
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length > 0 else b""
        if self.path == "/page.html":
            html = (
                "<!doctype html><html><head><base href='http://127.0.0.1:"
                + str(self.server.server_address[1])
                + "/sub/base/'></head><body><div id='out'></div><script>"
                "const r1 = new Request('rel?k=1');"
                "const r2 = new Request('../up');"
                "const rGuard = new Request('/g', { headers: { 'Host': 'evil.test', 'Origin': 'https://evil.test', 'Cookie': 'a=1', 'Sec-Fetch-Mode': 'cors', 'Proxy-Auth': 'x', 'X-Ok': 'yes' } });"
                "const rNoCors = new Request('/nc', { mode: 'no-cors', headers: { 'Accept': 'text/plain', 'X-Custom': 'drop', 'Content-Type': 'application/json' } });"
                "const rNoCorsOk = new Request('/nc2', { mode: 'no-cors', headers: { 'Content-Type': 'text/plain; charset=utf-8' } });"
                "document.getElementById('out').textContent = JSON.stringify({"
                "  r1: r1.url, r2: r2.url,"
                "  guardHost: rGuard.headers.get('host'),"
                "  guardOrigin: rGuard.headers.get('origin'),"
                "  guardCookie: rGuard.headers.get('cookie'),"
                "  guardSec: rGuard.headers.get('sec-fetch-mode'),"
                "  guardProxy: rGuard.headers.get('proxy-auth'),"
                "  guardOk: rGuard.headers.get('x-ok'),"
                "  ncAccept: rNoCors.headers.get('accept'),"
                "  ncCustom: rNoCors.headers.get('x-custom'),"
                "  ncCtJson: rNoCors.headers.get('content-type'),"
                "  ncCtPlain: rNoCorsOk.headers.get('content-type')"
                "});"
                "</script></body></html>"
            ).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(html)))
            self.end_headers()
            self.wfile.write(html)
            return

        if self.path == "/fetch-test.html":
            base = f"http://127.0.0.1:{self.server.server_address[1]}"
            script = build_fetch_loopback_case(base, dom_output=True)
            html = (
                "<!doctype html><html><body><div id='out'></div><script>"
                + script
                + "</script></body></html>"
            ).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(html)))
            self.end_headers()
            self.wfile.write(html)
            return

        payload = json.dumps({
            "method": self.command,
            "path": self.path,
            "xToken": self.headers.get("X-Token"),
            "xOverride": self.headers.get("X-Override"),
            "ct": self.headers.get("Content-Type"),
            "bodyHex": raw.hex(),
            "bodyText": raw.decode("utf-8", errors="replace"),
        }, sort_keys=True).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        self._echo()

    def do_POST(self):
        self._echo()

    def do_PUT(self):
        self._echo()


class ThreadedTCPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def build_fetch_loopback_case(base_url: str, dom_output: bool = False) -> str:
    emit = (
        "document.getElementById('out').textContent = JSON.stringify(result);"
        if dom_output
        else "console.log(JSON.stringify(result));"
    )
    return f"""
(async () => {{
  const req1 = new Request("{base_url}/echo1", {{
    method: "POST",
    headers: {{ "X-Token": "tok-1" }},
    body: "hello-request",
  }});
  const res1 = await fetch(req1);
  const j1 = await res1.json();
  const usedAfterFetch = req1.bodyUsed;

  let reuseReject = "NO_ERR";
  try {{ await fetch(req1); }} catch (e) {{ reuseReject = e.name; }}

  const req2 = new Request("{base_url}/echo2", {{
    method: "POST",
    headers: {{ "X-Token": "old" }},
    body: "old-body",
  }});
  const res2 = await fetch(req2, {{
    method: "PUT",
    headers: {{ "X-Override": "yes" }},
    body: new Uint8Array([0, 65, 66, 255]),
  }});
  const j2 = await res2.json();
  const req2UsedAfterOverride = req2.bodyUsed;

  const ac = new AbortController();
  const reqAbort = new Request("{base_url}/aborted", {{ signal: ac.signal }});
  ac.abort("custom-abort-reason");
  let abortReason = "NO_ERR";
  try {{ await fetch(reqAbort); }} catch (e) {{ abortReason = String(e); }}

  const result = {{
    res1Instanceof: res1 instanceof Response,
    j1,
    usedAfterFetch,
    reuseReject,
    j2Method: j2.method,
    j2Path: j2.path,
    j2Token: j2.xToken,
    j2Override: j2.xOverride,
    j2Hex: j2.bodyHex,
    req2UsedAfterOverride,
    abortReason,
  }};
  {emit}
}})();
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="build/litebird")
    parser.add_argument("--node", default="node")
    args = parser.parse_args()

    srv = ThreadedTCPServer(("127.0.0.1", 0), EchoHandler)
    port = srv.server_address[1]
    thread = threading.Thread(target=srv.serve_forever, daemon=True)
    thread.start()

    failures = 0
    try:
        for name, source in DIFF_CASES.items():
            try:
                node = subprocess.run([args.node, "-e", source], capture_output=True, text=True, timeout=20)
                bird = subprocess.run([args.binary, "--run-js", source], capture_output=True, text=True, timeout=20)
            except (OSError, subprocess.TimeoutExpired) as err:
                print(f"FAIL {name}: {err}")
                failures += 1
                continue
            if node.returncode or bird.returncode or node.stdout != bird.stdout:
                print(
                    f"FAIL {name}\n"
                    f"  node ({node.returncode}): {node.stdout.strip()!r} {node.stderr.strip()[:300]!r}\n"
                    f"  litebird ({bird.returncode}): {bird.stdout.strip()!r} {bird.stderr.strip()[:300]!r}"
                )
                failures += 1
            else:
                print(f"PASS {name}")

        # End-to-end loopback HTTP fetch(Request) & fetch(Request, init) parity with Node.js.
        node_fetch_src = build_fetch_loopback_case(f"http://127.0.0.1:{port}", dom_output=False)
        node_fetch = subprocess.run([args.node, "-e", node_fetch_src], capture_output=True, text=True, timeout=20)
        bird_fetch = subprocess.run(
            [args.binary, "--dump-dom", f"http://127.0.0.1:{port}/fetch-test.html"],
            capture_output=True, text=True, timeout=20,
        )
        node_json = node_fetch.stdout.strip()
        if node_fetch.returncode == 0 and bird_fetch.returncode == 0 and node_json and node_json in bird_fetch.stdout:
            print("PASS fetch_loopback_request_and_init")
        else:
            print(
                f"FAIL fetch_loopback_request_and_init\n"
                f"  node ({node_fetch.returncode}): {node_json!r} {node_fetch.stderr.strip()[:300]!r}\n"
                f"  litebird ({bird_fetch.returncode}): {bird_fetch.stdout.strip()[:500]!r} {bird_fetch.stderr.strip()[:300]!r}"
            )
            failures += 1

        # Browser-specific Fetch security & <base href> resolution check in Litebird.
        page_url = f"http://127.0.0.1:{port}/page.html"
        bird_page = subprocess.run([args.binary, "--dump-dom", page_url], capture_output=True, text=True, timeout=20)
        expected = {
            "r1": f"http://127.0.0.1:{port}/sub/base/rel?k=1",
            "r2": f"http://127.0.0.1:{port}/sub/up",
            "guardHost": None,
            "guardOrigin": None,
            "guardCookie": None,
            "guardSec": None,
            "guardProxy": None,
            "guardOk": "yes",
            "ncAccept": "text/plain",
            "ncCustom": None,
            "ncCtJson": None,
            "ncCtPlain": "text/plain; charset=utf-8",
        }
        expected_json = json.dumps(expected, separators=(",", ":"))
        if bird_page.returncode == 0 and expected_json in bird_page.stdout:
            print("PASS browser_security_guards_and_base_href")
        else:
            print(
                f"FAIL browser_security_guards_and_base_href\n"
                f"  expected substring: {expected_json}\n"
                f"  got ({bird_page.returncode}): {bird_page.stdout.strip()[:500]!r} {bird_page.stderr.strip()[:300]!r}"
            )
            failures += 1
    finally:
        srv.shutdown()
        srv.server_close()

    total = len(DIFF_CASES) + 2
    print(f"{total - failures}/{total} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
