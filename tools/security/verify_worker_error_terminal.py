#!/usr/bin/env python3
"""Exercise worker error stderr and ErrorEvent through the real CLI.

No attack bytes are printed by this checker; the child's streams are captured.
"""
import argparse
import http.server
import pathlib
import subprocess
import tempfile
import threading
import time


class YieldHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        time.sleep(0.025)  # give the real worker thread a chance to run
        self.send_response(200)
        self.send_header('Content-Length', '2')
        self.end_headers()
        self.wfile.write(b'ok')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', default='build/litebird')
    args = ap.parse_args()
    binary = pathlib.Path(args.binary).resolve()
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), YieldHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_address[1]}'
    page = r'''<!doctype html><script>
var u = 'data:text/javascript,throw "\\x1b]0;worker\\x07\\r\\x9b";';
var one = new Worker(u);
var handled = new Worker(u);
handled.onerror = function(e) {
  var s = e.message;
  console.log('HANDLED:' + s.length + ':' + s.charCodeAt(0) + ':' + s.charCodeAt(s.length-1));
};
var count = 0;
function wait() {
  if (++count === 12) { console.log('DONE'); return; }
  fetch('BASE/yield').then(wait, wait);
}
setTimeout(wait, 1);
</script>'''.replace('BASE', base)
    try:
        with tempfile.TemporaryDirectory(prefix='worker-terminal-') as tmp:
            path = pathlib.Path(tmp) / 'index.html'
            path.write_text(page, encoding='utf-8')
            proc = subprocess.run([str(binary), str(path), f'--url={base}/index.html',
                                   '--virtual-time-budget=2000'], capture_output=True, timeout=30)
        expected = b'Uncaught (in worker) ?]0;worker????\n'
        checks = [
            ('process and asynchronous wait', proc.returncode == 0 and b'DONE\n' in proc.stdout),
            ('unhandled worker diagnostic is printable ASCII', proc.stderr == expected),
            ('listener retains original characters', b'HANDLED:13:27:155\n' in proc.stdout),
        ]
        for name, ok in checks:
            print(('PASS ' if ok else 'FAIL ') + name)
        print(f'Worker terminal safety: {sum(ok for _, ok in checks)}/{len(checks)} checks pass')
        return int(not all(ok for _, ok in checks))
    finally:
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    raise SystemExit(main())
