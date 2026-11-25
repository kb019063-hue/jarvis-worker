#!/usr/bin/env python3
"""Test real console output without displaying the attack bytes."""
import argparse
import pathlib
import subprocess
import tempfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', default='build/litebird')
    binary = pathlib.Path(ap.parse_args().binary).resolve()
    # Different labels that have the same filtered representation still have
    # different counters. Check the unmodified JS value by its character codes.
    script = r'''var a="Aě\x1b]0;OWN\x07\r\u009bZ";
var b="Aě\x07]0;OWN\x07\r\u009bZ";
console.log(a, "tail"); console.info(a); console.warn(a); console.error(a);
console.assert(false, a); console.count(a); console.count(b); console.count(a);
console.log("RAW:" + a.charCodeAt(2) + ":" + a.charCodeAt(9) + ":" + a.charCodeAt(10) + ":" + a.charCodeAt(11));'''
    # String offsets are JS UTF-16 code units, including 'ě' as one unit.
    filtered = 'Aě�]0;OWN���Z'.encode()
    result = subprocess.run([str(binary), '--run-js', script], capture_output=True, timeout=20)
    lines = result.stdout.splitlines()
    checks = [
        ('CLI exits normally', result.returncode == 0),
        ('log and info filter arguments', len(lines) >= 2 and lines[0] == filtered + b' tail' and lines[1] == filtered),
        ('warning keeps only its own ANSI sequences', len(lines) >= 3 and lines[2] == b'\x1b[33m[WARN] ' + filtered + b' \x1b[0m'),
        ('counter keys remain distinct and unmodified', len(lines) >= 6 and lines[3:6] == [filtered + b': 1', filtered + b': 1', filtered + b': 2']),
        ('script-visible value is unchanged', b'RAW:27:7:13:155\n' in result.stdout),
        ('error and assert filter arguments', result.stderr == b'\x1b[31m[ERROR] ' + filtered + b' \x1b[0m\nAssertion failed: ' + filtered + b'\n'),
    ]
    with tempfile.TemporaryDirectory(prefix='console-terminal-') as directory:
        root = pathlib.Path(directory)
        page = root / 'page.html'
        page.write_text('<!doctype html><script>console.log("PAGE\\x1b]52;OWN\\x07");</script>', encoding='utf-8')
        run = subprocess.run([str(binary), '--render-bmp', str(root / 'frame.bmp'), str(page)],
                             capture_output=True, timeout=30)
        checks.append(('actual page console filters the attack', run.returncode == 0 and run.stderr == b'' and
                       run.stdout.startswith('PAGE�]52;OWN�\n'.encode())))
    for label, ok in checks:
        print(('PASS ' if ok else 'FAIL ') + label)
    passed = sum(ok for _, ok in checks)
    print(f'Console terminal safety: {passed}/{len(checks)} checks pass')
    return int(passed != len(checks))


if __name__ == '__main__':
    raise SystemExit(main())
