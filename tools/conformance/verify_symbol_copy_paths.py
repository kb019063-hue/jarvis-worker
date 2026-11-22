#!/usr/bin/env python3
"""Compare the symbol-copying reflection paths with Node.

`Object.getOwnPropertyDescriptors` is §20.1.2.9: for each key of
`[[OwnPropertyKeys]]`, a descriptor entry under that key. A symbol key therefore
becomes a *symbol-keyed* entry in the result, not a string-keyed one and not
nothing.

It took the strings-only key list, so a symbol-keyed property had no descriptor
entry — while `Object.assign` on the same object copied it. This checks the
descriptor view against the keys `Reflect.ownKeys` reports, and against
`getOwnPropertySymbols`, so the three paths are compared to each other rather
than each to its own expectation.

Two failure modes are deliberately covered, because both were live while this
was written:

  * the strings-only list (the original bug), and
  * `assign_keys(object, true)` — the *enumerable* filter, which is right for
    `Object.assign` and wrong here: it drops a non-enumerable key. This is the
    regression that this file's sibling in Phase 175 actually produced.

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
