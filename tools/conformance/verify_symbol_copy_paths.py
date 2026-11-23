#!/usr/bin/env python3
"""Compare the symbol-copying reflection and CopyDataProperties paths with Node.

Covers:
  * `Object.getOwnPropertyDescriptors` (§20.1.2.9, Phase 181),
  * object spread `{...source}` (§13.2.5.5 -> §7.3.24 `CopyDataProperties`), and
  * object rest destructuring `{...rest} = source` (§14.3.3.3 -> §7.3.24).

Run: python3 tools/conformance/verify_symbol_copy_paths.py
"""
import argparse
import subprocess
import sys

CASES = {
    # The bug: a symbol key gets a descriptor entry.
    'getOwnPropertyDescriptors describes a symbol key': r"""
var s = Symbol('k');
var o = {a: 1}; o[s] = 2;
var d = Object.getOwnPropertyDescriptors(o);
console.log(Reflect.ownKeys(d).length, Object.keys(d).length,
            Object.getOwnPropertySymbols(d).length);
""",
    # The descriptor is the same one getOwnPropertyDescriptor gives.
    'the descriptor matches getOwnPropertyDescriptor': r"""
var s = Symbol('k');
var o = {}; o[s] = 7;
var one = Object.getOwnPropertyDescriptor(o, s);
var all = Object.getOwnPropertyDescriptors(o)[s];
console.log(one.value, all.value, one.writable === all.writable,
            one.enumerable === all.enumerable, one.configurable === all.configurable);
""",
    # The regression that a wrong-but-plausible fix produces: the enumerable
    # filter drops a non-enumerable symbol and a non-enumerable string.
    'non-enumerable keys are described too': r"""
var o = {};
Object.defineProperty(o, 'hidden', {value: 1, enumerable: false});
Object.defineProperty(o, Symbol('sh'), {value: 2, enumerable: false});
var d = Object.getOwnPropertyDescriptors(o);
console.log(Reflect.ownKeys(d).length, d.hidden.value, d[Object.getOwnPropertySymbols(d)[0]].value);
""",
    # Order and identity: the result's own keys are the source's own keys.
    'the result has the source keys in order': r"""
var s1 = Symbol('a'), s2 = Symbol('b');
var o = {}; o[2] = 'two'; o.z = 'zed'; o[s1] = 1; o[s2] = 2;
var d = Object.getOwnPropertyDescriptors(o);
console.log(Reflect.ownKeys(d).map(function(k){ return typeof k === 'symbol' ? 'S' : k; }).join(','));
console.log(d[s1].value, d[s2].value);
""",
    # Two symbols sharing a description stay distinct.
    'same-description symbols are distinct entries': r"""
var a = Symbol('same'), b = Symbol('same');
var o = {}; o[a] = 'A'; o[b] = 'B';
var d = Object.getOwnPropertyDescriptors(o);
console.log(Reflect.ownKeys(d).length, d[a].value, d[b].value, a === b);
""",
    # Strings only: nothing changed for the ordinary case.
    'a plain object is unchanged': r"""
var o = {x: 1, y: 2};
var d = Object.getOwnPropertyDescriptors(o);
console.log(JSON.stringify(Object.keys(d)), d.x.value, d.y.value);
""",
    # The descriptor view agrees with what the copy paths see.
    'the descriptor view and Reflect.ownKeys agree': r"""
var s = Symbol('k');
var o = {a: 1}; o[s] = 2;
Object.defineProperty(o, 'ne', {value: 3, enumerable: false});
var viaReflect = Reflect.ownKeys(o).length;
var viaDescriptors = Reflect.ownKeys(Object.getOwnPropertyDescriptors(o)).length;
console.log(viaReflect, viaDescriptors);
""",
    # Object spread copies own enumerable symbol keys alongside string keys.
    'object spread copies enumerable symbol keys': r"""
var s1 = Symbol('a'), s2 = Symbol('b');
var src = {x: 1, [s1]: 2, [s2]: 3};
var copy = {...src};
console.log(copy.x, copy[s1], copy[s2], Object.getOwnPropertySymbols(copy).length, Reflect.ownKeys(copy).length);
""",
    # Object spread skips non-enumerable symbols and non-enumerable strings.
    'object spread skips non-enumerable symbols': r"""
var sEnum = Symbol('e'), sHidden = Symbol('h');
var src = {a: 1, [sEnum]: 2};
Object.defineProperty(src, 'hidden', {value: 3, enumerable: false});
Object.defineProperty(src, sHidden, {value: 4, enumerable: false});
var copy = {...src};
console.log(copy.a, copy[sEnum], 'hidden' in copy, sHidden in copy, Object.getOwnPropertySymbols(copy).length);
""",
    # Object spread preserves [[OwnPropertyKeys]] getter order (indices, strings, symbols)
    # and distinct symbols sharing a description.
    'object spread order and distinct same-description symbols': r"""
var s1 = Symbol('dup'), s2 = Symbol('dup');
var log = [];
var src = {};
Object.defineProperty(src, s1, {enumerable: true, get: function(){ log.push('s1'); return 'A'; }});
Object.defineProperty(src, 'b', {enumerable: true, get: function(){ log.push('b'); return 'B'; }});
Object.defineProperty(src, '2', {enumerable: true, get: function(){ log.push('2'); return 'two'; }});
Object.defineProperty(src, s2, {enumerable: true, get: function(){ log.push('s2'); return 'C'; }});
Object.defineProperty(src, '0', {enumerable: true, get: function(){ log.push('0'); return 'zero'; }});
var copy = {...src};
console.log(log.join(','), copy[0], copy[2], copy.b, copy[s1], copy[s2]);
""",
    # Object spread writes with CreateDataProperty (not [[Set]]): earlier own
    # setters and prototype setters are not invoked.
    'object spread uses CreateDataProperty instead of Set': r"""
var s = Symbol('setter');
var calls = [];
var proto = {};
Object.defineProperty(proto, 'p', {set: function(v){ calls.push('proto:' + v); }});
Object.defineProperty(proto, s, {set: function(v){ calls.push('protoSym:' + v); }});
var target = {
    __proto__: proto,
    set own(v) { calls.push('own:' + v); },
    set [s](v) { calls.push('ownSym:' + v); },
    ...{own: 1, p: 2, [s]: 3}
};
var dOwn = Object.getOwnPropertyDescriptor(target, 'own');
var dSym = Object.getOwnPropertyDescriptor(target, s);
console.log(calls.length, target.own, target.p, target[s], dOwn.value, dOwn.writable, dSym.value, dSym.writable);
""",
    # Object rest destructuring copies own enumerable symbol keys.
    'object rest copies enumerable symbol keys': r"""
var s1 = Symbol('a'), s2 = Symbol('b');
var src = {x: 1, y: 2, [s1]: 3, [s2]: 4};
var {x, ...rest} = src;
console.log(x, rest.y, rest[s1], rest[s2], 'x' in rest, Object.getOwnPropertySymbols(rest).length);
""",
    # Object rest destructuring excludes destructured computed symbol keys.
    'object rest excludes destructured symbol keys': r"""
var s1 = Symbol('drop'), s2 = Symbol('keep');
var src = {a: 1, [s1]: 10, [s2]: 20};
var {[s1]: picked, ...rest} = src;
console.log(picked, rest.a, s1 in rest, rest[s2], Object.getOwnPropertySymbols(rest).length);
""",
    # Object rest destructuring skips non-enumerable symbol and string keys.
    'object rest skips non-enumerable symbols': r"""
var sEnum = Symbol('e'), sHidden = Symbol('h');
var src = {a: 1, b: 2, [sEnum]: 3};
Object.defineProperty(src, 'ne', {value: 4, enumerable: false});
Object.defineProperty(src, sHidden, {value: 5, enumerable: false});
var {a, ...rest} = src;
console.log(a, rest.b, rest[sEnum], 'ne' in rest, sHidden in rest, Object.getOwnPropertySymbols(rest).length);
""",
    # Object rest in assignment and function parameter destructuring.
    'object rest in assignment and parameter patterns': r"""
var s1 = Symbol('one'), s2 = Symbol('two');
var src = {a: 1, [s1]: 2, [s2]: 3};
var picked, assignRest;
({[s1]: picked, ...assignRest} = src);
function take({a, ...paramRest}) { return [a, paramRest[s1], paramRest[s2]]; }
var p = take(src);
console.log(picked, assignRest.a, s1 in assignRest, assignRest[s2], p.join(','));
""",
    # Live enumerability in CopyDataProperties (ECMA-262 7.3.24 step 6.c):
    # deleting a later key skips it, flipping enumerable hides/reveals it,
    # and newly added keys are not visited.
    'CopyDataProperties checks enumerability live per key': r"""
var sHide = Symbol('hide'), sReveal = Symbol('reveal'), sNew = Symbol('new');
var src = {};
Object.defineProperty(src, 'trigger', {
    enumerable: true,
    get: function() {
        delete this.removed;
        Object.defineProperty(this, sHide, {enumerable: false});
        Object.defineProperty(this, sReveal, {enumerable: true});
        this[sNew] = 99;
        return 1;
    }
});
src.removed = 2;
src[sHide] = 3;
Object.defineProperty(src, sReveal, {value: 4, enumerable: false, configurable: true});
var spread = {...src};
console.log(spread.trigger, 'removed' in spread, sHide in spread, spread[sReveal], sNew in spread);
""",
    # Object rest uses CreateDataProperty and does not trigger Object.prototype setters.
    'object rest does not trigger Object.prototype setters': r"""
var s = Symbol('protoRest');
var calls = 0;
Object.defineProperty(Object.prototype, s, {
    configurable: true,
    set: function() { calls++; }
});
try {
    var src = {[s]: 42, a: 1};
    var {a, ...rest} = src;
    console.log(calls, a, rest[s], Object.getOwnPropertyDescriptor(rest, s).value);
} finally {
    delete Object.prototype[s];
}
""",
    # Spread and rest on strings, sparse arrays, TypedArrays, functions with symbols, nullish.
    'spread and rest across strings arrays typed arrays functions and nullish': r"""
var s = Symbol('fn');
function fn() {}
fn.x = 1;
fn[s] = 2;
var sparse = [10, , 30];
sparse[s] = 40;
var ta = new Uint8Array([5, 6]);
ta[s] = 7;
var strSpread = {...'hi'};
var {0: first, ...strRest} = 'hi';
var fnCopy = {...fn};
var arrCopy = {...sparse};
var taCopy = {...ta};
var nullCopy = {...null, ...undefined, ok: 1};
console.log(JSON.stringify(strSpread), first, JSON.stringify(strRest),
            fnCopy.x, fnCopy[s], 'name' in fnCopy,
            arrCopy[0], '1' in arrCopy, arrCopy[2], arrCopy[s],
            taCopy[0], taCopy[1], taCopy[s], nullCopy.ok);
""",
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    failures = 0
    for name, source in CASES.items():
        try:
            node = subprocess.run([args.node, '-e', source], capture_output=True, text=True, timeout=15)
            bird = subprocess.run([args.binary, '--run-js', source], capture_output=True,
                                  text=True, timeout=15)
        except (OSError, subprocess.TimeoutExpired) as error:
            print(f'FAIL {name}: {error}')
            failures += 1
            continue
        if node.returncode or bird.returncode or node.stdout != bird.stdout:
            print(f'FAIL {name}\n  node ({node.returncode}): {node.stdout.strip()!r} {node.stderr.strip()[:300]!r}'
                  f'\n  litebird ({bird.returncode}): {bird.stdout.strip()!r} {bird.stderr.strip()[:300]!r}')
            failures += 1
        else:
            print(f'PASS {name}: {node.stdout.strip()}')
    print(f'{len(CASES) - failures}/{len(CASES)} match Node')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
