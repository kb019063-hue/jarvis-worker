#!/usr/bin/env python3
"""End-to-end check that a FormData body reaches the wire as multipart/form-data.

Starts a raw TCP loopback server, loads a generated page in the Litebird CLI,
and captures the exact bytes of the two POSTs it makes: one with page fetch(),
one with XMLHttpRequest.send(FormData). Each must carry a
`multipart/form-data; boundary=...` Content-Type and a body the boundary parses
into the expected parts (RFC 7578), with Content-Length matching the body.

Run: python3 tools/conformance/verify_formdata_body.py [--binary build/litebird]
"""
from __future__ import annotations

import argparse
import socket
import subprocess
import sys
import tempfile
import threading
import pathlib

PAGE = """<!doctype html><meta charset="utf-8"><script>
function buildForm() {
  var fd = new FormData();
  fd.append('plain', 'value');
  fd.append('file', new File(['hello file'], 'greeting.txt', { type: 'text/plain' }));
  fd.append('blob', new Blob(['blob-bytes']));
  return fd;
}
fetch('__ORIGIN__/upload', { method: 'POST', body: buildForm() })
  .then(function (r) { console.log('CHECK:fetch=' + r.status); },
         function (e) { console.log('CHECK:fetch=ERR:' + e); });
var xhr = new XMLHttpRequest();
xhr.open('POST', '__ORIGIN__/upload');
xhr.onload = function () { console.log('CHECK:xhr=' + xhr.status); };
xhr.onerror = function () { console.log('CHECK:xhr=ERR'); };
xhr.send(buildForm());
</script>"""


def capture_requests(server: socket.socket, expected: int, out: list[bytes], ready: threading.Event) -> None:
    server.settimeout(20.0)
    ready.set()
    while len(out) < expected:
        try:
            conn, _ = server.accept()
        except socket.timeout:
            break
        with conn:
            conn.settimeout(10.0)
            raw = b""
            try:
                while b"\r\n\r\n" not in raw:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    raw += chunk
                head, _, rest = raw.partition(b"\r\n\r\n")
                headers = {}
                for line in head.decode("latin1").split("\r\n")[1:]:
                    name, _, value = line.partition(":")
                    headers[name.strip().lower()] = value.strip()
                length = int(headers.get("content-length", "0"))
                body = rest
                while len(body) < length:
                    chunk = conn.recv(65536)
                    if not chunk:
                        break
                    body += chunk
                out.append(head + b"\r\n\r\n" + body[:length])
                conn.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok")
            except OSError:
                continue


def parse_multipart(body: bytes, boundary: str):
    delim = ("--" + boundary).encode()
    close = delim + b"--\r\n"
    parts = []
    body = body.strip(b"\r\n")
    if not body.startswith(delim) or not body.endswith(close.rstrip(b"\r\n")):
        return None
    segments = body.split(delim)[1:-1]
    for seg in segments:
        if seg.startswith(b"--"):  # closing marker
            break
        seg = seg.lstrip(b"\r\n").removesuffix(b"\r\n")
        head, _, content = seg.partition(b"\r\n\r\n")
        headers = {}
        for line in head.decode("utf-8", "replace").split("\r\n"):
            if not line.strip():
                continue
            name, _, value = line.partition(":")
            headers[name.strip().lower()] = value.strip()
        parts.append((headers, content))
    return parts


def check_request(label: str, raw: bytes, failures: list[str]) -> None:
    head, sep, body = raw.partition(b"\r\n\r\n")
    if not sep:
        failures.append(f"{label}: no header/body separator")
        return
    headers = {}
    status_line = head.decode("latin1").split("\r\n")[0]
    for line in head.decode("latin1").split("\r\n")[1:]:
        name, _, value = line.partition(":")
        headers[name.strip().lower()] = value.strip()

    def ok(cond: bool, what: str):
        if not cond:
            failures.append(f"{label}: {what}")

    ok("POST" in status_line, f"method in {status_line!r}")
    ct = headers.get("content-type", "")
    ok(ct.startswith("multipart/form-data; boundary="), f"content-type {ct!r}")
    boundary = ct.partition("boundary=")[2]
    ok(len(boundary) >= 20 and "\r" not in boundary, f"boundary {boundary!r}")
    cl = headers.get("content-length")
    ok(cl == str(len(body)), f"content-length {cl!r} != body {len(body)}")
    parts = parse_multipart(body, boundary)
    ok(parts is not None, "multipart does not start/end with the boundary")
    if parts is None:
        return
    names = [p[0].get("content-disposition", "") for p in parts]
    ok(any('name="plain"' in n and 'filename=' not in n for n in names), f"plain part in {names}")
    ok(any('name="file"' in n and 'filename="greeting.txt"' in n for n in names), f"file part in {names}")
    ok(any('name="blob"' in n and 'filename="blob"' in n for n in names), f"blob part in {names}")
    plain = next(p for p in parts if 'name="plain"' in p[0].get("content-disposition", ""))
    ok(plain[1] == b"value", f"plain part content {plain[1]!r}")
    file_part = next(p for p in parts if 'name="file"' in p[0].get("content-disposition", ""))
    ok(file_part[0].get("content-type") == "text/plain", f"file part content-type {file_part[0]}")
    ok(file_part[1] == b"hello file", f"file part content {file_part[1]!r}")
    blob_part = next(p for p in parts if 'name="blob"' in p[0].get("content-disposition", ""))
    ok(blob_part[0].get("content-type") == "application/octet-stream", f"blob content-type {blob_part[0]}")
    ok(blob_part[1] == b"blob-bytes", f"blob part content {blob_part[1]!r}")
    if not any(f.startswith(label) for f in failures):
        print(f"PASS {label}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default="build/litebird")
    ap.add_argument("--timeout", type=float, default=30.0)
    args = ap.parse_args()
    binary = pathlib.Path(args.binary)
    if not binary.is_file():
        print(f"binary not found: {binary}", file=sys.stderr)
        return 2

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 0))
    server.listen(4)
    port = server.getsockname()[1]
    origin = f"http://127.0.0.1:{port}"

    requests: list[bytes] = []
    ready = threading.Event()
    thread = threading.Thread(target=capture_requests, args=(server, 2, requests, ready), daemon=True)
    thread.start()
    ready.wait(5)

    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="litebird-formdata-") as tmp:
        page_path = pathlib.Path(tmp) / "form.html"
        page_path.write_text(PAGE.replace("__ORIGIN__", origin), encoding="utf-8")
        try:
            proc = subprocess.run(
                [str(binary), str(page_path), f"--url={origin}/form.html", "--virtual-time-budget=8000"],
                capture_output=True, text=True, timeout=args.timeout,
            )
        except subprocess.TimeoutExpired:
            print("FAIL: Litebird did not finish in time", file=sys.stderr)
            server.close()
            return 1
    output = proc.stdout + proc.stderr
    for line in output.splitlines():
        if line.startswith("CHECK:"):
            print(line)

    thread.join(15)
    server.close()

    if len(requests) != 2:
        failures.append(f"expected 2 requests on the wire, captured {len(requests)}")
    labels = ["fetch", "xhr"]
    for i, raw in enumerate(requests[:2]):
        check_request(labels[i] if i < len(labels) else f"request-{i}", raw, failures)

    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("PASS FormData bodies arrive as multipart/form-data (fetch and XMLHttpRequest)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
