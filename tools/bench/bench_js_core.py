#!/usr/bin/env python3
"""What the tree-walking evaluator costs per operation, beside Node.js.

Run: python3 tools/bench/bench_js_core.py build/litebird
     python3 tools/bench/bench_js_core.py build/litebird --baseline /tmp/litebird-before
     python3 tools/bench/bench_js_core.py build/litebird --no-reference

Every case is one script, run the way the JavaScript scoreboard runs one: the
engine loads a one-paragraph page and then `--run-script`s the file as a
deferred script, so what is measured is the evaluator and not the parser of a
page. Each case is run `--reps` times, alternating the two binaries, and the median
of each is what is reported: a scheduler hiccup, or another tenant on the same
box, must not be what a number says.

The number a reader should look at is the ratio to Node.js on the same box, not
the milliseconds: milliseconds move with the host, and the row this bench
exists for (ROADMAP.md, "the tree-walker is 50-100x slower than V8 on loops and
calls") is a ratio. `--max-ratio` turns it into a check: the run fails when a
case is slower than that many times Node.js, so a change that gives speed back
is caught rather than described.

`--reference CMD` names the reference engine (default: `node`). Without it, the
engine's own milliseconds are still reported, and nothing is compared.
"""

from __future__ import annotations

import argparse
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path


# Every case is a function, so that what is measured is a call and not a global
# object lookup: a top-level `var` in a script is a property of `window` here,
# and a loop over globals measures the global object instead of the evaluator.
# That distinction is real and is the first finding of the row this bench is
# for -- see docs/phases/204-*.md.
CASES: dict[str, str] = {
    # One loop, arithmetic and one call per iteration.
    "loop": """
function main() {
    var s = 0;
    for (var i = 0; i < 1500000; i++) { s = (s + i) % 1000003; }
    return s;
}
console.log(String(main()));
""",
    # Property read and write on one object, one allocation.
    "property": """
function P(x, y) { this.x = x; this.y = y; }
function main() {
    var o = new P(1, 2), t = 0;
    for (var i = 0; i < 1500000; i++) { o.x = (o.x + o.y) % 1000; t += o.x; }
    return t;
}
console.log(String(main()));
""",
    # One function call per iteration, two arguments, one return.
    "call": """
function add(a, b) { return a + b; }
function main() {
    var t = 0;
    for (var i = 0; i < 1000000; i++) { t = add(t, 1) % 100000; }
    return t;
}
console.log(String(main()));
""",
    # Recursion: 100k calls, two per level on half of them.
    "recursion": """
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
function main() { return fib(24); }
console.log(String(main()));
""",
    # An array element read and write, and its length.
    "array": """
function main() {
    var a = [], t = 0;
    for (var i = 0; i < 200000; i++) { a[i] = i & 255; }
    for (var j = 0; j < 200000; j++) { t += a[j]; }
    return t;
}
console.log(String(main()));
""",
    # Method calls on a plain object: the receiver and a lookup per call.
    "method": """
function Counter() { this.n = 0; }
Counter.prototype.bump = function (by) { this.n = (this.n + by) % 1000; return this.n; };
function main() {
    var c = new Counter(), t = 0;
    for (var i = 0; i < 500000; i++) { t += c.bump(1); }
    return t;
}
console.log(String(main()));
""",
}


def timed(argv: list[str], timeout: float) -> float:
    started = time.perf_counter()
    finished = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
    elapsed = time.perf_counter() - started
    if finished.returncode != 0:
        sys.stderr.write(f"  `{' '.join(argv[:2])}` exited {finished.returncode}: "
                         f"{finished.stderr.strip()[:300]}\n")
        return -1.0
    return elapsed


def paired_runs(binary: str, baseline: str | None, page: Path, script: Path,
                reps: int, timeout: float) -> tuple[float, float]:
    """Medians of interleaved runs of the two binaries.

    Interleaved, because a shared machine drifts: measuring the whole of one
    binary and then the whole of the other charges each drift to whichever ran
    during it. A median, because one run can meet another tenant's compile."""
    def engine(path: str) -> list[float]:
        return [timed([path, str(page), "--run-script", str(script)], timeout)
                for _ in range(reps)]

    own: list[float] = []
    old: list[float] = []
    for _ in range(reps):
        own.append(timed([binary, str(page), "--run-script", str(script)], timeout))
        if baseline:
            old.append(timed([baseline, str(page), "--run-script", str(script)], timeout))
    if any(t < 0 for t in own):
        return -1.0, -1.0
    if baseline and any(t < 0 for t in old):
        return statistics.median(own), -1.0
    return statistics.median(own), (statistics.median(old) if old else -1.0)


def reference_seconds(command: str, script: Path, reps: int, timeout: float) -> float:
    argv = command.split() + [str(script)]
    times = [timed(argv, timeout) for _ in range(reps)]
    if any(t < 0 for t in times):
        return -1.0
    return statistics.median(times)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("binary", type=Path, help="the engine's binary")
    parser.add_argument("--baseline", type=Path, help="an earlier binary to compare against")
    parser.add_argument("--reference", default="node", help="reference engine (default: node)")
    parser.add_argument("--no-reference", action="store_true", help="do not run the reference")
    parser.add_argument("--reps", type=int, default=5,
                        help="interleaved runs per binary; the median is reported")
    parser.add_argument("--timeout", type=float, default=600.0)
    parser.add_argument("--only", nargs="+", help="run these cases only")
    parser.add_argument("--max-ratio", type=float,
                        help="fail when a case is slower than this many times the reference")
    parser.add_argument("--json", type=Path, help="write the numbers here")
    args = parser.parse_args()

    binary = str(args.binary.resolve())
    baseline = str(args.baseline.resolve()) if args.baseline else None
    has_reference = not args.no_reference and shutil.which(args.reference.split()[0]) is not None
    if not has_reference and not args.no_reference:
        print(f"note: `{args.reference}` is not on this host; ratios are not reported",
              file=sys.stderr)

    cases = [c for c in CASES if not args.only or c in args.only]
    scratch = Path(tempfile.mkdtemp(prefix="litebird-js-core-"))
    page = scratch / "page.html"
    page.write_text("<!doctype html><body><p>js core bench</p></body>")

    rows = []
    failures = []
    print(f"{'case':10} {'engine':>10} {'baseline':>10} {'speedup':>9} {'node':>10} {'ratio':>9}")
    for name in cases:
        script = scratch / f"{name}.js"
        script.write_text(CASES[name])
        own, old = paired_runs(binary, baseline, page, script, args.reps, args.timeout)
        if own < 0:
            failures.append(f"{name}: the engine failed to run the case")
            continue
        ref = reference_seconds(args.reference, script, args.reps, args.timeout) if has_reference else -1.0
        speedup = (old / own) if old > 0 else None
        ratio = (own / ref) if ref > 0 else None
        rows.append({"case": name, "seconds": own, "baseline_seconds": old if old > 0 else None,
                     "speedup": speedup, "reference_seconds": ref if ref > 0 else None,
                     "ratio": ratio})
        dash = "-"
        printed_baseline = f"{old * 1000:9.1f}ms" if old > 0 else f"{dash:>10}"
        printed_speedup = f"{speedup:8.2f}x" if speedup else f"{dash:>9}"
        printed_reference = f"{ref * 1000:9.1f}ms" if ref > 0 else f"{dash:>10}"
        printed_ratio = f"{ratio:8.1f}x" if ratio else f"{dash:>9}"
        print(f"{name:10} {own * 1000:9.1f}ms {printed_baseline} {printed_speedup} "
              f"{printed_reference} {printed_ratio}")
        if args.max_ratio is not None and ratio is not None and ratio > args.max_ratio:
            failures.append(f"{name}: {ratio:.1f}x the reference, over the {args.max_ratio}x limit")

    if rows:
        own_total = sum(r["seconds"] for r in rows)
        print(f"\n{'total':10} {own_total * 1000:9.1f}ms")
        if baseline:
            old_total = sum(r["baseline_seconds"] or 0.0 for r in rows)
            if old_total > 0:
                print(f"           {old_total * 1000:9.1f}ms before, "
                      f"{old_total / own_total:.2f}x faster")
        if has_reference:
            ratios = [r["ratio"] for r in rows if r["ratio"]]
            if ratios:
                print(f"ratio to the reference: median {statistics.median(ratios):.1f}x, "
                      f"worst {max(ratios):.1f}x")

    if args.json:
        import json
        args.json.write_text(json.dumps({"cases": rows}, indent=2) + "\n")

    if failures:
        print("\nFAILED:", file=sys.stderr)
        for line in failures:
            print(f"  {line}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
