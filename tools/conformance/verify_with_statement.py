#!/usr/bin/env python3
"""Differential check of ECMA-262 `with` statements (§14.11 / §9.1.1.2) vs Node.js.

Exercises sloppy-mode object environment records, `Symbol.unscopables`,
`WithBaseObject`, `typeof`/`delete`/`var`/destructuring/compound assignment,
strict-mode early SyntaxError rejection, nested strict functions, closures,
completion values, direct `eval`, and TodoMVC's runtime-compiled template in
both Litebird and Node.js.

Run: python3 tools/conformance/verify_with_statement.py
"""
import argparse
import subprocess
import sys

CASES = {
    "basic lookup, mutation, nesting, and outer fallback": r"""
var x = 1, y = 2, z = 3;
var a = { x: 10, y: 20 };
var b = { y: 200 };
var out = [];
with (a) {
  out.push(x, y, z);
  with (b) {
    out.push(x, y, z);
    y = 201;
    x = 11;
    z = 31;
  }
  out.push(x, y, z);
}
out.push(x, y, z, a.x, a.y, b.y);
console.log(JSON.stringify(out));
""",

    "prototype chain lookup and own-property shadow on write": r"""
var proto = { p: 1, q: 2 };
var obj = Object.create(proto);
obj.q = 20;
var p = 99;
var out = [];
with (obj) {
  out.push(p, q);
  p = 5;
  out.push(p, proto.p, Object.prototype.hasOwnProperty.call(obj, 'p'));
}
out.push(p);
console.log(JSON.stringify(out));
""",

    "ToObject boxing of primitives and TypeError on null/undefined": r"""
var out = [];
with ('hello') { out.push(length, toUpperCase()); }
with (3.5) { out.push(toFixed(2)); }
with (true) { out.push(valueOf()); }
for (var v of [null, undefined]) {
  try {
    with (v) { out.push('unreachable'); }
  } catch (e) {
    out.push(e.name);
  }
}
console.log(JSON.stringify(out));
""",

    "Array.prototype[Symbol.unscopables] lets outer bindings win for all 16 names": r"""
var at = 'outer-at';
var copyWithin = 'outer-copyWithin';
var entries = 'outer-entries';
var fill = 'outer-fill';
var find = 'outer-find';
var findIndex = 'outer-findIndex';
var findLast = 'outer-findLast';
var findLastIndex = 'outer-findLastIndex';
var flat = 'outer-flat';
var flatMap = 'outer-flatMap';
var includes = 'outer-includes';
var keys = 'outer-keys';
var toReversed = 'outer-toReversed';
var toSorted = 'outer-toSorted';
var toSpliced = 'outer-toSpliced';
var values = 'outer-values';
var arr = [10, 20, 30];
var out = [];
with (arr) {
  out.push(at, copyWithin, entries, fill, find, findIndex, findLast, findLastIndex,
           flat, flatMap, includes, keys, toReversed, toSorted, toSpliced, values);
  // Scopable array members still resolve on arr with `this === arr`.
  push(40);
  out.push(length, join('-'));
}
console.log(JSON.stringify(out));
""",

    "custom Symbol.unscopables truthy/falsy, primitive, inherited, and getter order": r"""
var log = [];
var a = 'outerA', b = 'outerB', c = 'outerC', d = 'outerD';
var o = {
  a: 'inA',
  b: 'inB',
  c: 'inC',
  get [Symbol.unscopables]() {
    log.push('get-unscopables');
    return { a: 1, b: 0, c: false };
  }
};
var out = [];
with (o) {
  out.push(a, b, c, d);
}
// `d` is absent from `o`, so `HasProperty` is false and `@@unscopables` MUST NOT be read for `d`.
console.log(JSON.stringify([out, log.length]));

// Primitive @@unscopables values are ignored (§9.1.1.2.1 step 6).
var primOut = [];
for (var u of [null, undefined, true, 42, 'x']) {
  var x = 'outer';
  var obj = { x: 'inner', [Symbol.unscopables]: u };
  with (obj) { primOut.push(x); }
}
console.log(JSON.stringify(primOut));

// Throwing getter on @@unscopables propagates.
var threw = 'no';
var bad = { x: 1, get [Symbol.unscopables]() { throw new RangeError('boom'); } };
try {
  with (bad) { void x; }
} catch (e) {
  threw = e.name + ':' + e.message;
}
console.log(threw);
""",

    "WithBaseObject passes binding object as this for calls and tagged templates": r"""
var outer = function() { 'use strict'; return this === undefined; };
var obj = {
  tag: 'OBJ',
  m() { 'use strict'; return this === obj ? this.tag : 'wrong'; },
  t(strings, v) { 'use strict'; return (this === obj) + ':' + strings[0] + v; },
  'this': 999
};
var out = [];
with (obj) {
  out.push(m());
  out.push((m)());
  out.push((0, m).call(undefined) === 'wrong');
  out.push(outer());
  out.push(t`val=${42}`);
  // `this` keyword is never resolved through the with object!
  out.push(this !== 999);
}
console.log(JSON.stringify(out));
""",

    "typeof inside with reads bound properties, respects unscopables, and never throws on missing": r"""
var log = [];
var u = 'outerU';
var o = {
  get a() { log.push('a'); return 123; },
  get u() { log.push('u'); return 456; },
  [Symbol.unscopables]: { u: true }
};
var out = [];
with (o) {
  out.push(typeof a, typeof u, typeof definitelyNotDeclaredAnywhereXYZ);
}
console.log(JSON.stringify([out, log]));
""",

    "delete inside with deletes configurable properties and skips unscopables": r"""
var x = 'outerX';
var u = 'outerU';
var o = { x: 'innerX', u: 'innerU', [Symbol.unscopables]: { u: true } };
Object.defineProperty(o, 'nc', { value: 1, configurable: false });
var out = [];
with (o) {
  out.push(delete x, 'x' in o, x);
  out.push(delete nc, o.nc);
  out.push(delete u, o.u);
  out.push(delete neverExistedXYZ);
}
console.log(JSON.stringify(out));
""",

    "var hoisting and destructuring inside with": r"""
function run() {
  var o = { x: 10, a: 1 };
  with (o) {
    var x = 20;
    var y = 30;
    var [a, b] = [100, 200];
  }
  return [o.x, o.y, typeof x, x, y, o.a, o.b, typeof a, a, b];
}
console.log(JSON.stringify(run()));
""",

    "compound assignment, logical assignment, update operators, and getters/setters": r"""
var log = [];
var store = { n: 5, f: 0, t: 1, z: null };
var o = {
  get n() { log.push('get:n'); return store.n; },
  set n(v) { log.push('set:n=' + v); store.n = v; },
  get f() { log.push('get:f'); return store.f; },
  set f(v) { log.push('set:f=' + v); store.f = v; },
  get t() { log.push('get:t'); return store.t; },
  set t(v) { log.push('set:t=' + v); store.t = v; },
  get z() { log.push('get:z'); return store.z; },
  set z(v) { log.push('set:z=' + v); store.z = v; },
};
var out = [];
with (o) {
  n += 3;
  out.push(n++);
  out.push(++n);
  f ||= 7;
  t &&= 9;
  z ??= 11;
  f &&= 99;
}
console.log(JSON.stringify([out, store, log]));
""",

    "destructuring assignment and for-in/for-of targets inside with": r"""
var o = { a: 0, b: 0, k: '' };
var c = 0;
var seen = [];
with (o) {
  [a, c] = [10, 20];
  ({ x: b } = { x: 30 });
  for (k of ['p', 'q']) { seen.push(o.k); }
}
console.log(JSON.stringify([o.a, o.b, o.k, c, seen]));
""",

    "undefined shadowing inside with": r"""
var out = [];
with ({ undefined: 42 }) {
  out.push(undefined, typeof undefined);
}
out.push(undefined);
console.log(JSON.stringify(out));
""",

    "strict mode rejects with as an early SyntaxError and single-statement declarations are rejected": r"""
var rejected = [
  '"use strict"; with ({}) {}',
  'function f() { "use strict"; with ({}) {} }',
  'class C { m() { with ({}) {} } }',
  'class C { static { with ({}) {} } }',
  'with ({}) let x = 1;',
  'with ({}) const x = 1;',
  'with ({}) class C {}',
  'with ({}) function f() {}',
  'with ({}) async function f() {}',
  'with ({}) let[0] = 1;',
  'with ({}) label: function f() {}',
  'with ({}) label: let x = 1;',
];
var out = [];
for (var code of rejected) {
  try {
    new Function(code);
    out.push('ALLOWED:' + code);
  } catch (e) {
    out.push(e.name);
  }
}
// Direct eval in strict mode also rejects `with`.
try {
  (function() { 'use strict'; eval('with ({}) {}'); })();
  out.push('ALLOWED:strict-eval');
} catch (e) {
  out.push(e.name);
}
// Allowed sloppy forms parse and run.
var allowed = [
  'with ({}) ;',
  'with ({}) var x = 1;',
  'with ({ let: 0 }) let = 1;',
  'with ({}) { function f() {} }',
  'with ({}) label: { break label; }',
];
for (var ok of allowed) {
  try {
    new Function(ok)();
    out.push('OK');
  } catch (e) {
    out.push('ERR:' + e.name);
  }
}
console.log(JSON.stringify(out));
""",

    "strict function nested inside with captures with scope and enforces strict Set": r"""
var o = { x: 1 };
Object.defineProperty(o, 'ro', { value: 5, writable: false, configurable: true });
var readX, writeX, writeRo, delCompound;
with (o) {
  readX = function() { 'use strict'; return x; };
  writeX = function(v) { 'use strict'; x = v; };
  writeRo = function() { 'use strict'; ro = 6; };
  delCompound = function() { 'use strict'; x += (delete o.x, 10); };
}
var out = [readX()];
writeX(42);
out.push(o.x, readX());
try { writeRo(); out.push('no-throw'); } catch (e) { out.push(e.name); }
try { delCompound(); out.push('no-throw'); } catch (e) { out.push(e.name); }
console.log(JSON.stringify(out));
""",

    "closures outliving with, block lexical shadowing, and abrupt control flow": r"""
function makeCounter(obj) {
  with (obj) {
    return function() { return ++count; };
  }
}
var box = { count: 10 };
var inc = makeCounter(box);
var out = [inc(), inc(), box.count];

// Block `let` inside `with` shadows the `with` object.
var o = { v: 'from-with' };
with (o) {
  out.push(v);
  {
    let v = 'from-let';
    out.push(v);
  }
  out.push(v);
}

// Loop break/continue/return out of `with`.
var sum = 0;
for (var i = 0; i < 4; i++) {
  with ({ step: i }) {
    if (step === 1) continue;
    if (step === 3) break;
    sum += step;
  }
}
out.push(sum);
console.log(JSON.stringify(out));
""",

    "completion values via eval and direct eval inside with": r"""
var out = [
  eval('1; with ({}) {}'),
  eval('1; with ({ x: 2 }) { x; }'),
  eval('1; with ({}) ;'),
  eval('1; with ({}) { var a = 3; }'),
  eval('L: with ({ x: 5 }) { x; break L; }'),
];
var o = { x: 77 };
with (o) {
  out.push(eval('x'));
}
var o2 = { x: 88, eval: eval };
with (o2) {
  out.push(eval('x'));
}
(function() {
  var t = { x: 1 };
  with (t) {
    eval('var x = 2; var y = 3;');
  }
  out.push(t.x, t.y, typeof x, y);
})();
console.log(JSON.stringify(out));
""",

    "TodoMVC / Underscore runtime-compiled template pattern": r"""
var template = new Function(
  'obj',
  "var __t,__p='',__j=Array.prototype.join,print=function(){__p+=__j.call(arguments,'');};" +
  "with(obj||{}){__p+='<li class=\"'+(completed?'completed':'')+'\">'+title+' ('+items.length+')</li>';}" +
  "return __p;"
);
console.log(template({ completed: true, title: 'Buy mangoes', items: [1, 2, 3] }));
console.log(template({ completed: false, title: 'Ship with', items: [] }));
""",

    "generator yield and async await preserve with scope across suspension": r"""
function* gen(o) {
  with (o) {
    yield a;
    a += 10;
    yield a;
  }
}
var obj = { a: 5 };
var it = gen(obj);
var g1 = it.next().value;
obj.a = 7;
var g2 = it.next().value;
var g3 = it.next().done;

async function runAsync(o) {
  with (o) {
    var first = await Promise.resolve(val);
    val = first + 100;
    return val;
  }
}
var aobj = { val: 23 };
runAsync(aobj).then(function(res) {
  console.log(JSON.stringify([g1, g2, g3, obj.a, res, aobj.val]));
});
""",

    "delete on inherited property, try/catch/finally unwind, and arrow/bound this": r"""
var proto = { p: 1 };
var child = Object.create(proto);
var out = [];
with (child) {
  out.push(delete p, proto.p, p);
}

var caught = 'none';
var fin = 'no';
var y = 'outerY';
try {
  with ({ y: 'innerY' }) {
    try {
      throw new Error(y);
    } finally {
      fin = y;
    }
  }
} catch (e) {
  caught = e.message + ':' + y;
}
out.push(caught, fin);

var boundTarget = { id: 'BOUND' };
var holder = {
  id: 'HOLDER',
  arrow: () => this,
  bound: (function() { return this.id; }).bind(boundTarget),
  plain() { return this.id; },
};
with (holder) {
  out.push(bound(), plain(), arrow() !== holder);
}
console.log(JSON.stringify(out));
""",
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="build/litebird")
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    failures = 0
    for name, source in CASES.items():
        try:
            node = subprocess.run([args.node, "-e", source], capture_output=True, text=True, timeout=15)
            bird = subprocess.run([args.binary, "--run-js", source], capture_output=True, text=True, timeout=15)
        except (OSError, subprocess.TimeoutExpired) as error:
            print(f"FAIL {name}: {error}")
            failures += 1
            continue
        if node.returncode or bird.returncode or node.stdout != bird.stdout:
            print(
                f"FAIL {name}\n"
                f"  node ({node.returncode}): {node.stdout.strip()!r} {node.stderr.strip()[:300]!r}\n"
                f"  litebird ({bird.returncode}): {bird.stdout.strip()!r} {bird.stderr.strip()[:300]!r}"
            )
            failures += 1
        else:
            print(f"PASS {name}: {node.stdout.strip()}")
    print(f"{len(CASES) - failures}/{len(CASES)} match Node")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
