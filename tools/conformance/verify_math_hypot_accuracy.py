#!/usr/bin/env python3
"""Grade multi-argument Math.hypot against Decimal roots and Node."""
import argparse
import decimal
import math
import random
import subprocess
import sys


def cases():
    out = [[], [0.0], [-0.0, -0.0, -0.0], [3.0, 4.0], [math.inf, math.nan, 1.0],
           [math.nan, 2.0, 3.0], [1e308, 1e308, 0.0], [5e-324, 5e-324, 5e-324],
           [1e-300, 2e-300, 3e-300]]
    for scale in (1e-200, 1.0, 1e200):
        for size in (3, 10, 50, 100, 300):
            for small in (1e-8, 2e-8, 3e-8, 1e-7):
                vals = [scale] + [scale * small] * size
                out.extend((vals, list(reversed(vals))))
    rng = random.Random(20261128)
    for _ in range(150):
        scale = 10.0 ** rng.randint(-280, 280)
        n = rng.randint(3, 65)
        out.append([scale * rng.uniform(-1, 1) * (10.0 ** -rng.randint(0, 12)) for _ in range(n)])
    return out


def reference(values):
    if any(math.isinf(x) for x in values): return math.inf
    if any(math.isnan(x) for x in values): return math.nan
    with decimal.localcontext() as ctx:
        ctx.prec = 140
        return float(sum((decimal.Decimal.from_float(x) ** 2 for x in values), decimal.Decimal(0)).sqrt())


def literal(x):
    if math.isnan(x): return 'NaN'
    if math.isinf(x): return '-Infinity' if x < 0 else 'Infinity'
    if x == 0 and math.copysign(1, x) < 0: return '-0'
    return repr(x)


def run(command, inputs):
    expressions = ["Math.hypot(" + ','.join(map(literal, row)) + ")" for row in inputs]
    script = "var a=[" + ','.join(expressions) + "];console.log('HYPOT:'+a.map(function(x){return x===0&&1/x===-Infinity?'-0':String(x)}).join('|'))"
    proc = subprocess.run([*command, script], capture_output=True, text=True, timeout=30)
    if proc.returncode: raise RuntimeError(f'{command[0]}: {proc.stderr[:400]}')
    lines = [line[6:] for line in proc.stdout.splitlines() if line.startswith('HYPOT:')]
    if len(lines) != 1: raise RuntimeError(f'{command[0]}: missing output {proc.stdout[:400]}')
    return lines[0].split('|')


def agrees(text, expected):
    try: actual = float(text)
    except ValueError: return False
    if math.isnan(expected): return math.isnan(actual)
    if math.isinf(expected): return actual == expected
    if expected == 0: return text == '0'
    return math.isfinite(actual) and abs(actual - expected) <= 2 * math.ulp(expected)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', default='build/litebird')
    p.add_argument('--node', default='node')
    args = p.parse_args()
    inputs = cases()
    failures = 0
    try:
        for i in range(0, len(inputs), 12):
            batch = inputs[i:i+12]
            actual, node = run([args.binary, '--run-js'], batch), run([args.node, '-e'], batch)
            if len(actual) != len(batch) or len(node) != len(batch): raise RuntimeError('result count mismatch')
            for row, got, other in zip(batch, actual, node):
                want = reference(row)
                if not agrees(got, want) or not agrees(other, want):
                    if failures < 12: print(f'FAIL length={len(row)} first={row[:3]}: Litebird={got}, Node={other}, Decimal={want}')
                    failures += 1
    except (OSError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1
    print(f'{len(inputs)-failures}/{len(inputs)} cases within two ulps of Decimal, both clients')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
