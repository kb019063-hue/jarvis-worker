#!/usr/bin/env python3
"""End-to-end verification of Worker MIME/module policy, EventTarget dispatch,
and concurrent startup/exit stability against the real CLI binary.

Spins up a local loopback HTTP server returning JavaScript and non-JavaScript
Content-Type headers, verifies classic vs module worker options, error listener
delivery, importScripts MIME/origin boundaries, and runs a multi-process
concurrent startup/shutdown stress loop to catch exit-time SIGSEGV regressions.
"""
from __future__ import annotations

import argparse
import http.server
import pathlib
import subprocess
import sys
import tempfile
import threading
import time


class WorkerTestHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format: str, *args) -> None:
        return

    def do_GET(self) -> None:
        if self.path == "/yield":
            time.sleep(0.025)
            body = b"ok"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
            return
        routes: dict[str, tuple[int, str | None, bytes]] = {
            "/ok.js": (200, "text/javascript; charset=utf-8", b"postMessage('http-ok');"),
            "/helper.js": (200, "application/javascript", b"var IMPORTED_VALUE = 77;"),
            "/plain.js": (200, "text/plain", b"postMessage('MUST_NOT_RUN_PLAIN');"),
            "/html.js": (200, "text/html", b"postMessage('MUST_NOT_RUN_HTML');"),
            "/json.js": (200, "application/json", b"postMessage('MUST_NOT_RUN_JSON');"),
            "/no-ct.js": (200, None, b"postMessage('MUST_NOT_RUN_NO_CT');"),
            "/not-found.js": (404, "text/javascript", b"postMessage('MUST_NOT_RUN_404');"),
        }
        status, content_type, body = routes.get(
            self.path, (404, "text/plain", b"not found")
        )
        self.send_response(status)
        if content_type is not None:
            self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)


def run_cli(binary: str, args: list[str], timeout: float) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [binary, *args],
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def parse_checks(output: str) -> dict[str, str]:
    found: dict[str, str] = {}
    for line in output.splitlines():
        if line.startswith("WCHECK:"):
            k, _, v = line[len("WCHECK:"):].partition("=")
            found[k] = v
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default="build/litebird")
    ap.add_argument("--stress-runs", type=int, default=60)
    ap.add_argument("--timeout", type=float, default=30.0)
    args = ap.parse_args()

    binary = pathlib.Path(args.binary)
    if not binary.is_file():
        print(f"binary not found: {binary}", file=sys.stderr)
        return 2

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), WorkerTestHandler)
    port = server.server_address[1]
    base = f"http://127.0.0.1:{port}"
    t = threading.Thread(target=server.serve_forever, daemon=True)
    t.start()

    failures = 0
    checks = 0

    try:
        with tempfile.TemporaryDirectory(prefix="litebird-worker-policy-") as tmp:
            root = pathlib.Path(tmp)
            secret_js = root / "secret.js"
            secret_js.write_text("postMessage('LOCAL_LEAK');", encoding="utf-8")

            # 1. HTTP MIME policy + data: MIME policy + module policy + EventTarget error listeners
            html = f"""<!doctype html><meta charset="utf-8"><script>
var pending = 0;
function probe(name, url, opts) {{
  pending++;
  var settled = false;
  function settle() {{ if (!settled) {{ settled = true; pending--; }} }}
  var w = opts !== undefined ? new Worker(url, opts) : new Worker(url);
  w.onmessage = function(e) {{ console.log('WCHECK:msg_' + name + '=' + e.data); settle(); }};
  w.addEventListener('error', function(e) {{ console.log('WCHECK:err_' + name + '=' + e.message); settle(); }});
  return w;
}}
probe('http_ok', '{base}/ok.js');
probe('http_plain', '{base}/plain.js');
probe('http_html', '{base}/html.js');
probe('http_json', '{base}/json.js');
probe('http_noct', '{base}/no-ct.js');
probe('http_404', '{base}/not-found.js');
probe('data_js', 'data:text/javascript,postMessage("data-ok")');
probe('data_plain', 'data:text/plain,postMessage("BAD")');
probe('data_default', 'data:,postMessage("BAD")');
probe('module', '{base}/ok.js', {{ type: 'module', name: 'm' }});
probe('classic_name', 'data:text/javascript,postMessage(self.name)', {{ type: 'classic', name: 'w-alpha' }});
probe('import_ok', 'data:text/javascript,importScripts("{base}/helper.js"); postMessage(IMPORTED_VALUE);');
probe('import_bad_mime', 'data:text/javascript,try {{ importScripts("{base}/plain.js"); postMessage("BAD"); }} catch (e) {{ postMessage("BLOCKED:" + e.message); }}');
probe('import_local', 'data:text/javascript,try {{ importScripts("{secret_js}"); postMessage("BAD"); }} catch (e) {{ postMessage("BLOCKED_LOCAL"); }}');
try {{ new Worker('{base}/ok.js', {{ type: 'bogus' }}); }} catch (e) {{ console.log('WCHECK:invalid_type=' + e.name); }}
var attempts = 0;
function waitWorkers() {{
  if (pending === 0 || attempts >= 80) {{
    console.log('WCHECK:done=1');
    return;
  }}
  attempts++;
  fetch('{base}/yield').then(waitWorkers, waitWorkers);
}}
setTimeout(waitWorkers, 1);
</script>"""
            page_path = root / "policy.html"
            page_path.write_text(html, encoding="utf-8")
            proc = run_cli(
                str(binary),
                [str(page_path), f"--url={base}/policy.html", "--virtual-time-budget=2000"],
                args.timeout,
            )
            res = parse_checks(proc.stdout + "\n" + proc.stderr)

            def check(label: str, cond: bool) -> None:
                nonlocal checks, failures
                checks += 1
                print(f"{'PASS' if cond else 'FAIL'} {label}")
                if not cond:
                    failures += 1

            check("CLI policy page exited cleanly", proc.returncode == 0 and res.get("done") == "1")
            check("HTTP text/javascript executes", res.get("msg_http_ok") == "http-ok" and "err_http_ok" not in res)
            check("HTTP text/plain refused via error listener", "msg_http_plain" not in res and "text/plain" in res.get("err_http_plain", ""))
            check("HTTP text/html refused via error listener", "msg_http_html" not in res and "text/html" in res.get("err_http_html", ""))
            check("HTTP application/json refused via error listener", "msg_http_json" not in res and "application/json" in res.get("err_http_json", ""))
            check("HTTP missing Content-Type refused", "msg_http_noct" not in res and "missing Content-Type" in res.get("err_http_noct", ""))
            check("HTTP 404 refused even with JS MIME", "msg_http_404" not in res and "404" in res.get("err_http_404", ""))
            check("data:text/javascript executes", res.get("msg_data_js") == "data-ok")
            check("data:text/plain refused", "msg_data_plain" not in res and "text/plain" in res.get("err_data_plain", ""))
            check("data:, (default text/plain) refused", "msg_data_default" not in res and "text/plain" in res.get("err_data_default", ""))
            check("{type:'module'} refused instead of running classic", "msg_module" not in res and "Module workers" in res.get("err_module", ""))
            check("{type:'classic', name} exposes self.name", res.get("msg_classic_name") == "w-alpha")
            check("invalid WorkerOptions.type throws TypeError", res.get("invalid_type") == "TypeError")
            check("importScripts loads valid JS MIME", res.get("msg_import_ok") == "77")
            check("importScripts refuses non-JS MIME", res.get("msg_import_bad_mime", "").startswith("BLOCKED:"))
            check("importScripts enforces Phase 33 local-file boundary", res.get("msg_import_local") == "BLOCKED_LOCAL")

            # 2. Concurrent worker startup + immediate process exit stress test
            #    (repro for exit-time SIGSEGV when 4 workers are starting as main() returns).
            js_stress = 'for (var i = 0; i < 4; i++) new Worker("data:text/javascript,postMessage(1)");'
            stress_failures = 0
            for _ in range(args.stress_runs):
                sp = run_cli(str(binary), ["--run-js", js_stress], args.timeout)
                if sp.returncode != 0:
                    stress_failures += 1
                    print(f"  stress failure rc={sp.returncode}: {sp.stderr[:200]}")
                    if stress_failures >= 3:
                        break
            check(
                f"4x concurrent Worker startup/exit stability ({args.stress_runs - stress_failures}/{args.stress_runs})",
                stress_failures == 0,
            )
    finally:
        server.shutdown()
        server.server_close()

    print(f"\n{checks} checks; {failures} failure(s).")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
