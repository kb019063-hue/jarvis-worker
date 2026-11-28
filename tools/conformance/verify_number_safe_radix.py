#!/usr/bin/env python3
"""Compare safe-integer Number.toString radices with Python integers and Node."""
import argparse
import random
import subprocess
import sys

DIGITS = '0123456789abcdefghijklmnopqrstuvwxyz'


def reference(n, radix):
    if n < 0: return '-' + reference(-n, radix)
    if n == 0: return '0'
    s = ''
    while n:
        n, r = divmod(n, radix)
        s = DIGITS[r] + s
    return s


def cases():
    rng = random.Random(20261128)
    values = [0, 1, -1, 2**53 - 1, -(2**53 - 1), 2**53, -(2**53)]
    values += [2**53 - x for x in range(1, 65)]
    values += [rng.randrange(-(2**53 - 1), 2**53) for _ in range(250)]
    return [(n, radix) for n in values for radix in range(2, 37)]


def run(command, batch):
    expr = ','.join(f'({n}).toString({radix})' for n, radix in batch)
    proc = subprocess.run([*command, "console.log('RADIX:'+[" + expr + "].join('|'))"],
                          capture_output=True, text=True, timeout=30)
    if proc.returncode: raise RuntimeError(f'{command[0]}: {proc.stderr[:400]}')
    lines = [line[6:] for line in proc.stdout.splitlines() if line.startswith('RADIX:')]
    if len(lines) != 1: raise RuntimeError(f'{command[0]}: missing output {proc.stdout[:400]}')
    return lines[0].split('|')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', default='build/litebird')
    p.add_argument('--node', default='node')
    args = p.parse_args()
    inputs = cases()
    failures = 0
    try:
        for i in range(0, len(inputs), 128):
            batch = inputs[i:i+128]
            actual, node = run([args.binary, '--run-js'], batch), run([args.node, '-e'], batch)
            if len(actual) != len(batch) or len(node) != len(batch): raise RuntimeError('result count mismatch')
            for (n, radix), got, other in zip(batch, actual, node):
                want = reference(n, radix)
                if got != want or other != want:
                    if failures < 12: print(f'FAIL {n} radix {radix}: Litebird={got}, Node={other}, Python={want}')
                    failures += 1
    except (OSError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1
    print(f'{len(inputs)-failures}/{len(inputs)} integer radix cases agree with Python and Node')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
