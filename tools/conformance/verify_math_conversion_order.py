#!/usr/bin/env python3
"""Compare Math.atan2 and Math.pow conversion order with Node.js.

The cases exercise normal coercion, abrupt completion, NaN and infinity,
mutation of the other argument, and numeric orientation. Control cases also
cover imul, min, max and hypot. Run with a baseline or new Litebird binary.
"""
import argparse
import subprocess
import sys

CASES = []
for name in ('atan2', 'pow'):
    CASES.extend([
        (name + ' ordered', f"var log=[]; var a={{valueOf:function(){{log.push('a');return 2}}}}, b={{valueOf:function(){{log.push('b');return 3}}}}; var r=Math.{name}(a,b); console.log(log.join('/')+':'+String(r===Math.{name}(2,3)));"),
        (name + ' first throws', f"var log=[]; var a={{valueOf:function(){{log.push('a');throw 'first'}}}}, b={{valueOf:function(){{log.push('b');throw 'second'}}}}; try{{Math.{name}(a,b)}}catch(e){{log.push(e)}} console.log(log.join('/'));"),
        (name + ' second throws', f"var log=[]; var a={{valueOf:function(){{log.push('a');return 2}}}}, b={{valueOf:function(){{log.push('b');throw 'second'}}}}; try{{Math.{name}(a,b)}}catch(e){{log.push(e)}} console.log(log.join('/'));"),
        (name + ' first mutates second', f"var log=[];var b={{valueOf:function(){{log.push('old');return 3}}}};var a={{valueOf:function(){{log.push('a');b.valueOf=function(){{log.push('new');return 4}};return 2}}}};var r=Math.{name}(a,b);console.log(log.join('/')+':'+String(r===Math.{name}(2,4)));"),
        (name + ' first NaN', f"var log=[];var a={{valueOf:function(){{log.push('a');return NaN}}}},b={{valueOf:function(){{log.push('b');return 1}}}};var r=Math.{name}(a,b);console.log(log.join('/')+':'+String(isNaN(r)));"),
        (name + ' second symbol', f"var log=[];var a={{valueOf:function(){{log.push('a');return 1}}}};try{{Math.{name}(a,Symbol())}}catch(e){{log.push(e.name)}}console.log(log.join('/'));"),
        (name + ' first symbol', f"var log=[];var b={{valueOf:function(){{log.push('b');return 2}}}};try{{Math.{name}(Symbol(),b)}}catch(e){{log.push(e.name)}}console.log(log.join('/'));"),
        (name + ' second missing', f"var log=[];var a={{valueOf:function(){{log.push('a');return 1}}}};console.log(String(isNaN(Math.{name}(a)))+':'+log.join('/'));"),
    ])
for name in ('imul', 'min', 'max', 'hypot'):
    CASES.append((name + ' control', f"var log=[];var a={{valueOf:function(){{log.push('a');return 2}}}},b={{valueOf:function(){{log.push('b');return 3}}}};Math.{name}(a,b);console.log(log.join('/'));"))
CASES.extend([
    ('atan2 signs', "console.log([1/Math.atan2(-0,1),Math.atan2(0,-1)===Math.PI,Math.atan2(1,0)===Math.PI/2].join('/'))"),
    ('pow orientation', "console.log([Math.pow(2,3),Math.pow(-0,3),1/Math.pow(-0,3),Math.pow(3,2)].join('/'))"),
])


def run(command, source):
    result = subprocess.run([*command, source], capture_output=True, text=True, timeout=15)
    if result.returncode:
        raise RuntimeError(f'{command[0]} rc={result.returncode}: {result.stderr[:600]}')
    return result.stdout.strip()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', default='build/litebird')
    p.add_argument('--node', default='node')
    args = p.parse_args()
    failures = 0
    for name, script in CASES:
        try:
            expected = run([args.node, '-e'], script)
            observed = run([args.binary, '--run-js'], script)
        except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
            print(f'ERROR {name}: {exc}', file=sys.stderr)
            return 1
        ok = observed == expected
        failures += not ok
        print(f'{"PASS" if ok else "FAIL"} {name}: Litebird={observed!r}, Node={expected!r}')
    print(f'{len(CASES) - failures}/{len(CASES)} Math conversion cases match Node.')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
