#!/usr/bin/env python3
"""Check exception and promise diagnostics through the real CLI.

The subprocess output is captured; attack bytes are never printed by this tool.
"""
import argparse
import pathlib
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    binary = pathlib.Path(parser.parse_args().binary).resolve()
    attack = r'"\x1b]0;bad\x07\r\x9b"'
    clean = b'?]0;bad????'
    cases = [
        ('uncaught thrown string', 'throw ' + attack, 1, b'', b'Uncaught ' + clean + b'\n'),
        ('uncaught Error object', 'throw new Error(' + attack + ')', 1, b'', b'Uncaught Error: ' + clean + b'\n'),
        ('unhandled promise rejection', 'Promise.reject(' + attack + ')', 1, b'', b'Uncaught (in promise) ' + clean + b'\n'),
        ('uncanceled error event keeps original value',
         "addEventListener('error',function(e){console.log('EVENT:'+e.message.length+':'+e.message.charCodeAt(0)+':'+e.error.charCodeAt(9));});throw " + attack,
         1, b'EVENT:10:27:155\n', b'Uncaught ' + clean + b'\n'),
        ('canceled error event keeps original value',
         "addEventListener('error',function(e){console.log('EVENT:'+e.message.length+':'+e.message.charCodeAt(0));e.preventDefault()});throw " + attack,
         0, b'EVENT:10:27\n', b''),
        ('caught rejection keeps original value',
         "Promise.reject(" + attack + ").catch(function(e){console.log('CAUGHT:'+e.length+':'+e.charCodeAt(0)+':'+e.charCodeAt(9))})",
         0, b'CAUGHT:10:27:155\n', b''),
    ]
    passed = 0
    for label, code, status, stdout, stderr in cases:
        result = subprocess.run([str(binary), '--run-js', code], capture_output=True, timeout=15)
        ok = result.returncode == status and result.stdout == stdout and result.stderr == stderr
        passed += ok
        print(('PASS ' if ok else 'FAIL ') + label)
    print(f'JS error terminal safety: {passed}/{len(cases)} checks pass')
    return int(passed != len(cases))


if __name__ == '__main__':
    raise SystemExit(main())
