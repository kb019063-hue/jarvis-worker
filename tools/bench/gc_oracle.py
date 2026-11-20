#!/usr/bin/env python3
"""Check the garbage collector's invariants through the CLI.

ROADMAP Tier 1 lists automatic GC and says of it: "collection only runs on
gc() today; incremental sizing, CI memory benchmarks." This is the oracle
half of that gap — what happens when it *does* collect, which can be
checked without asking a person — as opposed to the half no oracle can give
(the collector deciding when to run on its own, and whether the pacing feels
right). It answers four questions, in one process through the shell's own
`--run-js` path, so it checks the collector the way the shell meets it:

  1. reclamation — a call that churned self, mutual, closure and
     self-containing-array cycles and kept none of them leaves nothing
     behind after gc();
  2. flatness   — repeating that churn at growing sizes does not grow the
     surviving set beyond the baseline plus `--slack` cells;
  3. retention  — an object a global still names is intact after
     collections (the reclaim above did not reach the wrong cells);
  4. progress   — collections_count grows with every gc() call, and
     collected_cells ends above zero, so the check above is not passing
     because nothing ever happened.

It is not a leak audit: `make asan` owns that. It does not time the
collector. It is not a showing that the engine collects by itself — it does
not. It fails the way an oracle should: a specific number on a line, with
what it was expected to be.

Usage:
    python3 tools/bench/gc_oracle.py build/litebird
    python3 tools/bench/gc_oracle.py build/litebird
    python3 tools/bench/gc_oracle.py build/litebird --slack 16 --timeout 30
"""

import argparse
import subprocess
import sys
from pathlib import Path

SCRIPT = r"""
var rounds = [50, 150, 300, 600, 1200];
function churn(tag, n) {
    for (var i = 0; i < n; i++) {
        var a = { tag: tag + i };
        var b = { tag: tag + i, other: a };
        a.other = b;                 // mutual cycle
        a.self = a;                  // self cycle
        var captured = { tag: tag + i };
        var fn = function () { return captured; };
        captured.fn = fn;            // cycle through a closure's scope
        a.fn = fn;                   // and reachable from the object graph too
        var list = [a, b, captured];
        list.push(list);             // an array that holds itself
    }
}
// Everything the script keeps for its own sake exists before this line, so
// the baseline covers it and every check after it is a differential.
var checkpoints = [];
var baseline = gc().alive_cells;
// The same work at growing sizes: any leak shows up as a ramp, not a one-off.
for (var i = 0; i < rounds.length; i++) {
    churn("r" + i, rounds[i]);
    checkpoints.push(gc().alive_cells);
}
var afterAll = gc();
console.log(String(baseline) + "|" + checkpoints.join("|"));
console.log(String(afterAll.collections_count) + "|" + String(afterAll.collected_cells));
var keep = { tag: "keep", child: { tag: "child" } };
churn("keep", 200);
gc();
console.log(keep.tag + "|" + keep.child.tag);
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("binary", nargs="?", default="build/litebird", help="Litebird binary (default: build/litebird)")
    parser.add_argument("--slack", type=int, default=8,
                        help="cells the surviving set may exceed the baseline (default: 8)")
    parser.add_argument("--timeout", type=float, default=30.0, help="seconds allowed for one process (default: 30)")
    args = parser.parse_args()

    binary = Path(args.binary)
    if not binary.is_file():
        parser.error(f"binary not found: {binary}")
    if args.slack < 0:
        parser.error("--slack must be >= 0")

    try:
        proc = subprocess.run([str(binary), "--run-js", SCRIPT],
                              capture_output=True, text=True, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        print(f"FAIL: timeout after {args.timeout:g}s")
        return 1
    if proc.returncode:
        detail = (proc.stderr or proc.stdout).strip().replace("\n", " ")
        print(f"FAIL: exit {proc.returncode}: {detail[:200]}")
        return 1

    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if len(lines) != 3:
        print(f"FAIL: expected 3 output lines, got {len(lines)}: {proc.stdout.strip()!r}")
        return 1

    failures = []
    try:
        flat = [int(part) for part in lines[0].split("|")]
    except ValueError:
        print(f"FAIL: line 1 is not integers: {lines[0]!r}")
        return 1
    baseline = flat[0]
    checkpoints = flat[1:]
    if len(checkpoints) != 5:
        print(f"FAIL: expected 5 checkpoints, got {len(checkpoints)}")
        return 1

    print(f"baseline alive cells: {baseline}")
    for index, alive in enumerate(checkpoints, 1):
        grew = alive - baseline
        print(f"after round {index}: {alive} alive ({'+%d' % grew if grew >= 0 else grew} from baseline)")

    worst = max(checkpoints)
    if worst > baseline + args.slack:
        failures.append(f"flatness: peak {worst} exceeds baseline {baseline} + slack {args.slack}")
    if max(checkpoints) != min(checkpoints) and (max(checkpoints) - min(checkpoints)) > args.slack:
        failures.append(f"flatness: checkpoints spread {min(checkpoints)}..{max(checkpoints)} beyond slack {args.slack}")

    try:
        collections, collected = (int(part) for part in lines[1].split("|"))
    except ValueError:
        print(f"FAIL: line 2 is not integers: {lines[1]!r}")
        return 1
    print(f"collections_count: {collections}")
    print(f"collected_cells:   {collected}")
    if collections < 6:  # one per round + the final afterAll, plus the keep round's
        failures.append(f"progress: only {collections} collections happened; expected at least 6")
    if collected <= 0:
        failures.append("progress: collected_cells is 0 — nothing was ever reclaimed")

    retained = lines[2]
    print(f"retained: {retained}")
    if retained != "keep|child":
        failures.append(f"retention: expected 'keep|child', got {retained!r}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("GC oracle: PASS — cycles reclaimed, the live set stays flat, "
          "what a global names stays intact.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
