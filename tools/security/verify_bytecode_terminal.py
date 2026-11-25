#!/usr/bin/env python3
"""Capture bytecode dump attacks without displaying their control bytes."""
import argparse
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    binary = pathlib.Path(parser.parse_args().binary).resolve()
    literal = r'Aě\x1b]0;OWN\x07\r\u009bZ'
    clean = 'Aě�]0;OWN���Z'.encode()
    replacement = '�'.encode()
    checks = []

    def check(label, condition):
        checks.append((label, bool(condition)))

    script = 'var x="' + literal + '"; x;'
    cli = subprocess.run([str(binary), '--run-js', script, '--dump-bytecode'],
                         capture_output=True, timeout=20)
    check('CLI bytecode compiles', cli.returncode == 0 and cli.stderr == b'')
    check('CLI constant is filtered, valid Unicode retained',
          b"OP_CONSTANT        '" + clean + b"'" in cli.stdout)
    check('ordinary disassembly structure and names survive',
          cli.stdout.startswith(b'== Disassembly for CLI Script ==\n') and
          b"OP_SET_GLOBAL      'x'" in cli.stdout and b'OP_RETURN' in cli.stdout)
    check('CLI dump contains no attack control',
          all(control not in cli.stdout for control in (b'\x1b', b'\x07', b'\r', b'\xc2\x9b')))

    invalid = 'var "' + literal + '" = 1;'
    error = subprocess.run([str(binary), '--run-js', invalid, '--dump-bytecode'],
                           capture_output=True, timeout=20)
    check('CLI compiler diagnostic is filtered', error.returncode == 1 and
          b"Unexpected token '" + clean + b"': expected a variable name" in error.stderr and
          all(control not in error.stderr for control in (b'\x1b', b'\x07', b'\r', b'\xc2\x9b')))

    with tempfile.TemporaryDirectory(prefix='bytecode-terminal-') as directory:
        root = pathlib.Path(directory)
        page = root / 'page.html'
        bmp = root / 'frame.bmp'
        page.write_text('<script>' + script + '</script><script>' + invalid + '</script>', encoding='utf-8')
        result = subprocess.run([str(binary), '--dump-bytecode', '--render-bmp', str(bmp), str(page)],
                                capture_output=True, timeout=30)
        heading = b'\x1b[1;33m--- [Compiled Bytecode Disassembly] ---\x1b[0m\n'
        check('page reaches both disassemblies', result.returncode == 0 and result.stdout.count(heading) == 2)
        check('page constant is filtered', b"OP_CONSTANT        '" + clean + b"'" in result.stdout)
        check('page compiler diagnostic is filtered',
              b"(not compiled: Unexpected token '" + clean + b"': expected a variable name" in result.stdout)
        # Only the application's fixed headings and success message contain ESC.
        check('page retains only program-owned escape sequences',
              result.stdout.count(b'\x1b') == 6 and b'\x07' not in result.stdout and
              b'\r' not in result.stdout and b'\xc2\x9b' not in result.stdout)

    # A lone UTF-16 surrogate is output as one replacement, not malformed UTF-8.
    surrogate = subprocess.run([str(binary), '--run-js', r'"\ud800";', '--dump-bytecode'],
                               capture_output=True, timeout=20)
    check('lone surrogate is one replacement character', surrogate.returncode == 0 and
          b"OP_CONSTANT        '" + replacement + b"'" in surrogate.stdout)
    for label, ok in checks:
        print(('PASS ' if ok else 'FAIL ') + label)
    passed = sum(ok for _, ok in checks)
    print(f'Bytecode terminal safety: {passed}/{len(checks)} checks pass')
    return int(passed != len(checks))


if __name__ == '__main__':
    raise SystemExit(main())
