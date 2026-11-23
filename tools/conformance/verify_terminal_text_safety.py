#!/usr/bin/env python3
"""Verify every text byte through both ANSI output APIs, without a terminal.

The independent parser accepts exactly the renderer's color escapes, one safe
ASCII character per cell, and the row/final resets. Test the old renderer with
--source path/to/old/TerminalRenderer.cpp. The process never prints test bytes
unescaped to a terminal.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CELL = re.compile(rb"\x1b\[38;2;0;0;0m\x1b\[48;2;30;30;30m([\x20-\x7e])")


def verify(output):
    position = 0
    for n in range(256):
        match = CELL.match(output, position)
        if not match:
            raise ValueError(f"byte {n}: unexpected ANSI output at offset {position}")
        expected = n if 0x20 <= n <= 0x7e else ord('?')
        if match.group(1)[0] != expected:
            raise ValueError(f"byte {n}: got {match.group(1)[0]}, expected {expected}")
        position = match.end()
    if output[position:] != b"\x1b[0m\n\x1b[0m":
        raise ValueError("unexpected row or final reset")


def self_test():
    prefix = b'\x1b[38;2;0;0;0m\x1b[48;2;30;30;30m'
    good = b''.join(prefix + bytes([n if 0x20 <= n <= 0x7e else 63])
                    for n in range(256)) + b'\x1b[0m\n\x1b[0m'
    verify(good)
    for bad in (good.replace(prefix + b'?', prefix + b'\x1b', 1),
                good.replace(prefix + b'A', prefix + b'B', 1),
                good[:-1], good.replace(prefix + b'?', prefix + b'\x9b', 1)):
        try:
            verify(bad)
        except ValueError:
            continue
        raise AssertionError('invalid stream accepted')
    print('Verifier self-test: 5/5 (valid, ESC, wrong printable, truncated, C1)')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', default='src/render/TerminalRenderer.cpp')
    args = p.parse_args()
    self_test()
    with tempfile.TemporaryDirectory(prefix='terminal-safe-') as tmp:
        exe = Path(tmp) / 'fixture'
        subprocess.run(shlex.split(os.environ.get('CXX', 'g++')) + [
            '-std=c++20', '-O2', '-I', str(ROOT / 'include'),
            str(ROOT / 'tools/conformance/terminal_text_safety_fixture.cpp'),
            str(ROOT / args.source), '-o', str(exe)], check=True, cwd=ROOT)
        failures = 0
        for mode in ('stream', 'string'):
            result = subprocess.run([exe, mode], capture_output=True, timeout=10)
            try:
                if result.returncode:
                    raise ValueError(f'exited {result.returncode}')
                verify(result.stdout)
            except ValueError as exc:
                failures += 1
                print(f'FAIL {mode}: {exc}')
            else:
                print(f'PASS {mode}: all 256 bytes')
        print(f'Terminal safety: {2 - failures}/2 APIs pass')
        return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
