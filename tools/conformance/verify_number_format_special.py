#!/usr/bin/env python3
"""Compare Number formatting's argument coercion and special values with Node."""
import argparse
import subprocess
import sys

CASES = []
for method in ('toFixed', 'toExponential', 'toPrecision'):
    for value in ('NaN', 'Infinity', '-Infinity', '1'):
        for digits in ('undefined', '-1', '0', '1', '100', '101', 'Infinity', '-Infinity', 'NaN'):
            CASES.append(f'Number.prototype.{method}.call({value},{digits})')
    for value in ('NaN', 'Infinity', '1'):
        for arg in ("{valueOf:function(){log.push('valueOf');return 101}}",
                    "{valueOf:function(){log.push('valueOf');throw 'boom'}}",
                    'Symbol()'):
            CASES.append(f'Number.prototype.{method}.call({value},{arg})')
    CASES.append(f'Number.prototype.{method}.call({{}},{{valueOf:function(){{log.push("wrong");return 1}}}})')


def run(cmd, expr):
    script = ("var log=[];try{var result=" + expr + ";log.push('result:'+result)}"
              "catch(e){log.push('throw:'+(e.name||e))}console.log('RESULT:'+log.join('/'))")
    proc = subprocess.run([*cmd, script], text=True, capture_output=True, timeout=10)
    if proc.returncode:
        raise RuntimeError(f'{cmd[0]} exited {proc.returncode}: {proc.stderr[:300]}')
    lines = [line[7:] for line in proc.stdout.splitlines() if line.startswith('RESULT:')]
    if len(lines) != 1:
        raise RuntimeError(f'{cmd[0]}: missing result: {proc.stdout[:300]}')
    return lines[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    failures = 0
    try:
        for case in CASES:
            expected = run([args.node, '-e'], case)
            actual = run([args.binary, '--run-js'], case)
            if actual != expected:
                if failures < 20:
                    print(f'FAIL {case}: Litebird={actual}; Node={expected}')
                failures += 1
    except (OSError, subprocess.SubprocessError, RuntimeError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1
    print(f'{len(CASES)-failures}/{len(CASES)} cases agree with Node')
    return int(failures != 0)


if __name__ == '__main__':
    raise SystemExit(main())
