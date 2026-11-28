#!/usr/bin/env python3
"""Differential oracle for ES2025 Set.prototype methods against Node v22.

Phase 505, finding 505.
Run: python3 tools/conformance/verify_set_methods.py [--binary build/litebird] [--node node]
"""
import argparse
import subprocess
import sys

HELPERS = r"""
function fmtVal(v) {
  if (typeof v === 'symbol') return 'Sym(' + String(v.description) + ')';
  if (Object.is(v, -0)) return '-0';
  if (Number.isNaN(v)) return 'NaN';
  if (v && typeof v === 'object') {
    if (v instanceof Set) return 'Set[' + Array.from(v, fmtVal).join(',') + ']';
    return v.tag ? ('#' + v.tag) : 'obj';
  }
  return typeof v + ':' + String(v);
}
function fmtRes(fn) {
  try {
    return 'OK:' + fmtVal(fn());
  } catch (e) {
    return 'ERR:' + (e && e.constructor ? e.constructor.name : String(e));
  }
}
"""

CASES = {
    "method metadata and descriptors": r"""
const names = ['union', 'intersection', 'difference', 'symmetricDifference', 'isSubsetOf', 'isSupersetOf', 'isDisjointFrom'];
for (const m of names) {
  const fn = Set.prototype[m];
  const d = Object.getOwnPropertyDescriptor(Set.prototype, m);
  let ctorErr = 'none';
  try { new fn(); } catch (e) { ctorErr = e.constructor.name; }
  console.log(m, typeof fn, fn.name, fn.length, d.writable, d.enumerable, d.configurable, ctorErr);
}
""",
    "core set algebra across primitives objects and special values": r"""
const o1 = { tag: 'o1' }, o2 = { tag: 'o2' }, o3 = { tag: 'o3' };
const a = new Set([1, 'a', NaN, null, undefined, false, o1, o2, -0]);
const b = new Set([2, 'a', NaN, true, undefined, o2, o3, +0]);
console.log(fmtVal(a.union(b)));
console.log(fmtVal(a.intersection(b)));
console.log(fmtVal(a.difference(b)));
console.log(fmtVal(a.symmetricDifference(b)));
console.log(a.isSubsetOf(b), a.isSupersetOf(b), a.isDisjointFrom(b));
console.log(new Set([NaN, -0, o1]).isSubsetOf(new Set([NaN, +0, o1, o2])));
console.log(new Set([NaN, +0, o1, o2]).isSupersetOf(new Set([NaN, -0, o1])));
console.log(new Set([NaN, o1]).isDisjointFrom(new Set([NaN])));
console.log(new Set([1, 2]).isDisjointFrom(new Set([3, 4])));
console.log(fmtVal(new Set().union(new Set())));
console.log(new Set().isSubsetOf(new Set()), new Set().isSupersetOf(new Set()), new Set().isDisjointFrom(new Set()));
""",
    "self argument and Map argument and array with set-like props": r"""
const s = new Set([10, 20, 30]);
console.log(fmtVal(s.union(s)), s.union(s) === s);
console.log(fmtVal(s.intersection(s)), s.intersection(s) === s);
console.log(fmtVal(s.difference(s)));
console.log(fmtVal(s.symmetricDifference(s)));
console.log(s.isSubsetOf(s), s.isSupersetOf(s), s.isDisjointFrom(s));
const m = new Map([[20, 'v1'], [40, 'v2']]);
console.log(fmtVal(s.union(m)), fmtVal(s.intersection(m)), fmtVal(s.difference(m)), fmtVal(s.symmetricDifference(m)));
const arr = [99];
arr.size = 2;
arr.has = (v) => v === 20 || v === 50;
arr.keys = () => [20, 50].values();
console.log(fmtVal(s.union(arr)), fmtVal(s.intersection(arr)), fmtVal(s.difference(arr)));
""",
    "receiver validation precedes other inspection": r"""
const methods = ['union', 'intersection', 'difference', 'symmetricDifference', 'isSubsetOf', 'isSupersetOf', 'isDisjointFrom'];
for (const m of methods) {
  for (const r of [null, undefined, 1, 's', true, {}, [], new Map(), new WeakSet()]) {
    let touched = false;
    const probe = { get size() { touched = true; return 1; }, has() { return false; }, keys() { return [].values(); } };
    console.log(m, fmtRes(() => Set.prototype[m].call(r, probe)), 'touched=' + touched);
  }
}
""",
    "GetSetRecord validation and size coercions": r"""
const methods = ['union', 'intersection', 'difference', 'symmetricDifference', 'isSubsetOf', 'isSupersetOf', 'isDisjointFrom'];
for (const m of methods) {
  for (const arg of [undefined, null, 0, 'x', true, Symbol('s')]) {
    console.log(m, 'prim', fmtRes(() => new Set([1])[m](arg)));
  }
}
const sizes = [
  undefined, NaN, 'abc', Symbol('x'), [1, 2], {}, { valueOf() { return NaN; } },
  -1, -Infinity, '-Infinity', { valueOf() { return -1.5; } },
  -0.5, -0, +0, 0.5, null, false, true, '', ' 2 ', [], [2], Infinity, 'Infinity',
  { valueOf() { return 1; } }, { toString() { return '2'; } },
];
for (const sz of sizes) {
  const s = new Set([1, 2]);
  console.log('union', fmtRes(() => s.union({ size: sz, has: () => false, keys: () => [3].values() })));
  console.log('superset', fmtRes(() => s.isSupersetOf({ size: sz, has: () => true, keys: () => [1].values() })));
}
console.log('bad has', fmtRes(() => new Set([1]).union({ size: 1, has: 42, keys: () => [].values() })));
console.log('bad keys', fmtRes(() => new Set([1]).isSubsetOf({ size: 1, has: () => true, keys: {} })));
""",
    "GetSetRecord access order and size branching": r"""
function makeSetLike(sz, hasSet, vals, log) {
  return {
    get size() {
      log.push('get size');
      return { valueOf() { log.push('ToNumber(size)'); return sz; } };
    },
    get has() {
      log.push('get has');
      return function(v) { log.push('call has(' + v + ')'); return hasSet.includes(v); };
    },
    get keys() {
      log.push('get keys');
      return function() {
        log.push('call keys');
        let i = 0;
        return {
          get next() {
            log.push('get next');
            return function() {
              log.push('call next');
              return {
                get done() { log.push('get done'); return i >= vals.length; },
                get value() { log.push('get value'); return vals[i++]; },
              };
            };
          }
        };
      };
    }
  };
}
for (const m of ['union', 'intersection', 'difference', 'symmetricDifference', 'isSubsetOf', 'isSupersetOf', 'isDisjointFrom']) {
  for (const [label, aVals, sz, otherVals] of [
    ['smaller', [1, 2], 3, [2, 3, 4]],
    ['equal', [1, 2, 3], 3, [2, 3, 4]],
    ['larger', [1, 2, 3, 4], 2, [2, 4]],
  ]) {
    const log = [];
    const r = new Set(aVals)[m](makeSetLike(sz, otherVals, otherVals, log));
    console.log(m, label, fmtVal(r), log.join('|'));
  }
}
""",
    "negative zero canonicalization and non-boolean truthiness": r"""
for (const m of ['intersection', 'difference', 'isSubsetOf', 'isDisjointFrom']) {
  const seen = [];
  const s = new Set([-0]);
  const r = s[m]({ size: 1, has(v) { seen.push(fmtVal(v)); return v === 0 ? {} : ''; }, keys: () => [].values() });
  console.log(m, seen.join(','), fmtVal(r));
}
for (const m of ['union', 'intersection', 'difference', 'symmetricDifference', 'isSupersetOf', 'isDisjointFrom']) {
  const s = new Set([0, 1, 2]);
  const other = { size: 1, has: () => false, keys: () => [-0].values() };
  console.log(m, 'from-keys', fmtVal(s[m](other)));
}
for (const ret of [1, '0', {}, [], 0, '', NaN, null, undefined]) {
  const s = new Set([1]);
  console.log('truthy', fmtVal(ret),
    s.isSubsetOf({ size: 1, has: () => ret, keys: () => [].values() }),
    s.isDisjointFrom({ size: 1, has: () => ret, keys: () => [].values() }),
    fmtVal(s.intersection({ size: 1, has: () => ret, keys: () => [].values() })),
    fmtVal(s.difference({ size: 1, has: () => ret, keys: () => [].values() }))
  );
}
""",
    "live receiver mutations and cursor compaction": r"""
for (const m of ['union', 'intersection', 'difference', 'symmetricDifference']) {
  const s = new Set(['a', 'b', 'c', 'd', 'e']);
  const r = s[m]({
    size: 4,
    get has() { s.add('q'); return (v) => v === 'b' || v === 'q'; },
    keys() {
      let i = 0;
      const vals = ['x', 'b', 'c', 'c'];
      return {
        next() {
          if (i === 0) {
            s.delete('b');
            s.delete('c');
            s.add('b');
            s.add('d');
          }
          return { done: i >= vals.length, value: vals[i++] };
        }
      };
    }
  });
  console.log(m, fmtVal(r), fmtVal(s));
}
// Compaction stress (>32 deletions while iterating with <= branch)
{
  const big = new Set();
  for (let i = 0; i < 60; i++) big.add(i);
  const visited = [];
  const res = big.intersection({
    size: 100,
    has(v) {
      visited.push(v);
      if (v === 0) {
        for (let k = 1; k < 45; k++) big.delete(k);
        big.add(999);
      }
      return v % 2 === 0 || v === 999;
    },
    keys() { return [].values(); }
  });
  console.log('compact-intersection', visited.length, fmtVal(res));
}
{
  const big = new Set();
  for (let i = 0; i < 60; i++) big.add(i);
  const res = big.difference({
    size: 100,
    has(v) { return v < 55; },
    keys() { return [].values(); }
  });
  console.log('compact-difference', fmtVal(res));
}
{
  const s = new Set([1, 2]);
  const res = s.intersection({
    size: 5,
    has(v) {
      if (v === 1) { s.clear(); s.add(77); }
      return v === 77;
    },
    keys() { return [].values(); }
  });
  console.log('clear-during-has', fmtVal(res));
}
""",
    "IteratorClose on early return and error propagation": r"""
for (const [m, arr, sVals, expectClose] of [
  ['isSupersetOf', [1, 2, 3], [1, 2, 3, 4], false],
  ['isSupersetOf', [1, 9, 3], [1, 2, 3, 4], true],
  ['isDisjointFrom', [8, 9, 10], [1, 2, 3, 4], false],
  ['isDisjointFrom', [8, 2, 10], [1, 2, 3, 4], true],
]) {
  let nextCalls = 0, returnCalls = 0;
  const iter = {
    next() {
      const done = nextCalls >= arr.length;
      const value = arr[nextCalls++];
      return { done, value };
    },
    return() { returnCalls++; return {}; }
  };
  const r = new Set(sVals)[m]({ size: arr.length, has: () => true, keys: () => iter });
  console.log(m, expectClose, r, nextCalls, returnCalls);
}

// Generator finally triggers only when early exit happens
for (const [m, sVals] of [['isSupersetOf', [1, 2, 3]], ['isDisjointFrom', [1, 2, 3]]]) {
  let fin = 0;
  function* g() { try { yield 2; yield 99; yield 100; } finally { fin++; } }
  const r = new Set(sVals)[m]({ size: 3, has: () => true, keys: g });
  console.log('gen', m, r, fin);
}

// IteratorClose must NOT run on next errors, and return validation throws cleanly
for (const [label, makeIter] of [
  ['next-non-fn', (flag) => ({ next: 123, return() { flag.v++; return {}; } })],
  ['next-throws', (flag) => ({ next() { throw new URIError(); }, return() { flag.v++; return {}; } })],
  ['next-non-obj', (flag) => ({ next() { return 42; }, return() { flag.v++; return {}; } })],
  ['done-throws', (flag) => ({ next() { return { get done() { throw new EvalError(); } }; }, return() { flag.v++; return {}; } })],
  ['value-throws', (flag) => ({ next() { return { done: false, get value() { throw new RangeError(); } }; }, return() { flag.v++; return {}; } })],
  ['return-null-ok', () => ({ next: () => ({ done: false, value: 99 }), return: null })],
  ['return-non-fn', () => ({ next: () => ({ done: false, value: 99 }), return: {} })],
  ['return-non-obj', () => ({ next: () => ({ done: false, value: 99 }), return: () => 42 })],
  ['return-getter-throws', () => ({ next: () => ({ done: false, value: 99 }), get return() { throw new URIError(); } })],
  ['return-call-throws', () => ({ next: () => ({ done: false, value: 99 }), return() { throw new SyntaxError(); } })],
]) {
  const flag = { v: 0 };
  const res = fmtRes(() => new Set([1, 2]).isSupersetOf({ size: 1, has: () => true, keys: () => makeIter(flag) }));
  console.log(label, res, 'closed=' + flag.v);
}
""",
    "subclass isolation Species ignored Set.prototype.add ignored": r"""
let speciesCount = 0, addCalls = 0;
class MySet extends Set {
  static get [Symbol.species]() { speciesCount++; return MySet; }
  get size() { throw new Error('subclass size'); }
  has() { throw new Error('subclass has'); }
  keys() { throw new Error('subclass keys'); }
}
const s = new MySet([1, 2]);
const other = new Set([2, 3]);
const origAdd = Set.prototype.add;
Set.prototype.add = function() { addCalls++; return origAdd.apply(this, arguments); };
try {
  for (const m of ['union', 'intersection', 'difference', 'symmetricDifference']) {
    const out = s[m](other);
    console.log(m, out instanceof Set, out instanceof MySet, Object.getPrototypeOf(out) === Set.prototype, fmtVal(out));
  }
  console.log('counts', speciesCount, addCalls);
} finally {
  Set.prototype.add = origAdd;
}
""",
    "union and intersection result insertion order": r"""
console.log(fmtVal(new Set([3, 1]).union(new Set([2, 1, 4]))));
console.log(fmtVal(new Set([1, 3, 5]).intersection(new Set([3, 2, 1]))));
console.log(fmtVal(new Set([1, 3, 5, 7]).intersection(new Set([3, 2, 1]))));
console.log(fmtVal(new Set([6, 5, 3, 2]).difference(new Set([1, 2, 3, 4]))));
console.log(fmtVal(new Set([7, 6, 5, 3, 2]).difference(new Set([1, 2, 3, 4]))));
console.log(fmtVal(new Set([6, 5, 4, 3]).symmetricDifference(new Set([1, 2, 3, 4]))));
console.log(fmtVal(new Set([1, 2]).union({ size: 2, has: () => false, keys: () => [2, 3, 2, 4, 3].values() })));
console.log(fmtVal(new Set([1, 2, 3, 4]).intersection({ size: 2, has: () => false, keys: () => [3, 1, 3].values() })));
""",
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="build/litebird")
    parser.add_argument("--node", default="node")
    args = parser.parse_args()

    failures = 0
    for name, body in CASES.items():
        source = HELPERS + "\n" + body
        try:
            node = subprocess.run([args.node, "-e", source], capture_output=True, text=True, timeout=15)
            bird = subprocess.run([args.binary, "--run-js", source], capture_output=True, text=True, timeout=15)
        except (OSError, subprocess.TimeoutExpired) as exc:
            print(f"FAIL {name}: {exc}")
            failures += 1
            continue
        if node.returncode or bird.returncode or node.stdout != bird.stdout:
            print(f"FAIL {name}")
            print(f"  node ({node.returncode}): {node.stdout!r}\n  err: {node.stderr!r}")
            print(f"  litebird ({bird.returncode}): {bird.stdout!r}\n  err: {bird.stderr!r}")
            failures += 1
        else:
            print(f"PASS {name}")
    print(f"{len(CASES) - failures}/{len(CASES)} cases match Node v22")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
