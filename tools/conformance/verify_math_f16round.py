#!/usr/bin/env python3
"""Compare binary16 rounding with Node >=24 and Python's struct 'e'.

Tests every positive finite binary16 value and every midpoint to its successor,
including the two adjacent binary64 values, each with both signs, plus seeded
random binary64 inputs. Runs in batches to keep command-line sizes bounded.
Neither reference calls Litebird's implementation.
"""
import argparse
import math
import random
import struct
import subprocess
import sys


def python_half(value):
    try:
        return struct.unpack('>e', struct.pack('>e', value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def numbers():
    # 0..0x7bff are positive finite binary16 values in increasing order.
    for bits in range(0x7c00):
        value = struct.unpack('>e', struct.pack('>H', bits))[0]
        yield value
        yield -value
        following = (struct.unpack('>e', struct.pack('>H', bits + 1))[0]
                     if bits < 0x7bff else 65536.0)
        mid = (value + following) / 2
        for candidate in (math.nextafter(mid, -math.inf), mid, math.nextafter(mid, math.inf)):
            yield candidate
            yield -candidate
    rng = random.Random(20261124)
    for _ in range(10000):
        yield struct.unpack('>d', rng.getrandbits(64).to_bytes(8, 'big'))[0]
    yield math.inf
    yield -math.inf
    yield math.nan


def literal(x):
    if math.isnan(x): return 'NaN'
    if math.isinf(x): return '-Infinity' if x < 0 else 'Infinity'
    return repr(x)


def run_command(command, source):
    proc = subprocess.run([*command, source], capture_output=True, text=True, timeout=30)
    if proc.returncode != 0:
        raise RuntimeError(f'{command[0]} failed: {proc.stderr[:1000]}')
    lines = proc.stdout.strip().splitlines()
    line = next((x[4:] for x in lines if x.startswith('F16:')), None)
    if line is None:
        raise RuntimeError(f'{command[0]} produced no F16 result: {proc.stdout[:1000]} {proc.stderr[:1000]}')
    return line.split(',')


def equal(actual, expected):
    if math.isnan(expected): return actual == 'NaN'
    if expected == 0: return actual == ('-0' if math.copysign(1, expected) < 0 else '0')
    try: return float(actual) == expected
    except ValueError: return False


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', default='build/litebird')
    p.add_argument('--node', default='build/node-reference/node_modules/node/bin/node')
    p.add_argument('--batch-size', type=int, default=512)
    args = p.parse_args()
    if args.batch_size < 1 or args.batch_size > 1000: p.error('batch size must be 1..1000')
    checks = failures = 0
    batch = []

    def compare(batch):
        nonlocal checks, failures
        source = ('var vals=[' + ','.join(map(literal, batch)) + '];' +
                  "console.log('F16:' + vals.map(function(x) { var y=Math.f16round(x); "
                  "return y===0 && 1/y===-Infinity ? '-0' : String(y); }).join(','));")
        engine = run_command([args.binary, '--run-js'], source)
        node = run_command([args.node, '-e'], source)
        if len(engine) != len(batch) or len(node) != len(batch):
            raise RuntimeError(f'wrong result count: engine {len(engine)}, Node {len(node)}, expected {len(batch)}')
        for x, a, b in zip(batch, engine, node):
            expected = python_half(x)
            good = equal(a, expected) and equal(b, expected)
            if not good and failures < 10:
                print(f'FAIL {literal(x)}: Litebird={a} Node={b} Python={literal(expected)}')
            checks += 1
            failures += not good

    try:
        for value in numbers():
            batch.append(value)
            if len(batch) == args.batch_size:
                compare(batch)
                batch = []
        if batch: compare(batch)
    except (OSError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1
    print(f'{checks} binary16 comparisons; {failures} failure(s).')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
