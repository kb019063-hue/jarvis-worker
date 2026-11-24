#!/usr/bin/env python3
"""Compare Math.sumPrecise to Node's V8 implementation and Python exact rationals.

Node 26 currently exposes it behind --js-sum-precise. Python computes an exact
rational and rounds once, independently of both implementations. No external
network is needed after setup.
"""
import argparse
from fractions import Fraction
import math
import random
import struct
import subprocess
import sys


def literal(x):
    if math.isnan(x): return 'NaN'
    if math.isinf(x): return '-Infinity' if x < 0 else 'Infinity'
    return repr(x)


def exact(values):
    if any(math.isnan(x) for x in values): return math.nan
    pos = any(x == math.inf for x in values)
    neg = any(x == -math.inf for x in values)
    if pos and neg: return math.nan
    if pos: return math.inf
    if neg: return -math.inf
    total = sum((Fraction.from_float(x) for x in values), Fraction())
    if total == 0:
        return -0.0 if all(x == 0 and math.copysign(1, x) < 0 for x in values) else 0.0
    try: return float(total)
    except OverflowError: return math.copysign(math.inf, -1 if total < 0 else 1)


def cases():
    maxv = float.fromhex('0x1.fffffffffffffp+1023')
    tiny = float.fromhex('0x0.0000000000001p-1022')
    yield from [[], [-0.0], [0.0], [0.0, -0.0], [1.0, -1.0],
                [1e16, 1.0, -1e16], [maxv, maxv, -maxv], [maxv, maxv],
                [maxv, math.ldexp(1.0, 969)], [maxv, math.ldexp(1.0, 970)],
                [tiny, tiny, -tiny], [math.inf, -math.inf], [math.nan, 42.0]]
    rng = random.Random(20261124)
    for _ in range(10000):
        xs = []
        for j in range(rng.randrange(1, 14)):
            if xs and rng.random() < 0.26:
                xs.append(-rng.choice(xs))
            else:
                x = struct.unpack('>d', rng.getrandbits(64).to_bytes(8, 'big'))[0]
                xs.append(x if math.isfinite(x) else 0.0)
        yield xs


def invoke(command, script):
    result = subprocess.run([*command, script], capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise RuntimeError(f'{command[0]}: {result.stderr[:1000]}')
    lines = [s[4:] for s in result.stdout.splitlines() if s.startswith('SUM:')]
    if len(lines) != 1: raise RuntimeError(f'missing result: {result.stdout[:500]} {result.stderr[:500]}')
    return lines[0].split(',')


def matches(text, value):
    if math.isnan(value): return text == 'NaN'
    if value == 0: return text == ('-0' if math.copysign(1, value) < 0 else '0')
    try: return float(text) == value
    except ValueError: return False


SEMANTIC_CASES = [
    ('method shape', "var d=Object.getOwnPropertyDescriptor(Math,'sumPrecise'); return [Math.sumPrecise.name,Math.sumPrecise.length,d.writable,d.enumerable,d.configurable].join('/')"),
    ('non constructor', "try {new Math.sumPrecise([]);return 'none'}catch(e){return e.name}"),
    ('non iterable', "try {Math.sumPrecise(7);return 'none'}catch(e){return e.name}"),
    ('number object rejected', "try {Math.sumPrecise([new Number(1)]);return 'none'}catch(e){return e.name}"),
    ('array hole rejected', "try {Math.sumPrecise([,]);return 'none'}catch(e){return e.name}"),
    ('nan does not stop iteration', "var seen=[];var i=0;var o={};o[Symbol.iterator]=function(){return {next:function(){seen.push('next');i++;return i===1?{value:NaN,done:false}:i===2?{value:'bad',done:false}:{done:true}},return:function(){seen.push('return');return {done:true}}}};try {Math.sumPrecise(o)}catch(e){seen.push(e.name)} return seen.join('/')"),
    ('iterable lookup once', "var log=[];var o={};var desc={};desc.get=function(){log.push('get');return function(){log.push('call');return {next:function(){log.push('next');return {done:true}}}}};Object.defineProperty(o,Symbol.iterator,desc);var r=Math.sumPrecise(o);return log.join('/')+'/'+(1/r)"),
    ('next throw is not closed', "var log=[];var o={};o[Symbol.iterator]=function(){return {next:function(){log.push('next');throw 7},return:function(){log.push('return');return {done:true}}}};try{Math.sumPrecise(o)}catch(e){log.push(e)} return log.join('/')"),
]


def semantic(binary, node):
    failures = 0
    for name, body in SEMANTIC_CASES:
        source = "console.log('SUM:'+(function(){" + body + "})());"
        actual = invoke([binary, '--run-js'], source)
        expected = invoke([node, '--js-sum-precise', '-e'], source)
        ok = actual == expected
        failures += not ok
        print(f'{"PASS" if ok else "FAIL"} {name}: Litebird={actual[0]} Node={expected[0]}')
    return failures


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', default='build/litebird')
    p.add_argument('--node', default='build/node-reference/node_modules/node/bin/node')
    p.add_argument('--batch-size', type=int, default=50)
    args = p.parse_args()
    if not 1 <= args.batch_size <= 100: p.error('batch size must be 1..100')
    n = bad = 0
    batch = []

    def compare(batch):
        nonlocal n, bad
        source = 'var cases=[' + ','.join('[' + ','.join(map(literal, xs)) + ']' for xs in batch) + '];' + (
            "console.log('SUM:' + cases.map(function(a){var x=Math.sumPrecise(a);"
            "return x===0 && 1/x===-Infinity ? '-0' : String(x);}).join(','));")
        a = invoke([args.binary, '--run-js'], source)
        b = invoke([args.node, '--js-sum-precise', '-e'], source)
        if len(a) != len(batch) or len(b) != len(batch): raise RuntimeError('result count mismatch')
        for xs, bird, node in zip(batch, a, b):
            expected = exact(xs)
            good = matches(bird, expected) and matches(node, expected)
            if not good and bad < 10:
                print(f'FAIL {list(map(literal, xs))}: Litebird={bird} Node={node} Python={literal(expected)}')
            n += 1
            bad += not good

    try:
        for item in cases():
            batch.append(item)
            if len(batch) == args.batch_size:
                compare(batch)
                batch = []
        if batch: compare(batch)
    except (OSError, subprocess.SubprocessError, RuntimeError) as error:
        print('ERROR:', error, file=sys.stderr)
        return 1
    try:
        semantic_bad = semantic(args.binary, args.node)
    except (OSError, subprocess.SubprocessError, RuntimeError) as error:
        print('ERROR:', error, file=sys.stderr)
        return 1
    print(f'{n} exact-sum comparisons; {bad} failure(s).')
    print(f'{len(SEMANTIC_CASES)} semantic comparisons; {semantic_bad} failure(s).')
    return int(bool(bad or semantic_bad))


if __name__ == '__main__':
    raise SystemExit(main())
