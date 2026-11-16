#!/usr/bin/env python3
"""End-to-end float placement benchmark and deterministic layout differential corpus.

Run: python3 tools/bench/bench_sized_floats.py build/litebird --out build/floats-after
For a before/after diff, save the old binary and run this twice with distinct --out.
All files (including the input pages and full layout dumps) are written under --out.
"""
import argparse
from pathlib import Path
import random
import subprocess
import time


def page(n, pattern):
    rng = random.Random(48154 + n)
    parts = ['<!doctype html><body style="margin:0"><div id="box" style="width:800px;overflow:hidden">']
    for i in range(n):
        if pattern == 'rows':
            side, width, height, margins = 'left', 100, 10, ''
        elif pattern == 'mixed':
            side = rng.choice(('left', 'right'))
            width, height = rng.randrange(30, 260), rng.randrange(1, 85)
            margins = f'margin-left:{rng.randrange(-25, 16)}px;margin-right:{rng.randrange(-25, 16)}px;'
        else:  # sparse clearance and changes in y_cursor
            side = 'right' if i % 3 == 0 else 'left'
            width, height = (i * 73) % 370 + 10, (i * 19) % 55 + 1
            margins = f'margin-top:{(-4 if i % 7 == 0 else 0)}px;'
            if i % 11 == 0:
                parts.append('<div style="clear:both;height:3px"></div>')
        parts.append(f'<div id="f{i}" style="float:{side};width:{width}px;height:{height}px;{margins}"></div>')
    parts.append('</div></body>')
    return ''.join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--sizes', type=int, nargs='+', default=[1000, 3000])
    parser.add_argument('--max-seconds', type=float, help='fail if any page exceeds this wall time')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    binary = args.binary.resolve()
    for pattern in ('rows', 'mixed', 'clear'):
        for n in args.sizes:
            name = f'{pattern}-{n}'
            html = args.out / f'{name}.html'
            html.write_text(page(n, pattern))
            start = time.perf_counter()
            result = subprocess.run([str(binary), str(html), '--dump-layout'], capture_output=True,
                                    text=True, timeout=180, check=True)
            elapsed = time.perf_counter() - start
            (args.out / f'{name}.layout').write_text(result.stdout)
            print(f'{name}: {elapsed:.3f}s', flush=True)
            if args.max_seconds is not None and elapsed > args.max_seconds:
                raise SystemExit(f'{name} exceeded {args.max_seconds}s budget')


if __name__ == '__main__':
    main()
