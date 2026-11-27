#!/usr/bin/env python3
"""Compare toExponential and toPrecision rounding against Node."""
import argparse
import math
import random
import struct
import subprocess
import sys


def cases():
    values = [0.0, -0.0, 1.25, -1.25, 0.125, -0.125, 2.5, 9.5, 99.5,
              0.0000009995, 999.5, 1e21, 5e-324, 1e-300, 1e308,
              math.inf, -math.inf, math.nan]
    for x in [0.125, 1.25, 9.5, 99.5, 999.5, 0.0000009995, 1e21]:
        values.extend([math.nextafter(x, 0), math.nextafter(x, math.inf),
                       -math.nextafter(x, 0), -math.nextafter(x, math.inf)])
    rng = random.Random(20261126)
    for _ in range(200):
        x = struct.unpack('>d', rng.getrandbits(64).to_bytes(8, 'big'))[0]
        if math.isfinite(x): values.append(x)
    result = []
    for x in values:
        for digits in (0, 1, 2, 3, 6, 10, 17, 50, 99):
            result.extend([(x, 'toExponential', digits), (x, 'toPrecision', digits + 1)])
    return result


def literal(x):
    if math.isnan(x): return 'NaN'
    if math.isinf(x): return '-Infinity' if x < 0 else 'Infinity'
    if x == 0 and math.copysign(1, x) < 0: return '-0'
    return repr(x)


def run(command, cases):
    expr = ','.join(f'({literal(x)}).{method}({digits})' for x, method, digits in cases)
    proc = subprocess.run([*command, "console.log('ROUNDS:'+[" + expr + "].join('|'))"],
                          capture_output=True, text=True, timeout=30)
    if proc.returncode: raise RuntimeError(f'{command[0]}: {proc.stderr[:400]}')
    lines = [line[7:] for line in proc.stdout.splitlines() if line.startswith('ROUNDS:')]
    if len(lines) != 1: raise RuntimeError(f'{command[0]}: missing output {proc.stdout[:400]}')
    return lines[0].split('|')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    inputs = cases()
    failures = 0
    try:
        for i in range(0, len(inputs), 128):
            batch = inputs[i:i+128]
            actual, expected = run([args.binary, '--run-js'], batch), run([args.node, '-e'], batch)
            if len(actual) != len(batch) or len(expected) != len(batch): raise RuntimeError('result count mismatch')
            for case, got, want in zip(batch, actual, expected):
                if got != want:
                    if failures < 16: print(f'FAIL {case}: Litebird={got!r}, Node={want!r}')
                    failures += 1
    except (OSError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1
    print(f'{len(inputs)-failures}/{len(inputs)} cases agree with Node')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
