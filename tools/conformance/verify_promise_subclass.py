#!/usr/bin/env python3
"""Runs the promise-subclass cases through the engine and through Node.js and
requires the two to print the same thing, line for line.

Why this exists
---------------
Phase 196 made every method that makes a promise ask the receiver's species
constructor for it, so a class that extends `Promise` gets its own kind of
promise back. The engine's own suites say what it *should* do in the cases
they were written for, but a suite written by the hand that wrote the code is
the failure mode `README.md` names -- "It was 100% before it was right".

So the expectation here comes from somewhere else: **Node.js**. Each case is a
program that prints what it saw; this runs it twice, once in `build/litebird`
and once in `node`, and requires the two outputs to be equal. Nothing is
hand-written, so nothing can be hand-written wrongly.

What it covers
--------------
  * `new P(...)` for `class P extends Promise {}`, with and without its own
    constructor and fields, and what `instanceof` says about it;
  * `then`/`catch`/`finally` answering a `P` (SpeciesConstructor), and a
    chain giving each step the value the step before returned;
  * a handler that answers a thenable, and one that answers a promise;
  * a rejection passing through a `then` with no handler for it, reaching a
    `catch` further down as the same reason;
  * `Promise.resolve`/`reject`/`all`/`allSettled`/`any`/`race`/`withResolvers`
    called on a subclass answering that subclass;
  * `Promise.resolve(p)` being `p` itself for a %Promise% promise;
  * `Symbol.species`, and a species that is not the constructor;
  * `Reflect.construct(Promise, [], NewTarget)`: reads `NewTarget.prototype`
    once, and complains about a non-callable executor before reading it.

Usage
-----
    python3 tools/conformance/verify_promise_subclass.py
    python3 tools/conformance/verify_promise_subclass.py --show 5 --filter then

Exit status: 0 when every case agreed with Node.js, 1 otherwise. A case whose
two runs both print nothing is not evidence of anything and counts as a
failure of the harness, not a pass.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

DEFAULT_BINARY = "build/litebird"
DEFAULT_NODE = "node"
DEFAULT_TIMEOUT = 20.0


@dataclass
class Case:
    name: str
    code: str


# Each case prints only what it observed -- no prose, no ordering the engines
# could disagree about -- so two matching outputs mean two matching answers.
CASES: list[Case] = [
    Case("ctor-runs-and-instanceof", """
class P extends Promise { constructor(fn) { super(fn); console.log("ctor ran"); } }
var p = new P(function (r) { r(1); });
console.log(p instanceof P, p instanceof Promise);
"""),
    Case("subclass-fields-and-methods", """
class P extends Promise { tag = "mine"; describe() { return this.tag + ":" + this.constructor.name; } }
var p = new P(function (r) { r(1); });
console.log(p.describe(), Object.getPrototypeOf(p) === P.prototype);
"""),
    Case("then-makes-the-species", """
class P extends Promise {}
var p = new P(function (r) { r(1); });
var q = p.then(function (v) { return v; });
console.log(q instanceof P, q.constructor === P);
"""),
    Case("catch-and-finally-make-the-species", """
class P extends Promise {}
var p = new P(function (r) { r(1); });
console.log(p.catch(function () {}) instanceof P, p.finally(function () {}) instanceof P);
"""),
    Case("chain-gives-each-step-the-last-value", """
var p = Promise.resolve(10);
p.then(function (v) { console.log("one", v); return v * 2; })
 .then(function (v) { console.log("two", v); return v + 5; })
 .then(function (v) { console.log("three", v); });
"""),
    Case("handler-answering-a-promise-is-adopted", """
var inner = Promise.resolve(7);
Promise.resolve(1).then(function () { return inner; }).then(function (v) { console.log("inner", v); });
Promise.resolve(3).then(function (v) { return Promise.resolve(v * 2); })
                 .then(function (v) { console.log("doubled", v); });
"""),
    Case("handler-answering-a-thenable-is-adopted", """
var thenable = { then: function (resolve) { resolve("from thenable"); } };
Promise.resolve(1).then(function () { return thenable; }).then(function (v) { console.log(v); });
"""),
    Case("rejection-passes-through-to-a-catch", """
Promise.reject(new Error("boom")).then(function () { console.log("wrong"); })
                                .catch(function (e) { console.log("caught", e.message); });
Promise.reject(2).then().then(function (v) { console.log("no", v); }, function (e) { console.log("second", e); });
"""),
    Case("a-throw-in-a-handler-rejects-the-chain", """
Promise.resolve(3).then(function (v) { throw new Error("thrown " + v); })
                  .catch(function (e) { console.log(e.message); });
"""),
    Case("constructor-half-keeps-the-reason", """
Promise.resolve(1).then(function () {}, function () {}).then(function (v) { console.log("fulfilled", v); });
"""),
    Case("statics-answer-the-subclass", """
class P extends Promise {}
console.log(P.resolve(1) instanceof P);
console.log(P.reject(1) instanceof P);
console.log(P.all([1]) instanceof P);
console.log(P.allSettled([1]) instanceof P);
console.log(P.any([1]) instanceof P);
console.log(P.race([1]) instanceof P);
"""),
    Case("withresolvers-answers-the-subclass", """
class P extends Promise {}
var w = P.withResolvers();
console.log(w.promise instanceof P, typeof w.resolve, typeof w.reject);
w.resolve("ok");
w.promise.then(function (v) { console.log(v); });
"""),
    Case("resolve-of-a-promise-is-that-promise", """
var p = Promise.resolve(1);
console.log(Promise.resolve(p) === p);
class P extends Promise {}
var q = new P(function (r) { r(1); });
console.log(P.resolve(q) === q, Promise.resolve(q) === q);
"""),
    Case("species-is-the-constructor-and-is-read", """
class P extends Promise {}
console.log(P[Symbol.species] === P, Promise[Symbol.species] === Promise);
class Odd extends Promise { static get [Symbol.species]() { return Promise; } }
var o = new Odd(function (r) { r(1); });
console.log(o.then(function () {}) instanceof Odd, o.then(function () {}) instanceof Promise);
"""),
    Case("all-settles-a-subclass-array", """
class P extends Promise {}
P.all([P.resolve(1), Promise.resolve(2)]).then(function (values) {
    console.log(values.join(), values instanceof Array);
});
"""),
    Case("any-and-race-settle", """
class P extends Promise {}
P.any([P.reject(1), P.resolve(2)]).then(function (v) { console.log("any", v); });
P.race([P.resolve(3), P.resolve(4)]).then(function (v) { console.log("race", v); });
"""),
    Case("reflect-construct-reads-prototype-once", """
var reads = 0;
function counting() {
    var nt = function () {}.bind(null);
    Object.defineProperty(nt, "prototype", { get: function () { reads++; return { tag: "G" }; } });
    return nt;
}
var before = reads;
var p = Reflect.construct(Promise, [function () {}], counting());
console.log(reads - before, p.tag, p instanceof Promise);
"""),
    Case("reflect-construct-refuses-executor-first", """
var reads = 0;
function counting() {
    var nt = function () {}.bind(null);
    Object.defineProperty(nt, "prototype", { get: function () { reads++; return {}; } });
    return nt;
}
var before = reads;
try { Reflect.construct(Promise, [], counting()); console.log("no throw"); }
catch (e) { console.log(e.name, reads - before); }
"""),
    Case("new-target-inside-a-subclass", """
class P extends Promise {
    constructor(fn) { super(fn); console.log("new.target is P:", new.target === P); }
}
new P(function (r) { r(1); });
var p = new P(function (r) { r(1); });
console.log(p.then(function () {}) instanceof P);
"""),
    Case("executor-name-length-and-not-a-constructor", """
var seen;
var p = new Promise(function (resolve, reject) { seen = resolve; });
console.log(seen.name, seen.length);
try { new seen(function () {}); console.log("constructed"); }
catch (e) { console.log(e.name); }
p.catch(function () {});
"""),
    Case("finally-runs-and-keeps-the-value", """
Promise.resolve(5).finally(function () { console.log("finally"); }).then(function (v) { console.log("value", v); });
Promise.reject(new Error("kept")).finally(function () { console.log("finally2"); })
    .catch(function (e) { console.log("reason", e.message); });
"""),
    Case("then-with-no-arguments-passes-both-outcomes", """
Promise.resolve(1).then().then(function (v) { console.log("value", v); });
Promise.reject(2).then().catch(function (e) { console.log("reason", e); });
"""),
]


def run(binary: str, code: str, timeout: float) -> tuple[int, str]:
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
        handle.write(code)
        path = handle.name
    try:
        if binary == DEFAULT_NODE or os.path.basename(binary) == "node":
            argv = [binary, path]
        else:
            argv = [binary, "--run-js", code]
        proc = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        out = proc.stdout
        # Unhandled rejections print on stderr in both engines and their text
        # differs; a case that means to observe one says so in its output.
        return proc.returncode, out
    except subprocess.TimeoutExpired:
        return -1, "<timeout>"
    finally:
        os.unlink(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--node", default=DEFAULT_NODE)
    parser.add_argument("--filter", default="", help="only cases whose name contains this")
    parser.add_argument("--show", type=int, default=20, help="how many failures to print in full")
    parser.add_argument("--jobs", type=int, default=min(8, (os.cpu_count() or 2)))
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    args = parser.parse_args()

    if not os.path.exists(args.binary) and "/" in args.binary:
        print(f"no such binary: {args.binary}", file=sys.stderr)
        return 1

    cases = [c for c in CASES if args.filter in c.name]
    if not cases:
        print(f"no case matches {args.filter!r}", file=sys.stderr)
        return 1

    def check(case: Case) -> tuple[Case, str, str]:
        _, ours = run(args.binary, case.code, args.timeout)
        _, theirs = run(args.node, case.code, args.timeout)
        return case, ours, theirs

    failures = []
    empty = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for case, ours, theirs in pool.map(check, cases):
            if not ours.strip() and not theirs.strip():
                empty.append(case)
            elif ours != theirs:
                failures.append((case, ours, theirs))

    print(f"cases: {len(cases)}   node: {args.node}")
    for case, ours, theirs in failures[: args.show]:
        print(f"\n--- {case.name}")
        print(f"  litebird: {ours!r}")
        print(f"  node:     {theirs!r}")
    if len(failures) > args.show:
        print(f"\n... and {len(failures) - args.show} more")
    for case in empty[: args.show]:
        print(f"\n--- {case.name}: both engines printed nothing (the harness, not a pass)")

    disagreed = len(failures) + len(empty)
    print(f"\nagreed: {len(cases) - disagreed} of {len(cases)} cases")
    return 0 if disagreed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
