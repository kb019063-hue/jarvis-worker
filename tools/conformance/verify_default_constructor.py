#!/usr/bin/env python3
"""Compare what a default constructor does to the array iterator with Node.

The implicit constructor of a derived class is `constructor(...args) {
super(...args); }`, but the language defines it as forwarding the arguments
list, not as iterating it (ECMA-262 15.7.14). So `new Derived(1, 2)` must not
call a script's patched `Array.prototype[Symbol.iterator]`.

The cases below exist in pairs. Each `*_must_ask` case is there to stop the fix
being made by shape rather than by provenance: a constructor a script *wrote*
with the same text has to keep asking, and a fix that looked only at "has a rest
parameter and a super call" would silence it while making the first case pass.

Run: python3 tools/conformance/verify_default_constructor.py
"""
import argparse
import subprocess
import sys

# Each case returns a count of iterator calls. Node is the oracle.
PROBE = r"""
function probe(fn) {
  var n = 0;
  var real = Array.prototype[Symbol.iterator];
  Array.prototype[Symbol.iterator] = function() { n++; return real.call(this); };
  try { fn(); } finally { Array.prototype[Symbol.iterator] = real; }
  return n;
}
"""

CASES = {
    # The bug this was written for: nothing is iterated, so nothing is asked.
    'default derived constructor does not ask': PROBE + r"""
var n = probe(function(){ class B {} class D extends B {} new D(1, 2); });
console.log(n);
""",
    # Two levels of default constructor: each must stay silent, so still 0.
    'two levels of default constructor do not ask': PROBE + r"""
var n = probe(function(){
  class A {} class B extends A {} class C extends B {}
  new C(1, 2, 3);
});
console.log(n);
""",
    # A default constructor with no arguments: nothing to forward.
    'default constructor with no arguments does not ask': PROBE + r"""
var n = probe(function(){ class B {} class D extends B {} new D(); });
console.log(n);
""",
    # Arguments still arrive. Silencing the iterator must not lose the values.
    'the arguments still arrive': PROBE + r"""
var seen = null;
class Base { constructor() { seen = Array.prototype.slice.call(arguments); } }
class Derived extends Base {}
new Derived(1, 2, 3);
console.log(seen.length, seen[0], seen[1], seen[2]);
""",
    # A default constructor of a class with no superclass has nothing to spread.
    'default constructor without extends does not ask': PROBE + r"""
var n = probe(function(){ class C { } new C(1, 2); });
console.log(n);
""",
    # --- the traps: script-written spreads must keep asking -------------------
    # Same text as the synthesised constructor, but the script wrote it.
    'script-written super spread still asks': PROBE + r"""
var n = probe(function(){
  class Base { constructor() {} }
  class Sub extends Base { constructor(...args) { super(...args); } }
  new Sub(1, 2);
});
console.log(n);
""",
    # A plain spread in an argument list still asks.
    'ordinary spread still asks': PROBE + r"""
var n = probe(function(){ (function(){})(...[1, 2]); });
console.log(n);
""",
    # A non-array spread is still iterated by its own iterator. The array
    # prototype is not consulted, so the count is 0 in both engines -- this
    # case asserts that a plain spread still *works*, which the count alone
    # would not show.
    'a non-array spread is still iterated': PROBE + r"""
var iterable = {};
iterable[Symbol.iterator] = function(){
  var i = 0;
  return { next: function(){ return i < 2 ? {value: i++, done: false}
                                            : {value: undefined, done: true}; } };
};
var seen = [];
(function(){ seen = Array.prototype.slice.call(arguments); })(...iterable);
console.log(seen.length, seen[0], seen[1]);
""",
    # A synthesised spread forwards the arguments list, so it does not iterate
    # at all -- even an array whose own iterator was replaced must not be asked.
    # This is the case that a shape-based fix would still get wrong in a subtler
    # way: it must be silent because of where the spread came from.
    'default constructor does not ask even when the array has its own iterator': PROBE + r"""
var own = 0;
var arr = [1, 2];
arr[Symbol.iterator] = function(){ own++; return [][Symbol.iterator].call([1, 2]); };
var seen = null;
class Base { constructor(){ seen = Array.prototype.slice.call(arguments); } }
class Derived extends Base {}
new Derived(1, 2);
console.log(own, seen.length, seen[0]);
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
