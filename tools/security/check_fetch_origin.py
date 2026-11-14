#!/usr/bin/env python3
"""Reproducible check that page fetch() cannot read arbitrary local files.

Runs the CLI against generated pages with a marker file on disk. A web
document must not read the marker by absolute path, file: URL, or relative
reference; a document the embedder loaded from file: keeps local access.
No network is needed: the web document uses https://example.test, and the
https scheme is not implemented, so a resolved web reference fails without
leaving the machine.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

MARKER = "LOCAL_FILE_MARKER_37"


def page(requests: list[tuple[str, str]], base: str | None = None) -> str:
    head = f'<base href="{base}">' if base else ""
    calls = ",\n".join(
        f"check({json.dumps(name)}, {json.dumps(url)})" for name, url in requests
    )
    return f"""<!doctype html><meta charset="utf-8">{head}<script>
function check(name, url) {{
  return fetch(url).then(function (r) {{
    return r.text().then(function (t) {{ console.log('CHECK:' + name + '=' + r.status + ':' + t); }});
  }}, function (e) {{ console.log('CHECK:' + name + '=ERR:' + e); }});
}}
Promise.all([{calls}]).then(function () {{ console.log('CHECK:done=1'); }});
</script>"""


def run(binary: str, page_path: pathlib.Path, url: str | None, timeout: float) -> str:
    command = [binary, str(page_path), "--virtual-time-budget=2000"]
    if url:
        command.append(f"--url={url}")
    proc = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(f"CLI exited {proc.returncode}: {proc.stderr.strip()[:200]}")
    return proc.stdout + proc.stderr


def lines(output: str) -> dict[str, str]:
    result = {}
    for line in output.splitlines():
        if line.startswith("CHECK:"):
            name, _, value = line[len("CHECK:"):].partition("=")
            result[name] = value
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default="build/litebird")
    ap.add_argument("--timeout", type=float, default=30.0)
    args = ap.parse_args()
    binary = pathlib.Path(args.binary)
    if not binary.is_file():
        print(f"binary not found: {binary}", file=sys.stderr)
        return 2

    failures = 0
    checks = 0
    with tempfile.TemporaryDirectory(prefix="litebird-fetch-origin-") as tmp:
        root = pathlib.Path(tmp)
        secret = root / "secret.txt"
        secret.write_text(MARKER, encoding="utf-8")
        secret_url = secret.as_uri()

        web_requests = [
            ("path", str(secret)),
            ("file", secret_url),
            ("rel", "secret.txt"),
            ("data", "data:text/plain,ok"),
        ]
        web_page = root / "web.html"
        web_page.write_text(page(web_requests), encoding="utf-8")
        web = lines(run(str(binary), web_page, "https://example.test/dir/page.html", args.timeout))
        checks += 1
        ok = (
            web.get("done") == "1"
            and web.get("data") == "200:ok"
            and web.get("path", "").startswith("ERR:")
            and web.get("file", "").startswith("ERR:")
            and web.get("rel", "").startswith("ERR:")
            and MARKER not in "\n".join(web.values())
        )
        print(f"{'PASS' if ok else 'FAIL'} web document cannot read local files")
        if not ok:
            failures += 1
            print(web)

        # A <base href> pointing at the filesystem must not grant file access.
        smuggled_page = root / "smuggled.html"
        smuggled_page.write_text(page([("rel", "secret.txt")], base=root.as_uri() + "/"), encoding="utf-8")
        smuggled = lines(run(str(binary), smuggled_page, "https://example.test/dir/page.html", args.timeout))
        checks += 1
        ok = smuggled.get("done") == "1" and smuggled.get("rel", "").startswith("ERR:") and MARKER not in "\n".join(smuggled.values())
        print(f"{'PASS' if ok else 'FAIL'} file: base href does not grant a web page local access")
        if not ok:
            failures += 1
            print(smuggled)

        file_requests = [("rel", "secret.txt"), ("data", "data:text/plain,ok")]
        file_page = root / "page.html"
        file_page.write_text(page(file_requests), encoding="utf-8")
        local = lines(run(str(binary), file_page, file_page.as_uri(), args.timeout))
        checks += 1
        ok = local.get("done") == "1" and local.get("rel") == f"200:{MARKER}" and local.get("data") == "200:ok"
        print(f"{'PASS' if ok else 'FAIL'} file: document keeps local relative access")
        if not ok:
            failures += 1
            print(local)

    print(f"\n{checks} checks; {failures} failure(s).")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
