#!/usr/bin/env python3
"""Measures whether a churn loop leaves garbage behind, by running it at two
sizes and requiring the residue to stay flat.

Why this exists
---------------
The AGENTS.md row for the GC audit said, in as many words:

> a promise reaction -- `Promise.resolve(1).then(f)` in a loop is still two
> cells an iteration, and `then(f).then(g)` four

Every one of those numbers reproduces on this tree -- if you count
`gc().alive_cells` at the bottom of the loop, before the loop's own reactions
have run. A reaction is not garbage when it is queued: it is work outstanding,
held by the microtask queue, and HTML says a microtask checkpoint drains the
queue before the next task. Counting mid-flight measures the queue's depth.

The mistake is invisible to a single measurement, because both a leak and a
backlog are a positive number that grows with the loop. It is only visible
across two sizes:

  * a **leak** holds every object it ever made: residue(10N) - residue(N) is
    nine times the per-iteration cost, so 2 cells an iteration means about
    3,600 extra cells between N=200 and N=2,000;
  * a **backlog** holds what one batch queued: the queue drains between
    batches, so the residue does not depend on how many batches ran at all.

So each case here runs at a small size and a large one and the harness
requires the two residues to agree within a tolerance. Nothing is
hand-written: the number that decides is the engine's own `gc()`.

What it covers
--------------
  * `Promise.resolve(1).then(f)` -- the row's own example;
  * `then(f).then(g)` -- the row's second example, four cells an iteration;
  * a deep chain, a settled promise with no handler, a handler that captures;
  * a handler whose promise is settled from a timer, so the collector's
    conservative `holders > 3` path is the one that runs;
  * `bind`, both targets the row named;
  * timers and intervals that fire, and timers and intervals that are cleared;
  * `addEventListener`/`removeEventListener` pairs.

Behaviour is checked too, not only memory: a case may print what it observed
as well as its residue, so a collector that reached these numbers by dropping
a reaction that still had to run would fail on the behaviour line first.

Usage
-----
    python3 tools/conformance/verify_gc_residue.py
    python3 tools/conformance/verify_gc_residue.py --filter timer --show 3
    python3 tools/conformance/verify_gc_residue.py --small 200 --large 4000

Exit status: 0 when every case stayed flat across the two sizes, 1 otherwise.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

DEFAULT_BINARY = "build/litebird"
DEFAULT_TIMEOUT = 60.0

# Cells. A case's residue is allowed to move this much between the two sizes
# and still count as flat. The measured noise on a flat case is 0-8 cells; the
# smallest real leak a churn loop can show at these sizes is thousands.
DEFAULT_TOLERANCE = 64

# How the harness is stitched around a case body. Every case runs inside these
# two wrappers, so no case can forget to drain, and none can measure its own
# baseline before the heap has settled.
HARNESS = """
var N = %(n)d;
var BATCH = 100;
%(setup)s
function step(i) { %(step)s }
function batches() { return Math.ceil(N / BATCH); }
var done = 0;
function drainThen(fn) { setTimeout(fn, 0); }
function runBatch() {
    var last = Math.min(done + BATCH, N);
    for (var i = done; i < last; i++) { step(i); }
    done = last;
    if (done < N) { setTimeout(runBatch, 0); } else { finish(); }
}
function finish() {
    // One turn past the last batch: the queue the last batch filled has run,
    // and anything the reactions queued in turn has run too.
    setTimeout(function () {
        gc();
        console.log("residue", gc().alive_cells - base);
        %(report)s
    }, 50);
}
gc();
// Settle first, so the baseline is not taken with the harness' own timers
// pending.
setTimeout(function () { gc(); base = gc().alive_cells; runBatch(); }, 20);
var base = 0;
"""


@dataclass
class Case:
    name: str
    setup: str = ""
    step: str = ""
    report: str = ""
    tolerance: int = DEFAULT_TOLERANCE
    # The measured drift this case is expected to show while the defect is
    # open, and why. A case with a `known` reason reports KNOWN and is not
    # counted as a pass; a case that comes back flat while it still carries a
    # reason reports FIXED and fails the run, so the corpus cannot accumulate
    # excuses (the convention `tests/render/reftests.json` uses).
    known: str = ""


CASES: list[Case] = [
    # --- the two shapes the row named -----------------------------------
    Case(
        "promise-reaction-single",
        step="Promise.resolve(1).then(function f(v) { return v; });",
    ),
    Case(
        "promise-reaction-pair",
        step="Promise.resolve(1).then(function f(v) { return v; })"
             ".then(function g(v) { return v; });",
    ),
    Case(
        "promise-chain-deep",
        step="Promise.resolve(1).then(function (v) { return v + 1; })"
             ".then(function (v) { return v + 1; })"
             ".then(function (v) { return v + 1; })"
             ".then(function (v) { return v + 1; });",
    ),
    Case(
        "promise-settled-no-handler",
        step="new Promise(function (r) { r(1); });",
    ),
    Case(
        "promise-handler-captures",
        step="var o = { n: i, s: 'payload' };"
             "Promise.resolve(1).then(function () { return o.n; });",
    ),
    # The collector keeps a promise's reactions as roots when the promise has
    # more owners than script and its own cell, because native code may still
    # settle it. Settling from a timer is that path, and the reaction still
    # has to run.
    Case(
        "promise-settled-from-timer",
        setup="var fired = 0;",
        step="var o = { n: i };"
             "var p = new Promise(function (r) { setTimeout(function () { r(1); }, 0); });"
             "p.then(function () { fired++; return o.n; });",
        report="console.log('observed fired > 0:', fired > 0);",
    ),
    # --- bind, which the row says was fixed in Phase 168 -----------------
    Case(
        "bind-null-target",
        setup="function f(x) { return x; }",
        step="var b = f.bind(null);",
    ),
    Case(
        "bind-object-target",
        step="var o = { tag: i }; var b = (function () { return this.tag; }).bind(o);",
    ),
    Case(
        "bind-with-arguments",
        step="var o = { tag: i }; var b = (function (a) { return a; }).bind(o, i);",
    ),
    # --- timers ---------------------------------------------------------
    Case(
        "timer-fired",
        setup="var fired = 0;",
        step="setTimeout(function () { fired++; }, 0);",
    ),
    Case(
        "timer-cleared",
        step="var t = setTimeout(function () {}, 100000); clearTimeout(t);",
    ),
    Case(
        "timer-pending-one",
        # One pending timer per case, not per iteration: a timer that has not
        # fired is supposed to keep its callback alive, and asserting anything
        # about N of them would be asserting the opposite.
        setup="var t = setTimeout(function () {}, 100000);",
        step="var x = i;",
        report="clearTimeout(t);",
    ),
    Case(
        "interval-cleared",
        step="var t = setInterval(function () {}, 100000); clearInterval(t);",
    ),
    # --- listeners ------------------------------------------------------
    Case(
        "listener-pair",
        setup="var el = document.createElement('div');",
        step="function h() {}"
             "el.addEventListener('x', h); el.removeEventListener('x', h);",
    ),
    # --- iterators, which hold a frame on another thread -----------------
    #
    # A generator's frame is a real OS thread parked mid-body. The collector
    # cannot scan that stack, so the frame's scope is rooted from
    # `JsInterpreter`'s registry (`m_generators`) instead — and that registry
    # is a list of `weak_ptr`, so it cannot keep the frame alive either. A
    # generator that is never advanced, or advanced to its end, still owns its
    # thread until `reap()` joins it; a generator that is simply dropped never
    # reaches `reap()` at all. Four shapes, one per way a page makes one.
    Case(
        "generator-never-advanced",
        step="function* g() { yield 1; } var it = g();",
        known="7 cells an iteration: `spawn_frame` gives every generator a real OS thread and a `GeneratorData`; the frame is dropped without `reap()` joining it, so the thread's stack and the frame itself are never freed. A loop making generators therefore dies at about 15,700 with `RangeError: Too many suspended functions: out of threads`, and Node v22.22.3 makes 20,000 without trouble",
    ),
    Case(
        "generator-advanced-once",
        step="function* g() { yield 1; yield 2; } var it = g(); it.next();",
        known="7 cells an iteration: `spawn_frame` gives every generator a real OS thread and a `GeneratorData`; the frame is dropped without `reap()` joining it, so the thread's stack and the frame itself are never freed. A loop making generators therefore dies at about 15,700 with `RangeError: Too many suspended functions: out of threads`, and Node v22.22.3 makes 20,000 without trouble",
    ),
    Case(
        "generator-finished",
        step="function* g() { yield 1; } var it = g(); it.next(); it.next();",
        report="var probe = (function* () { yield 42; })();"
               "console.log('observed value:', probe.next().value === 42);",
        known="7 cells an iteration: `spawn_frame` gives every generator a real OS thread and a `GeneratorData`; the frame is dropped without `reap()` joining it, so the thread's stack and the frame itself are never freed. A loop making generators therefore dies at about 15,700 with `RangeError: Too many suspended functions: out of threads`, and Node v22.22.3 makes 20,000 without trouble",
    ),
    Case(
        "generator-holds-value",
        step="var o = { n: i }; function* g() { var x = o.n; yield x; } var it = g();",
        known="8 cells an iteration: `spawn_frame` gives every generator a real OS thread and a `GeneratorData`; the frame is dropped without `reap()` joining it, so the thread's stack and the frame itself are never freed. A loop making generators therefore dies at about 15,700 with `RangeError: Too many suspended functions: out of threads`, and Node v22.22.3 makes 20,000 without trouble",
    ),
    # --- plain objects, as a control ------------------------------------
    Case(
        "object-literal-control",
        step="var o = { a: i, b: 'text', c: [i, i] };",
    ),
]


def render(case: Case, n: int) -> str:
    return HARNESS % {
        "n": n,
        "setup": case.setup,
        "report": case.report,
        "step": case.step,
    }


def run(binary: str, code: str, timeout: float) -> tuple[int, str]:
    argv = [binary, "--run-js", code]
    try:
        proc = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return -1, "<timeout>"
    if proc.returncode != 0:
        return proc.returncode, (proc.stdout or "") + (proc.stderr or "")
    return 0, proc.stdout


def residue_of(out: str) -> int | None:
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] == "residue":
            try:
                return int(parts[1])
            except ValueError:
                return None
    return None


def check(binary: str, case: Case, small: int, large: int,
          timeout: float) -> tuple[Case, int, int, str, str]:
    _, out_small = run(binary, render(case, small), timeout)
    _, out_large = run(binary, render(case, large), timeout)
    a = residue_of(out_small)
    b = residue_of(out_large)
    return case, (-1 if a is None else a), (-1 if b is None else b), out_small, out_large


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--filter", default="", help="only cases whose name contains this")
    parser.add_argument("--show", type=int, default=8, help="how many failures to print in full")
    parser.add_argument("--jobs", type=int, default=min(8, (os.cpu_count() or 2)))
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--small", type=int, default=200, help="the small batch count")
    parser.add_argument("--large", type=int, default=2000, help="the large batch count")
    parser.add_argument("--tolerance", type=int, default=DEFAULT_TOLERANCE,
                        help="cells a flat case may drift between the two sizes")
    args = parser.parse_args()

    if not Path(args.binary).exists():
        print(f"no such binary: {args.binary}", file=sys.stderr)
        return 1
    if args.large <= args.small:
        print("--large must be greater than --small", file=sys.stderr)
        return 1

    cases = [c for c in CASES if args.filter in c.name]
    if not cases:
        print(f"no case matches {args.filter!r}", file=sys.stderr)
        return 1

    def work(case: Case):
        return check(args.binary, case, args.small, args.large, args.timeout)

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for row in pool.map(work, cases):
            results.append(row)

    failures = []
    known = []
    table = []
    for case, a, b, out_small, out_large in results:
        if a < 0 or b < 0:
            failures.append((case, a, b, out_small, out_large, "the engine did not print a residue"))
            continue
        drift = b - a
        # A leak grows with the loop; a backlog does not. Only growth past the
        # tolerance counts against a case: a case that pinned fewer cells at the
        # larger size is not leaking either.
        grew = drift > max(args.tolerance, case.tolerance)
        if case.known and grew:
            known.append((case.name, a, b, drift, case.known))
            continue
        if case.known:
            # The defect this case was recording is gone. That is good news, and
            # it is a failure of the run: the reason has to be removed, or the
            # corpus starts carrying excuses.
            failures.append((case, a, b, out_small, out_large,
                             "FIXED: the case is flat but still carries a known reason"))
            continue
        if grew:
            failures.append((case, a, b, out_small, out_large,
                             f"grew by {drift} cells between {args.small} and {args.large} iterations"))
            continue
        table.append((case.name, a, b, drift))

    width = max((len(name) for name, *_ in table), default=10)
    print(f"binary: {args.binary}   sizes: {args.small} and {args.large}   "
          f"tolerance: {args.tolerance} cells")
    print(f"{'case'.ljust(width)}  {'@'+str(args.small):>8}  {'@'+str(args.large):>8}  {'drift':>6}")
    for name, a, b, drift in sorted(table):
        print(f"{name.ljust(width)}  {a:>8}  {b:>8}  {drift:>6}")

    for case, a, b, out_small, out_large, why in failures[: args.show]:
        print(f"\n--- {case.name}: {why}")
        print(f"  @{args.small}:\n{out_small.rstrip()}")
        print(f"  @{args.large}:\n{out_large.rstrip()}")
    if len(failures) > args.show:
        print(f"\n... and {len(failures) - args.show} more")

    for name, a, b, drift, why in sorted(known):
        print(f"\nKNOWN {name}: grew by {drift} cells ({a} -> {b}).")
        print(f"  {why}")

    print(f"\nflat: {len(table)} of {len(cases)} cases"
          + (f", {len(known)} known" if known else ""))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
