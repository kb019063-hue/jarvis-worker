#!/usr/bin/env python3
"""Exercise file-open and navigation diagnostics through the public CLI.

All output is captured, so the pre-fix attack bytes are never sent to a terminal.
"""
import argparse
import os
import pathlib
import socketserver
import subprocess
import tempfile
import threading
import urllib.parse


def safe(data):
    return bytes(c if 0x20 <= c <= 0x7e else ord('?') for c in data)


class HostileServer(socketserver.TCPServer):
    allow_reuse_address = True


class HostileResponse(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.recv(8192)
        self.request.sendall(b'HTTP/1.1 200 OK\r\nTransfer-Encoding: \x1b]0;evil\x07\r\nConnection: close\r\n\r\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', default='build/litebird')
    binary = os.fsencode(pathlib.Path(ap.parse_args().binary).resolve())
    checks = []

    def run(*args):
        return subprocess.run([binary, *args], capture_output=True, timeout=20)

    def check(name, result, status, stdout, stderr):
        checks.append((name, result.returncode == status and result.stdout == stdout and result.stderr == stderr))

    with tempfile.TemporaryDirectory(prefix='cli-terminal-') as tmp:
        root = os.fsencode(tmp)
        # NUL cannot occur in argv; slash is the path separator and is already
        # exercised in the prefix. Every other byte is present in the two names.
        groups = [bytes(c for c in range(1, 128) if c != ord('/')), bytes(range(128, 256))]
        for index, group in enumerate(groups):
            path = root + b'/missing-' + group
            result = run(path)
            check(f'missing filename bytes group {index + 1}', result, 1, b'',
                  b"Error: Could not open file '" + safe(path) + b"'\n")

        page = root + b'/page.html'
        with open(page, 'wb') as f:
            f.write(b'<!doctype html><p>safe text</p>')
        missing_script = root + b'/script-\x1b]0;bad\x07.js'
        check('missing deferred script', run(page, b'--run-script', missing_script), 1, b'',
              b"Error: Could not open file '" + safe(missing_script) + b"'\n")

        file_path = root + b'/absent-\x1b]0;bad\x07'
        url = b'file://' + urllib.parse.quote_from_bytes(file_path, safe='/').encode('ascii')
        check('decoded file URL in navigation error', run(url), 1, b'',
              b"Error loading '" + safe(url) + b"': File not found: " + safe(file_path) + b'\n')
        raw_url = b'data:\x1b]0;bad\x07'
        check('raw navigation URL in error', run(raw_url), 1, b'',
              b"Error loading '" + safe(raw_url) + b"': Invalid data: URL\n")

        existing = root + b'/existing-\x1b]0;safe\x07.html'
        with open(existing, 'wb') as f:
            f.write(b'<!doctype html><p>control path loaded</p>')
        result = run(b'--dump-dom-webplatform', existing)
        checks.append(('existing control-bearing filename still opens',
                       result.returncode == 0 and result.stderr == b'' and b'control path loaded' in result.stdout))
        normal = root + b'/missing-normal.html'
        check('ordinary filename error is unchanged', run(normal), 1, b'',
              b"Error: Could not open file '" + normal + b"'\n")

        with HostileServer(('127.0.0.1', 0), HostileResponse) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                url = f'http://127.0.0.1:{server.server_address[1]}/'.encode('ascii')
                check('hostile response cannot inject a terminal command', run(url), 1, b'',
                      b"Error loading '" + url + b"': Unsupported HTTP Transfer-Encoding: ?]0;evil?\n")
            finally:
                server.shutdown()
                thread.join()

    for name, ok in checks:
        print(('PASS ' if ok else 'FAIL ') + name)
    count = sum(ok for _, ok in checks)
    print(f'CLI error terminal safety: {count}/{len(checks)} checks pass')
    return int(count != len(checks))


if __name__ == '__main__':
    raise SystemExit(main())
