#!/usr/bin/env python3
"""Check a real shell's static ANSI frame without sending it to a terminal."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SGR = re.compile(rb'\x1b\[[0-9;]*m')


def verify(frame, path):
    # The shell and renderer generate SGR color/reset sequences only. An
    # injected ESC, C1, CR, BEL or other control outside them is never legal.
    stripped = SGR.sub(b'', frame)
    if any(c < 32 and c != 10 or 0x7f <= c <= 0x9f for c in stripped):
        # Trusted UI arrows are UTF-8 and contain continuation bytes in the C1
        # range, so remove their exact literal sequences first.
        trusted = stripped
        for arrow in ('◄', '►', '⟳', '↑', '↓'):
            trusted = trusted.replace(arrow.encode(), b'')
        if any(c < 32 and c != 10 or 0x7f <= c <= 0x9f for c in trusted):
            raise ValueError('unexpected control byte in frame')
    safe_path = bytes(c if 32 <= c <= 126 else 63 for c in path)
    # Test these fields before stripping escapes: an injected SGR is also a
    # syntactically valid SGR, but must not be accepted inside a label.
    if b'URL: [\x1b[1m' + safe_path + b'\x1b[22m]' not in frame:
        raise ValueError('URL missing or not escaped')
    if b'Loaded ' + safe_path + b'\x1b[0m' not in frame:
        raise ValueError('loaded status missing or not escaped')
    if b'Focus: <button#id?]2;owned?>' not in frame:
        raise ValueError('focused id missing or not escaped')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='shell-safety-') as directory:
        path = Path(directory) / 'page-\x1b]0;owned\x07.html'
        path.write_text('<!doctype html><button id="id&#27;]2;owned&#7;">press</button>')
        result = subprocess.run([str(Path(args.binary).resolve()), '--shell', str(path)],
                                stdin=subprocess.DEVNULL, capture_output=True, cwd=ROOT, timeout=20)
        if result.returncode:
            print(f'FAIL: shell exited {result.returncode}')
            return 1
        try:
            verify(result.stdout, bytes(str(path), 'utf-8'))
        except ValueError as exc:
            print(f'FAIL: {exc}')
            return 1
        # Challenge the checker itself without emitting dangerous bytes.
        for bad_frame in (result.stdout + b'\x1b]0;owned\x07',
                          result.stdout + b'\r', result.stdout + b'\x9b',
                          result.stdout.replace(b'URL: [\x1b[1m', b'URL: [\x1b[1m\x1b[31m', 1)):
            try:
                verify(bad_frame, bytes(str(path), 'utf-8'))
            except ValueError:
                continue
            raise AssertionError('checker accepted an injected control')
        print('Shell frame safety: 1/1 real shell frames pass; 4/4 injected controls rejected')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
