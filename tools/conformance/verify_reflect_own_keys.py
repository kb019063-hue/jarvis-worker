#!/usr/bin/env python3
"""Compare Reflect.ownKeys with Node, including the symbol keys it must report.

§28.1.11 is `[[OwnPropertyKeys]]`: every own key, the string keys in index-then
creation order and after every one of them the symbols in creation order, and
the array holds the symbols themselves rather than their descriptions.

`Reflect.ownKeys` used to answer with strings only, so a symbol key was invisible
to it while `Object.assign` on the same object copied that key — the engine
disagreed with itself about what an object's own keys are.

The cases below check the count, the order, the types, and that a returned
symbol is the *same* symbol (identity, not a description that happens to match),
because a fix that returned `Symbol(description)` would pass a count-and-order
test while breaking `o[key]`.

Run: python3 tools/conformance/verify_reflect_own_keys.py
"""
import argparse
import subprocess
import sys

CASES = {
    'string keys only': r"""
var o = {}; o.b = 1; o.a = 2;
console.log(JSON.stringify(Reflect.ownKeys(o)));
""",
    # The bug: a symbol key is an own key.
    'a symbol key is reported': r"""
var s = Symbol('k'); var o = {a: 1}; o[s] = 2;
var keys = Reflect.ownKeys(o);
console.log(keys.length, typeof keys[0], typeof keys[1], keys[1] === s);
""",
    # Strings first, then symbols — whatever order they were created in.
    'strings come before symbols': r"""
var o = {};
o[Symbol('s1')] = 1;
o.b = 1;
o[Symbol('s2')] = 2;
o.a = 2;
console.log(Reflect.ownKeys(o).map(function(k){ return typeof k === 'symbol' ? 'S' : k; }).join(''));
""",
    # Indices ascend, then the string keys in creation order, then symbols.
    'indices ascend before strings and symbols': r"""
var o = {};
o.z = 1;
o[2] = 1;
o[0] = 1;
o[Symbol('k')] = 1;
console.log(Reflect.ownKeys(o).map(function(k){ return typeof k === 'symbol' ? 'S' : k; }).join(','));
""",
    # The array holds the symbol itself, so it indexes back into the object.
    'the returned symbol is the key': r"""
var s = Symbol('k'); var o = {}; o[s] = 'found';
var back = Reflect.ownKeys(o)[0];
console.log(o[back]);
""",
    # Two symbols with the same description are still two keys.
    'same-description symbols are distinct keys': r"""
var a = Symbol('same'), b = Symbol('same');
var o = {}; o[a] = 1; o[b] = 2;
var keys = Reflect.ownKeys(o);
console.log(keys.length, keys[0] === a, keys[1] === b, o[keys[0]], o[keys[1]]);
""",
    # A symbol whose description is empty is still a key.
    'an undescribed symbol is a key': r"""
var s = Symbol(); var o = {}; o[s] = 1;
var keys = Reflect.ownKeys(o);
console.log(keys.length, typeof keys[0], keys[0] === s);
""",
    # Non-enumerable keys are own keys too.
    'non-enumerable keys are reported': r"""
var o = {};
Object.defineProperty(o, 'hidden', {value: 1, enumerable: false});
Object.defineProperty(o, Symbol('sh'), {value: 2, enumerable: false});
console.log(Reflect.ownKeys(o).length, Reflect.ownKeys(o).map(function(k){ return typeof k; }).join(','));
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
