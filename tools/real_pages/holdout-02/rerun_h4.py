#!/usr/bin/env python3
"""Re-run the exposed H4 gates using the existing driver and frozen steps.

Capture the pinned snapshot first with capture.py. This does not rewrite the
original results or run the other holdout tasks; it is a regression check.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent


def main():
    if len(sys.argv) != 3:
        print('usage: rerun_h4.py CAPTURE_ROOT DRIVER', file=sys.stderr)
        return 2
    root, driver = (Path(arg).resolve() for arg in sys.argv[1:])
    manifest = json.loads((HERE / 'manifest.json').read_text())
    page = next(p for p in manifest['pages'] if p['id'] == 'H4')
    if (root / 'H4' / '.revision').read_text() != page['sha']:
        print('snapshot revision does not match the frozen manifest', file=sys.stderr)
        return 2
    helper = (HERE / 'observe.js').read_text().replace('\n', ' ')
    steps = json.loads((HERE / 'steps.json').read_text())['H4']
    commands, expected = [], []
    for op, arg, *rest in steps:
        if op == 'read':
            expression, want = rest
            commands.append(f'read\t(()=>{{{helper}return String({expression})}})()')
            expected.append((arg, want))
        else:
            commands.append(op + '\t' + arg)
    with tempfile.TemporaryDirectory(prefix='holdout-h4-') as tmp:
        result = subprocess.run([driver, root / 'H4', page['path'], Path(tmp) / 'frame-'],
                                input='\n'.join(commands) + '\n', text=True,
                                capture_output=True, timeout=manifest['native_wall_seconds'])
    lines = result.stdout.splitlines()
    actual = [line[5:] for line in lines if line.startswith('READ\t')]
    errors = [line for line in lines if line.startswith('BLOCKED\t')]
    if result.returncode or errors or 'END' not in lines or len(actual) != len(expected):
        print(f'FAIL: exit={result.returncode}, reads={len(actual)}/{len(expected)}, blocked={errors}')
        return 1
    for (label, want), got in zip(expected, actual):
        if got != want:
            print(f'FAIL {label}: {got!r}, expected {want!r}')
            return 1
    print(f'Holdout-02 H4: {len(expected)}/{len(expected)} frozen reads match after the real click')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
