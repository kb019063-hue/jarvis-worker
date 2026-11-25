#!/usr/bin/env python3
"""Compare numeric parsers' Unicode leading whitespace against Node."""
import argparse
import json
import subprocess
import sys


def cases():
    whitespace = [9, 10, 11, 12, 13, 32, 0xA0, 0x1680, *range(0x2000, 0x200B),
                  0x2028, 0x2029, 0x202F, 0x205F, 0x3000, 0xFEFF]
    nonspaces = [0, 0x85, 0x180E, 0x200B, 0x200C, 0x202A, 0x2060,
                 0x2FFF, 0x3001, 0xFEFE, 0xFFFF]
    items = []
    for cp in whitespace + nonspaces:
        for prefix in (chr(cp), ' \t' + chr(cp), chr(cp) + '\ufeff'):
            for method, suffix in [('parseInt', '-42tail'), ('parseFloat', '-42.5tail'),
                                   ('Number.parseInt', '0x10tail'),
                                   ('Number.parseFloat', '-Infinitytail')]:
                items.append((method, prefix + suffix))
    for value in ('\u1680-0', '\u205f-0.0', '\u3000', '\u1680+12.5e2tail',
                  '\u1680\u205f\u3000\ufeff42', '1\u16802'):
        for method in ('parseInt', 'parseFloat'):
            items.append((method, value))
    return items


def run(command, items):
    expressions = [method + '(' + json.dumps(value, ensure_ascii=True) + ')'
                   for method, value in items]
    script = ("var a=[" + ','.join(expressions) + "];"
              "console.log('PARSERS:'+a.map(function(x){return x===0&&1/x===-Infinity?'-0':String(x)}).join('|'))")
    proc = subprocess.run([*command, script], capture_output=True, text=True, timeout=30)
    if proc.returncode:
        raise RuntimeError(f'{command[0]} exited {proc.returncode}: {proc.stderr[:400]}')
    lines = [line[8:] for line in proc.stdout.splitlines() if line.startswith('PARSERS:')]
    if len(lines) != 1:
        raise RuntimeError(f'{command[0]} missing result: {proc.stdout[:400]}')
    return lines[0].split('|')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', default='build/litebird')
    p.add_argument('--node', default='node')
    args = p.parse_args()
    inputs = cases()
    try:
        actual, expected = run([args.binary, '--run-js'], inputs), run([args.node, '-e'], inputs)
        if len(actual) != len(inputs) or len(expected) != len(inputs):
            raise RuntimeError('result count mismatch')
        failures = 0
        for (method, value), got, want in zip(inputs, actual, expected):
            if got != want:
                if failures < 15:
                    print(f'FAIL {method}({value!a}): Litebird={got}, Node={want}')
                failures += 1
        print(f'{len(inputs)-failures}/{len(inputs)} cases agree with Node')
        return int(bool(failures))
    except (OSError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
