#!/usr/bin/env python3
"""Capture XML CLI diagnostics containing document-controlled terminal bytes."""
import argparse
import os
import pathlib
import subprocess
import tempfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', default='build/litebird')
    binary = os.fsencode(pathlib.Path(ap.parse_args().binary).resolve())
    checks = []

    with tempfile.TemporaryDirectory(prefix='xml-terminal-') as tmp:
        root = pathlib.Path(tmp)
        cases = [
            ('encoding declaration controls',
             b"<?xml version='1.0' encoding='UTF-8\x1b]52;c;owned\x07'?><r/>",
             "'UTF-8�]52;c;owned�' is not the name of an encoding\n".encode()),
            ('ordinary error remains unchanged', b'<a>',
             b"the element 'a' is not closed at line 1, column 4\n"),
        ]
        for i, (label, data, expected) in enumerate(cases):
            path = root / f'{i}.xml'
            path.write_bytes(data)
            modes = ['canonical', 'events'] if i == 0 else ['canonical']
            for mode in modes:
                result = subprocess.run([binary, f'--xml-{mode}'.encode(), os.fsencode(path)],
                                        capture_output=True, timeout=20)
                checks.append((f'{label} ({mode})', result.returncode == 1 and
                               result.stdout == b'' and result.stderr == expected))

        # Machine-readable output is not passed through the human diagnostic filter.
        good = root / 'good.xml'
        good.write_bytes(b'<r>ok</r>')
        result = subprocess.run([binary, b'--xml-canonical', os.fsencode(good)],
                                capture_output=True, timeout=20)
        checks.append(('canonical XML output is unchanged', result.returncode == 0 and
                       result.stderr == b'' and result.stdout == b'<r>ok</r>'))

    for name, ok in checks:
        print(('PASS ' if ok else 'FAIL ') + name)
    count = sum(ok for _, ok in checks)
    print(f'XML diagnostic terminal safety: {count}/{len(checks)} checks pass')
    return int(count != len(checks))


if __name__ == '__main__':
    raise SystemExit(main())
