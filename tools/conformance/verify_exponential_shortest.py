#!/usr/bin/env python3
"""Compare shortest Number.toExponential against Node over binary64 inputs."""
import argparse
import math
import random
import struct
import subprocess
import sys


def cases():
    values = [0.0, -0.0, 1.0, -1.0, 1.25, math.inf, -math.inf, math.nan,
              5e-324, 1e-323, 1e23, -1e23, 1e21, 1e-6, 1e-7, 1e20]
    for e in range(-323, 309):
        x = float('1e' + str(e))
        values.extend((x, math.nextafter(x, 0.0), math.nextafter(x, math.inf)))
    rng = random.Random(20261128)
    for _ in range(1500):
        x = struct.unpack('>d', rng.getrandbits(64).to_bytes(8, 'big'))[0]
        if math.isfinite(x): values.append(x)
    return values


def literal(x):
    if math.isnan(x): return 'NaN'
    if math.isinf(x): return '-Infinity' if x < 0 else 'Infinity'
    if x == 0 and math.copysign(1, x) < 0: return '-0'
    return repr(x)


def run(cmd, values):
    expr = ','.join('(' + literal(x) + ').toExponential()' for x in values)
    proc = subprocess.run([*cmd, "console.log('EXP:'+[" + expr + "].join('|'))"],
                          capture_output=True, text=True, timeout=30)
    if proc.returncode: raise RuntimeError(f'{cmd[0]}: {proc.stderr[:400]}')
    lines = [line[4:] for line in proc.stdout.splitlines() if line.startswith('EXP:')]
    if len(lines) != 1: raise RuntimeError(f'{cmd[0]}: missing output {proc.stdout[:400]}')
    return lines[0].split('|')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', default='build/litebird')
    p.add_argument('--node', default='node')
    args = p.parse_args()
    values = cases()
    failures = 0
    try:
        for i in range(0, len(values), 128):
            batch = values[i:i+128]
            actual, expected = run([args.binary, '--run-js'], batch), run([args.node, '-e'], batch)
            if len(actual) != len(batch) or len(expected) != len(batch): raise RuntimeError('result count mismatch')
            for value, got, want in zip(batch, actual, expected):
                if got != want:
                    if failures < 15: print(f'FAIL {literal(value)}: Litebird={got!r}, Node={want!r}')
                    failures += 1
    except (OSError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1
    print(f'{len(values)-failures}/{len(values)} shortest exponential cases agree with Node')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
