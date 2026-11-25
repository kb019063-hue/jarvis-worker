#!/usr/bin/env python3
"""Compare Math.cbrt against Decimal's high-precision real cube root and Node.

Both JavaScript implementations are graded independently to within four
binary64 ulps of a 120-decimal-digit reference; exact equality is not assumed
for an implementation-approximated Math function. Seeded random finite double
bit patterns exercise the whole exponent range.
"""
import argparse
import decimal
import math
import random
import struct
import subprocess
import sys


def inputs():
    vals = [0.0, -0.0, 1.0, -1.0, 8.0, -8.0, 27.0, -27.0,
            float.fromhex('0x1.fffffffffffffp+1023'),
            float.fromhex('0x1.ffffffffffffep+1023'),
            -float.fromhex('0x1.fffffffffffffp+1023'),
            float.fromhex('0x1.0000000000000p-1022'),
            float.fromhex('0x0.0000000000001p-1022'),
            -float.fromhex('0x0.0000000000001p-1022'),
            1e-320, 1e-308, 1e308, math.inf, -math.inf, math.nan]
    rng = random.Random(20261125)
    for _ in range(4096):
        x = struct.unpack('>d', rng.getrandbits(64).to_bytes(8, 'big'))[0]
        if math.isfinite(x): vals.append(x)
    return vals


def literal(x):
    if math.isnan(x): return 'NaN'
    if math.isinf(x): return '-Infinity' if x < 0 else 'Infinity'
    return repr(x)


def reference(value):
    if not math.isfinite(value) or value == 0: return value
    with decimal.localcontext() as ctx:
        ctx.prec = 120
        a = decimal.Decimal.from_float(abs(value))
        result = ctx.power(a, decimal.Decimal(1) / decimal.Decimal(3))
        return math.copysign(float(result), value)


def run(command, source):
    proc = subprocess.run([*command, source], capture_output=True, text=True, timeout=30)
    if proc.returncode: raise RuntimeError(f'{command[0]}: {proc.stderr[:1000]}')
    lines = [line[5:] for line in proc.stdout.splitlines() if line.startswith('CBRT:')]
    if len(lines) != 1: raise RuntimeError(f'{command[0]}: missing result {proc.stdout[:300]}')
    return lines[0].split(',')


def agrees(text, expected):
    if math.isnan(expected): return text == 'NaN'
    if expected == 0: return text == ('-0' if math.copysign(1, expected) < 0 else '0')
    try: actual = float(text)
    except ValueError: return False
    if math.isinf(expected): return actual == expected
    return math.isfinite(actual) and abs(actual - expected) <= 4 * math.ulp(expected)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', default='build/litebird')
    p.add_argument('--node', default='node')
    args = p.parse_args()
    vals = inputs()
    failures = 0
    try:
        for pos in range(0, len(vals), 256):
            batch = vals[pos:pos + 256]
            source = ('var a=[' + ','.join(map(literal, batch)) + '];' +
                      "console.log('CBRT:'+a.map(function(x){var y=Math.cbrt(x);"
                      "return y===0&&1/y===-Infinity?'-0':String(y)}).join(','));")
            bird = run([args.binary, '--run-js'], source)
            node = run([args.node, '-e'], source)
            if len(bird) != len(batch) or len(node) != len(batch): raise RuntimeError('result count mismatch')
            for x, a, b in zip(batch, bird, node):
                expected = reference(x)
                ok = agrees(a, expected) and agrees(b, expected)
                if not ok and failures < 12:
                    print(f'FAIL {literal(x)}: Litebird={a}, Node={b}, Decimal={literal(expected)}')
                failures += not ok
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1
    print(f'{len(vals) - failures}/{len(vals)} cube roots within four ulps of Decimal, both clients.')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
