#!/usr/bin/env python3
"""Builds tests/gc-conformance from scripts, with every #expect taken from Node."""
import os, subprocess, sys
from pathlib import Path

OUT = Path("tests/gc-conformance")
OUT.mkdir(exist_ok=True)

CASES = [
("closures.dat", "a closure keeps the scope it captured", """
function counter() {
  var n = 0;
  var label = "ticks";
  return function () { n += 1; return label + ":" + n; };
}
var c = counter();
for (var i = 0; i < 500; i++) c();
console.log(c());
var d = counter();
console.log(d(), d(), c());
"""),

("closures.dat", "a closure outlives the loop that made it", """
var fns = [];
for (let i = 0; i < 200; i++) {
  fns.push(function () { return i * 2; });
}
var sum = 0;
for (var j = 0; j < fns.length; j++) sum += fns[j]();
console.log(sum);
"""),

("closures.dat", "a closure survives the block it was made in", """
function build() {
  var out;
  {
    let secret = "kept";
    let big = [];
    for (let k = 0; k < 100; k++) big.push(k);
    out = function () { return secret + ":" + big.length; };
  }
  return out;
}
var f = build();
console.log(f(), f());
"""),

("closures.dat", "a function's own property cell outlives the call that set it", """
function tag(fn, n) {
  fn.count = n;
  return fn;
}
function make() {
  var self = function () { return typeof self.count; };
  return tag(self, 7);
}
var a = make();
var b = make();
console.log(a(), b(), a.count, b.count);
"""),

("closures.dat", "a named function expression can call itself", """
var fact = function inner(n) { return n <= 1 ? 1 : n * inner(n - 1); };
console.log(fact(10));
var fib = function f(n) { return n < 2 ? n : f(n - 1) + f(n - 2); };
console.log(fib(18));
"""),

("closures.dat", "two hundred closures each keep their own value", """
function make(i) { return function () { return i; }; }
var fns = [];
for (var i = 0; i < 200; i++) fns.push(make(i));
var total = 0;
for (var j = 0; j < fns.length; j++) total += fns[j]();
console.log(total);
"""),

("iterators-generators.dat", "a suspended generator keeps its locals", """
function* g() {
  var keep = [];
  for (var i = 0; i < 50; i++) keep.push({ i: i });
  var first = keep[0].i;
  yield "ready";
  yield keep.length + ":" + first;
  yield keep[49].i;
}
var it = g();
var seen = [];
seen.push(it.next().value);
seen.push(it.next().value);
seen.push(it.next().value);
console.log(seen.join("|"));
"""),

("iterators-generators.dat", "a generator is stepped through a for-of", """
function* squares(n) {
  for (var i = 1; i <= n; i++) {
    var box = { v: i * i };
    yield box.v;
  }
}
var out = [];
for (var v of squares(6)) out.push(v);
console.log(out.join(","));
"""),

("iterators-generators.dat", "an abandoned generator is collected and its siblings are not", """
function* count(name) {
  var held = [];
  for (var i = 0; i < 40; i++) held.push({ n: name + i });
  yield name + ":first";
  yield name + ":" + held.length;
}
var a = count("a");
var b = count("b");
console.log(a.next().value);
console.log(b.next().value);
console.log(b.next().value);
a = null;
console.log(b.next().value);
"""),

("iterators-generators.dat", "yield-star keeps the delegate alive", """
function* inner() { yield 1; yield 2; yield 3; }
function* outer() {
  var tag = "outer";
  yield tag;
  yield* inner();
  yield tag + ":done";
}
var seen = [];
for (var v of outer()) seen.push(String(v));
console.log(seen.join(","));
"""),

("iterators-generators.dat", "a custom iterator protocol survives collections", """
var obj = {};
obj[Symbol.iterator] = function () {
  var i = 0;
  var data = [10, 20, 30, 40];
  return {
    next: function () {
      if (i < data.length) { return { value: data[i++], done: false }; }
      return { value: undefined, done: true };
    }
  };
};
var total = 0;
for (var v of obj) total += v;
console.log(total);
"""),

("maps-and-collections.dat", "Map entries survive a collection", """
var m = new Map();
for (var i = 0; i < 300; i++) m.set("k" + i, { i: i });
var total = 0;
var count = 0;
m.forEach(function (v) { total += v.i; count += 1; });
console.log(count, total, m.size);
"""),

("maps-and-collections.dat", "two live Map iterators across insertion and clear", """
var m = new Map();
for (var i = 0; i < 50; i++) m.set(i, i * 3);
var it1 = m.entries();
var it2 = m.values();
var a = it1.next().value;
var b = it2.next().value;
for (var j = 50; j < 80; j++) m.set(j, j * 3);
var rest = 0;
var step = it2.next();
while (!step.done) { rest += step.value; step = it2.next(); }
m.clear();
console.log(a[0], a[1], b, rest, m.size);
"""),

("maps-and-collections.dat", "objects stay alive while a Map keys them", """
var m = new Map();
var keys = [];
for (var i = 0; i < 100; i++) {
  var k = { id: i };
  keys.push(k);
  m.set(k, "v" + i);
}
var hits = 0;
for (var j = 0; j < keys.length; j++) if (m.get(keys[j]) === "v" + j) hits += 1;
console.log(hits, m.size);
"""),

("maps-and-collections.dat", "a Set deduplicates objects by identity", """
var s = new Set();
var a = { name: "a" };
var b = { name: "a" };
for (var i = 0; i < 100; i++) { s.add(a); s.add(b); }
s.add({ name: "a" });
console.log(s.size, s.has(a), s.has(b), s.has({ name: "a" }));
"""),

("maps-and-collections.dat", "an array with holes reads through the prototype", """
var a = [1, 2, 3];
a.length = 6;
var seen = [];
for (var i = 0; i < a.length; i++) seen.push(String(a[i]));
console.log(seen.join(","), a.length);
var holes = 0;
for (var j = 0; j < a.length; j++) if (j in a) holes += 1;
console.log(holes);
"""),

("scopes-and-exceptions.dat", "a caught exception keeps its message alive", """
var kept;
try {
  throw new Error("boom");
} catch (e) {
  kept = e;
}
console.log(kept.message, kept instanceof Error);
var again = 0;
for (var i = 0; i < 100; i++) {
  try { throw new Error("tick" + i); } catch (e) { again += e.message.length > 0 ? 1 : 0; }
}
console.log(again);
"""),

("scopes-and-exceptions.dat", "a finally block runs with its bindings alive", """
function f() {
  var log = [];
  try {
    log.push("try");
    throw new Error("x");
  } catch (e) {
    log.push("catch:" + e.message);
    return log.join(",");
  } finally {
    log.push("finally");
  }
}
console.log(f());
"""),

("scopes-and-exceptions.dat", "a catch scope is per iteration", """
var out = [];
for (var i = 0; i < 50; i++) {
  try { throw "e" + i; } catch (e) { out.push(e); }
}
console.log(out.length, out[0], out[49]);
"""),

("scopes-and-exceptions.dat", "an arguments object outlives the call", """
function grab() { return arguments; }
var args = grab(1, 2, 3);
console.log(args.length, args[0], args[2]);
function tail(x) { var a = arguments; return function () { return a.length + ":" + x; }; }
var t = tail("t");
console.log(t());
"""),

("scopes-and-exceptions.dat", "a direct eval sees the caller's bindings afterwards", """
function scope() {
  var local = "L";
  var made = eval("(function () { return local + '!'; })");
  return made;
}
console.log(scope()());
"""),

("scopes-and-exceptions.dat", "a private field survives collections", """
class Point {
  #x = 0;
  #y = 0;
  constructor(x, y) { this.#x = x; this.#y = y; }
  sum() { return this.#x + this.#y; }
  bump() { this.#x += 1; return this.sum(); }
}
var p = new Point(3, 4);
console.log(p.sum());
for (var i = 0; i < 100; i++) p.bump();
console.log(p.sum());
"""),

("builtins-that-call-back.dat", "sort's comparator allocates while the array is sorted", """
var items = [];
for (var i = 0; i < 120; i++) items.push({ k: (i * 37) % 120, pad: "x" + i });
items.sort(function (a, b) {
  var tag = { a: a.k, b: b.k };
  return tag.a - tag.b;
});
var ordered = true;
for (var j = 1; j < items.length; j++) if (items[j - 1].k > items[j].k) ordered = false;
console.log(ordered, items.length, items[0].k, items[items.length - 1].k);
"""),

("builtins-that-call-back.dat", "forEach's callback allocates while the array is iterated", """
var src = [];
for (var i = 0; i < 150; i++) src.push(i);
var total = 0;
src.forEach(function (v) {
  var box = { v: v, junk: new Array(4).join(".") };
  total += box.v;
});
console.log(total, src.length);
"""),

("builtins-that-call-back.dat", "map builds its result while the callback allocates", """
var src = [];
for (var i = 0; i < 100; i++) src.push(i);
var out = src.map(function (v) { return { doubled: v * 2 }; });
var sum = 0;
for (var j = 0; j < out.length; j++) sum += out[j].doubled;
console.log(out.length, sum);
"""),

("builtins-that-call-back.dat", "a getter allocates during a property read", """
var log = [];
var obj = {
  get value() { log.push("read"); return { n: log.length }; }
};
var a = obj.value;
var b = obj.value;
console.log(a.n, b.n, log.length);
"""),

("builtins-that-call-back.dat", "a filter's predicate allocates while the array is filtered", """
var src = [];
for (var i = 0; i < 200; i++) src.push(i);
var evens = src.filter(function (v) { return { ok: v % 2 === 0 }.ok; });
console.log(evens.length, evens[0], evens[evens.length - 1]);
"""),

("builtins-that-call-back.dat", "JSON round-trips a deep structure", """
var data = { rows: [] };
for (var i = 0; i < 120; i++) data.rows.push({ i: i, label: "r" + i, tags: ["a", "b"] });
var text = JSON.stringify(data);
var back = JSON.parse(text);
var sum = 0;
for (var j = 0; j < back.rows.length; j++) sum += back.rows[j].i;
console.log(text.length > 0, back.rows.length, sum, back.rows[5].tags.join(""));
"""),
]


def main() -> int:
    made = 0
    for filename, name, code in CASES:
        path = OUT / filename
        existing = path.read_text(encoding="utf-8") if path.exists() else ""
        if f"#name {name}" in existing.split("\n"):
            print(f"skip (exists): {name}")
            continue
        script = OUT / ".case.js"
        script.write_text(code.strip("\n"), encoding="utf-8")
        env = dict(os.environ, TZ="UTC")
        done = subprocess.run(["node", str(script)], capture_output=True, text=True, env=env)
        script.unlink()
        if done.returncode != 0 or not done.stdout:
            print(f"NODE FAILED {name}: {done.stderr.strip()[:300]}", file=sys.stderr)
            return 1
        expected = done.stdout.rstrip("\n")
        block = f"#name {name}\n#code\n{code.strip(chr(10))}\n#expect\n{expected}\n"
        with path.open("a", encoding="utf-8") as fh:
            if existing:
                fh.write("\n")
            fh.write(block)
        made += 1
        print(f"added: {filename} :: {name}")
    print(f"{made} cases added")
    return 0


if __name__ == "__main__":
    sys.exit(main())
