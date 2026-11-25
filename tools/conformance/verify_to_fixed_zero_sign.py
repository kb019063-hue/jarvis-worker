#!/usr/bin/env python3
"""Compare toFixed zero-sign and rounding-boundary cases against Node."""
import argparse
import math
import subprocess
import sys


def cases():
    out = []
    for digits in (0, 1, 2, 3, 6, 10, 20, 50, 100):
        half = 0.5 * 10.0 ** -digits
        values = [0.0, -0.0, -5e-324, 5e-324, -1e-300, 1e-300,
                  -half, half, -math.nextafter(half, 0.0), math.nextafter(half, 0.0),
                  -math.nextafter(half, math.inf), math.nextafter(half, math.inf),
                  -2 * half, 2 * half, -1.25, 1.25]
        out.extend((x, digits) for x in values)
    return out


def run(command, inputs):
    literals = [('-0' if x == 0 and math.copysign(1, x) < 0 else repr(x), d)
                for x, d in inputs]
    script = "console.log('FIXED:'+[" + ','.join(
        f'({x}).toFixed({d})' for x, d in literals) + "].join('|'))"
    proc = subprocess.run([*command, script], capture_output=True, text=True, timeout=30)
    if proc.returncode:
        raise RuntimeError(f'{command[0]} exited {proc.returncode}: {proc.stderr[:400]}')
    lines = [line[6:] for line in proc.stdout.splitlines() if line.startswith('FIXED:')]
    if len(lines) != 1:
        raise RuntimeError(f'{command[0]} did not print a result: {proc.stdout[:400]}')
    return lines[0].split('|')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    inputs = cases()
    try:
        actual = run([args.binary, '--run-js'], inputs)
        expected = run([args.node, '-e'], inputs)
        if len(actual) != len(inputs) or len(expected) != len(inputs):
            raise RuntimeError('result count mismatch')
        failures = 0
        for case, got, want in zip(inputs, actual, expected):
            if got != want:
                if failures < 15:
                    print(f'FAIL toFixed{case}: Litebird={got!r}, Node={want!r}')
                failures += 1
        print(f'{len(inputs)-failures}/{len(inputs)} cases agree with Node')
        return int(bool(failures))
    except (OSError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
