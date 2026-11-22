#!/usr/bin/env python3
"""Differential check of eval context, declaration instantiation, and early errors vs Node.

Exercises the Phase 171 rules in both Litebird and Node.js:
  1. Direct eval inherits caller strictness, `super.x`, `super()`, `new.target`,
     class-field `arguments` prohibition, and private names (including through
     arrows). Indirect eval (`(0, eval)`) drops all of those.
  2. EvalDeclarationInstantiation checks collisions with intervening lexical
     bindings before mutating anything (atomic rollback), while allowing
     Annex B.3.5 `var` / `function` over a simple `catch (e)` parameter.
  3. Global `var` / `function` declarations live on the global object (non-
     configurable for scripts, configurable for sloppy eval; `delete` removes
     sloppy eval local bindings and global eval bindings) and reject collisions
     with global `let`/`const`/`class`.
  4. Function parameter-expression scopes separate parameter defaults from
     body `var` / lexical bindings, while body functions close over body lex.
  5. Iteration statements reset completion values to `undefined`; early syntax
     errors catch invalid `break`/`continue`/labels, top-level `return`, and
     top-level `new.target`; classes reject static `prototype` members; class
     fields isolate `var`, forbid `arguments`, and bind `new.target = undefined`.
  6. Loops/switches/labels containing multiple nested functions exercise
     parser context-stack reallocation without corrupting AST nodes.

Run: python3 tools/conformance/verify_eval_context.py
"""
import argparse
import subprocess
import sys

CASES = {
    "direct eval inherits caller strictness at parse and run time": r"""
function f() {
  'use strict';
  var out = [];
  for (var code of ['var a = 1; x_strict_leak = 2;', 'function dup(a, a) {}', 'var public = 1;', 'var x; delete x;', 'eval = 1;']) {
    try {
      eval(code);
      out.push('no-throw');
    } catch (e) {
      out.push(e.name);
    }
  }
  return out.join(',') + ':' + (typeof a) + ':' + (typeof x_strict_leak);
}
console.log(f());
""",
    "direct eval through arrow preserves super, new.target, and strict": r"""
class Base {
  m() { return 10; }
}
class Sub extends Base {
  constructor() {
    var arrow = () => eval('super(); [super.m(), new.target === Sub]');
    var res = arrow();
    this.res = res;
  }
}
console.log(JSON.stringify(new Sub().res));
""",
    "indirect eval isolates strictness and lexical bindings": r"""
function outer() {
  'use strict';
  var r1 = (0, eval)('let z = 42; with_ok = 7; z + with_ok');
  return [r1, typeof z, with_ok];
}
console.log(JSON.stringify(outer()), typeof z, globalThis.with_ok);
""",
    "indirect eval rejects new.target and super": r"""
function f() {
  var out = [];
  try { (0, eval)('new.target'); out.push('nt-ok'); } catch (e) { out.push(e.name); }
  class C {
    m() {
      try { (0, eval)('super.x'); out.push('sp-ok'); } catch (e) { out.push(e.name); }
    }
  }
  new C().m();
  return out.join(',');
}
console.log(f());
""",
    "eval declaration instantiation atomic rollback on collision": r"""
function f() {
  let conflict = 10;
  try {
    eval('var okBefore = 1; var conflict = 2;');
    return 'no-throw';
  } catch (e) {
    return e.name + ':' + (typeof okBefore) + ':' + conflict;
  }
}
console.log(f());
""",
    "annex B.3.5 catch allows direct eval var and function": r"""
var err = 'outer';
var out = [];
try {
  throw 'caught';
} catch (err) {
  eval('var err = 123;');
  out.push(err);
}
out.push(err);
try {
  throw 'caught2';
} catch (err2) {
  eval('function err2() { return 7; }');
  out.push(typeof err2);
}
out.push(typeof err2, err2());
console.log(JSON.stringify(out));
""",
    "global var and eval configurability and delete": r"""
var scriptVar = 1;
eval('var evalVar = 2;');
var dScript = Object.getOwnPropertyDescriptor(globalThis, 'scriptVar');
var dEval = Object.getOwnPropertyDescriptor(globalThis, 'evalVar');
var delScript = delete scriptVar;
var delEval = delete evalVar;
function localDel() {
  eval('var lv = 9;');
  return [lv, delete lv, typeof lv];
}
console.log(JSON.stringify([dScript.configurable, dEval.configurable, delScript, delEval, typeof evalVar, localDel()]));
""",
    "global lexical conflict rejects script var and eval var": r"""
let gLex = 1;
var out = [];
try { eval('var gLex = 2;'); out.push('no-throw'); } catch (e) { out.push(e.name); }
try { (0, eval)('function gLex() {}'); out.push('no-throw2'); } catch (e) { out.push(e.name); }
out.push(gLex);
console.log(JSON.stringify(out));
""",
    "function parameter expression scope splits from body var and lex": r"""
function f(a = 1, b = () => a) {
  var a = 99;
  return [a, b()];
}
function g(p = eval('var x = 7'), q = () => x) {
  var x = 2;
  return [x, q()];
}
function h() {
  let v = 1;
  {
    function inner() { return v; }
    var res = inner();
  }
  return res;
}
console.log(JSON.stringify([f(), g(), h()]));
""",
    "loop completion values reset across while, do-while, for, for-of": r"""
console.log(JSON.stringify([
  eval('1; while (false) {}'),
  eval('1; for (; false;) {}'),
  eval('1; for (var k of []) {}'),
  eval('1; for (var p in {}) {}'),
  eval('1; do { 2; } while (false)'),
  eval('1; do { break; } while (false)'),
]));
""",
    "early syntax errors for break, continue, label, return, new.target": r"""
var errs = [];
for (var code of [
  'break;',
  'continue;',
  'lbl: { continue lbl; }',
  'lbl: { lbl: 1; }',
  'break missing;',
  'return 1;',
  'new.target;',
]) {
  try {
    eval(code);
    errs.push('ok:' + code);
  } catch (e) {
    errs.push(e.name);
  }
}
console.log(JSON.stringify(errs));
""",
    "class static prototype rejection and field initializer rules": r"""
var res = [];
for (var src of [
  'class A { static prototype() {} }',
  'class B { static get prototype() { return 1; } }',
  'class C { static prototype = 1; }',
  "class A2 { static ['prototype']() {} }",
  "class B2 { static get ['prototype']() { return 1; } }",
  "class C2 { static ['prototype'] = 1; }",
  'class D { static x = eval("arguments"); }',
]) {
  try { eval(src); res.push('ok'); } catch (e) { res.push(e.name); }
}
class E {
  nt = new.target;
  f = (function() { var fv = 5; return fv; })();
}
var e = new E();
res.push(typeof e.nt, e.f, typeof fv);
console.log(JSON.stringify(res));
""",
    "nested functions inside loops, switch, and labels preserve AST": r"""
var out = [];
for (let i = 0; i < 3; i++) {
  (function() {
    var inner = (function(x) { return () => x * 2; })(i);
    out.push(inner());
  })();
}
lbl: for (var j = 0; j < 2; j++) {
  switch (j) {
    case 0:
      (function() { out.push('c0'); })();
      continue lbl;
    case 1:
      (function() { out.push('c1'); })();
      break lbl;
  }
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
