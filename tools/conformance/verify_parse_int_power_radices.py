#!/usr/bin/env python3
"""Compare parseInt power-of-two radices with exact Python integers and Node."""
import argparse
import json
import math
import random
import subprocess
import sys

ALPHABET = '0123456789abcdefghijklmnopqrstuv'


def digits(n, radix):
    if not n: return '0'
    s = ''
    while n:
        n, d = divmod(n, radix)
        s = ALPHABET[d] + s
    return s


def cases():
    rng = random.Random(20261128)
    numbers = [0, 1, 15, (1 << 53) - 1, (1 << 53), (1 << 53) + 1,
               (1 << 53) + 3, (1 << 54) + 3, (1 << 1024) - (1 << 970),
               (1 << 1024) - (1 << 971), 1 << 1024, 1 << 1100]
    for bits in [54, 55, 64, 100, 200, 500, 1023, 1024, 1025]:
        for _ in range(32): numbers.append((1 << (bits - 1)) | rng.getrandbits(bits - 1))
    out = []
    for radix in (2, 4, 8, 16, 32):
        for index, n in enumerate(numbers):
            text = digits(n, radix)
            if index % 2: text = '-' + text
            if index % 3 == 0: text = ' \t' + text + '!tail'
            out.append((text, radix, -n if index % 2 else n))
    out += [('-0', 2, 0), ('0x', 16, None), ('0x10tail', 16, 16),
            ('-0Xfftail', 16, -255), ('z', 32, None), ('00000000000000', 2, 0)]
    return out


def expected(n, text):
    if n is None: return math.nan
    if n == 0 and text.lstrip().startswith('-'): return -0.0
    try: return float(n)
    except OverflowError: return -math.inf if n < 0 else math.inf


def agrees(text, expected_value):
    try: actual = float(text)
    except ValueError: return False
    if math.isnan(expected_value): return math.isnan(actual)
    if expected_value == 0: return actual == 0 and math.copysign(1, actual) == math.copysign(1, expected_value)
    return actual == expected_value


def run(command, batch):
    expr = ','.join('parseInt(' + json.dumps(s) + ',' + str(r) + ')' for s, r, _ in batch)
    script = "var a=[" + expr + "];console.log('INTS:'+a.map(function(x){return x===0&&1/x===-Infinity?'-0':String(x)}).join('|'))"
    proc = subprocess.run([*command, script], capture_output=True, text=True, timeout=30)
    if proc.returncode: raise RuntimeError(f'{command[0]}: {proc.stderr[:400]}')
    lines = [line[5:] for line in proc.stdout.splitlines() if line.startswith('INTS:')]
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
            batch = inputs[i:i + 128]
            actual, node = run([args.binary, '--run-js'], batch), run([args.node, '-e'], batch)
            if len(actual) != len(batch) or len(node) != len(batch): raise RuntimeError('result count mismatch')
            for (text, radix, n), got, other in zip(batch, actual, node):
                want = expected(n, text)
                if not agrees(got, want) or not agrees(other, want):
                    if failures < 12: print(f'FAIL radix {radix} {text[:64]!r}: Litebird={got}, Node={other}, Python={want}')
                    failures += 1
    except (OSError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1
    print(f'{len(inputs)-failures}/{len(inputs)} cases agree with Python and Node')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
