#!/usr/bin/env python3
"""Capture CLI and REPL echo attacks without displaying control bytes."""
import argparse
import json
import os
import pathlib
import re
import select
import subprocess
import tempfile
import time
import urllib.request


REPLACEMENT = '�'.encode()
PREFIX = b'\x1b[1;32mlitebird> \x1b[0m\x1b[90m=> \x1b[0m'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', default='build/litebird')
    binary = os.fsencode(pathlib.Path(ap.parse_args().binary).resolve())
    checks = []

    def check(name, condition):
        checks.append((name, bool(condition)))

    def run(*args, input=None):
        return subprocess.run([binary, *args], input=input, capture_output=True, timeout=30)

    # Both page/script data and local names can carry instructions. The
    # verifier never sends captured pre-fix output to its own stdout.
    attack = 'Aě\x1b]0;OWN\x07\r\u009bZ'
    clean = 'Aě�]0;OWN���Z'.encode()
    repl = run(b'--repl', input=(
        'var s="Aě\\x1b]0;OWN\\x07\\r\\u009bZ";\n'
        's\n'
        's.charCodeAt(2) + ":" + s.charCodeAt(9) + ":" + s.charCodeAt(10) + ":" + s.charCodeAt(11)\n'
        '"\\ud800"\n'
        '"keep\\ttab\\nline"\n'
        'exit\n').encode())
    check('REPL completes', repl.returncode == 0 and repl.stderr == b'')
    check('REPL filters result and preserves Unicode', PREFIX + clean + b'\n' in repl.stdout)
    check('REPL leaves stored JS value unchanged', PREFIX + b'27:7:13:155\n' in repl.stdout)
    check('REPL outputs one replacement for a lone surrogate', PREFIX + REPLACEMENT + b'\n' in repl.stdout)
    check('REPL retains tabs and line feeds', PREFIX + b'keep\ttab\nline\n' in repl.stdout)

    with tempfile.TemporaryDirectory(prefix='cli-echo-') as tmp:
        root = pathlib.Path(tmp)
        root_bytes = os.fsencode(tmp)
        page = root / 'page.html'
        page.write_text('<!doctype html><title>echo test</title><p>hello</p>', encoding='utf-8')
        link = root / (attack + '-program')
        link.symlink_to(os.fsdecode(binary))
        help_output = subprocess.run([os.fsencode(link), b'--help'], capture_output=True, timeout=20)
        check('help filters the invoked program path', help_output.returncode == 0 and
              b'Usage: ' + root_bytes + b'/' + clean + b'-program [options]' in help_output.stdout)
        check('help keeps its own ANSI and normal usage text',
              b'\x1b[1;36m' in help_output.stdout and b'--run-js' in help_output.stdout)

        for fmt, label in [('bmp', 'bitmap'), ('png', 'PNG')]:
            path = root / (attack + '.' + fmt)
            result = run(('--render-' + fmt).encode(), os.fsencode(path), os.fsencode(page))
            expected = b'\x1b[1;32mRendered ' + label.encode() + b' saved to: ' + root_bytes + b'/' + clean + b'.' + fmt.encode() + b'\x1b[0m\n'
            check(fmt + ' success filters its display path', result.returncode == 0 and result.stdout == expected and result.stderr == b'')
            check(fmt + ' is written at the original filename', path.is_file() and path.stat().st_size > 0)
            missing = root / 'missing' / (attack + '.' + fmt)
            failure = run(('--render-' + fmt).encode(), os.fsencode(missing), os.fsencode(page))
            expected_error = b'Failed to save ' + label.encode() + b' to: ' + root_bytes + b'/missing/' + clean + b'.' + fmt.encode() + b'\n'
            check(fmt + ' failure filters its display path', failure.returncode == 0 and
                  failure.stdout == b'' and failure.stderr == expected_error and not missing.exists())

        invalid = root_bytes + b'/invalid-\xff\x9b.bmp'
        invalid_result = run(b'--render-bmp', invalid, os.fsencode(page))
        check('malformed filename bytes are filtered and still used',
              invalid_result.returncode == 0 and os.path.isfile(invalid) and
              b'invalid-' + REPLACEMENT * 2 + b'.bmp\x1b[0m\n' in invalid_result.stdout)

        # Read startup from a real server, query its state, then always stop it.
        # The port is ephemeral; no remote service is needed.
        hostile_page = root / (attack + '.html')
        hostile_page.write_text('<!doctype html><title>hostile filename opened</title>', encoding='utf-8')
        proc = subprocess.Popen([binary, b'--port=0', b'--gui', os.fsencode(hostile_page)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        output = bytearray()
        state = None
        try:
            deadline = time.monotonic() + 25
            while b'\nInitial page: ' not in output or not output.endswith(b'\n'):
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not select.select([proc.stdout], [], [], remaining)[0]:
                    break
                chunk = os.read(proc.stdout.fileno(), 4096)
                if not chunk:
                    break
                output.extend(chunk)
            match = re.search(rb'listening on http://0\.0\.0\.0:(\d+)\n', output)
            if match:
                url = 'http://127.0.0.1:' + match.group(1).decode('ascii') + '/api/state'
                with urllib.request.urlopen(url, timeout=10) as response:
                    state = json.load(response)
        except (OSError, ValueError):
            pass
        finally:
            proc.terminate()
            try:
                extra, _ = proc.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                extra, _ = proc.communicate(timeout=10)
            output.extend(extra)
        check('GUI startup filters the initial page echo',
              b'Initial page: ' + root_bytes + b'/' + clean + b'.html\n' in output)
        check('GUI navigates to the original control-bearing path',
              state is not None and state.get('url') == str(hostile_page) and
              state.get('title') == 'hostile filename opened')

    for name, ok in checks:
        print(('PASS ' if ok else 'FAIL ') + name)
    passed = sum(ok for _, ok in checks)
    print(f'CLI echo terminal safety: {passed}/{len(checks)} checks pass')
    return int(passed != len(checks))


if __name__ == '__main__':
    raise SystemExit(main())
