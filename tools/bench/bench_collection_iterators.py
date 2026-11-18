#!/usr/bin/env python3
"""Check that completed Map and Set iteration stays flat without gc().

Map and Set iterators keep a cursor so deletion/compaction can move an entry
without moving an iterator to a different logical item. This runner makes two
same-sized batches of completed `for...of` loops in one JavaScript process and
deliberately never calls `gc()`. Each batch sums all three values, so a timing
only counts when the loops actually visited the collection.

The guard is relative rather than an absolute wall-clock budget: a busy or slow
machine may make both batches slow, but it must not make the later batch grow
with the number of iterators made by the first. The checked-in Make target uses
8,192 loops per batch, three repetitions, and lets the later batch take at
most 2.25 times the earlier one plus 40 ms.

Usage:
    python3 tools/bench/bench_collection_iterators.py build/litebird
    python3 tools/bench/bench_collection_iterators.py build/litebird --reps 5
    python3 tools/bench/bench_collection_iterators.py build/litebird --baseline build/litebird-before

`--baseline` is reported, not required to meet the growth limit: it is useful
for showing the old progressive behaviour beside the fixed binary.
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path


CASES = (
    ("Map entries", "var collection = new Map([['one', 1], ['two', 2], ['three', 3]]);",
     "for (var pair of collection) checksum += pair[1];"),
    ("Set values", "var collection = new Set([1, 2, 3]);",
     "for (var value of collection) checksum += value;"),
)


def script(setup, visit, rounds):
    return f"""{setup}
var checksum = 0;
function runBatch() {{
    for (var i = 0; i < {rounds}; ++i) {{
        {visit}
    }}
}}
var started = Date.now();
runBatch();
var early = Date.now() - started;
started = Date.now();
runBatch();
var late = Date.now() - started;
console.log([checksum, early, late].join('|'));
"""


def run_once(binary, code, expected_checksum, timeout):
    started = time.perf_counter()
    try:
        proc = subprocess.run([str(binary), "--run-js", code], capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, f"timeout after {timeout:g}s"
    elapsed = time.perf_counter() - started
    if proc.returncode:
        detail = (proc.stderr or proc.stdout).strip().replace("\n", " ")
        return None, f"exit {proc.returncode}: {detail[:160]}"
    fields = proc.stdout.strip().split("|")
    if len(fields) != 3:
        return None, f"unexpected output: {proc.stdout.strip()!r}"
    try:
        checksum, early, late = (int(field) for field in fields)
    except ValueError:
        return None, f"non-numeric output: {proc.stdout.strip()!r}"
    if checksum != expected_checksum:
        return None, f"wrong checksum: expected {expected_checksum}, got {checksum}"
    if early < 0 or late < 0:
        return None, f"clock moved backwards: {early}ms then {late}ms"
    return (early, late, elapsed), ""


def best_of(binary, code, expected_checksum, timeout, reps):
    """Use the least-contended complete pair, preserving its growth relation."""
    best = None
    for _ in range(reps):
        result, error = run_once(binary, code, expected_checksum, timeout)
        if error:
            return None, error
        if best is None or result[0] + result[1] < best[0] + best[1]:
            best = result
    return best, ""


def format_pair(sample):
    if sample is None:
        return ""
    early, late, _ = sample
    return f"{early:4d} -> {late:4d} ms"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("binary", nargs="?", default="build/litebird", help="Litebird binary (default: build/litebird)")
    parser.add_argument("--baseline", default="", help="optional old Litebird binary to report beside the current one")
    parser.add_argument("--rounds", type=int, default=8192, help="completed loops per batch (default: 8192)")
    parser.add_argument("--reps", type=int, default=3, help="repetitions; use the least-contended pair (default: 3)")
    parser.add_argument("--timeout", type=float, default=10.0, help="seconds allowed for one process (default: 10)")
    parser.add_argument("--max-growth", type=float, default=2.25,
                        help="maximum late/early growth before slack (default: 2.25)")
    parser.add_argument("--slack-ms", type=float, default=40.0,
                        help="extra milliseconds allowed for scheduler noise (default: 40)")
    args = parser.parse_args()

    if args.rounds < 1 or args.reps < 1 or args.timeout <= 0 or args.max_growth < 1 or args.slack_ms < 0:
        parser.error("--rounds and --reps must be positive, --timeout positive, --max-growth >= 1, --slack-ms >= 0")
    binaries = [Path(args.binary)] + ([Path(args.baseline)] if args.baseline else [])
    for binary in binaries:
        if not binary.is_file():
            parser.error(f"binary not found: {binary}")

    expected_checksum = 12 * args.rounds  # two batches, each summing 1 + 2 + 3 per loop
    failures = 0
    print(f"{'case':14} {'rounds':>7} {'current (early -> late)':>25} {'limit':>10} {'baseline':>22}  result")
    for name, setup, visit in CASES:
        code = script(setup, visit, args.rounds)
        current, error = best_of(Path(args.binary), code, expected_checksum, args.timeout, args.reps)
        baseline = None
        baseline_error = ""
        if args.baseline:
            baseline, baseline_error = best_of(Path(args.baseline), code, expected_checksum, args.timeout, args.reps)

        note = error
        limit = ""
        if current and not note:
            early, late, _ = current
            allowed = early * args.max_growth + args.slack_ms
            limit = f"{allowed:7.1f} ms"
            if late > allowed:
                note = f"GREW: late {late}ms exceeds {allowed:.1f}ms"
        if baseline_error:
            baseline_text = "failed"
            if not note:
                note = f"baseline: {baseline_error}"
        else:
            baseline_text = format_pair(baseline)
        if note:
            failures += 1
        current_text = format_pair(current) if current else "failed"
        print(f"{name:14} {args.rounds:7d} {current_text:>25} {limit:>10} {baseline_text:>22}  {note or 'checksum and growth ok'}",
              flush=True)

    print(f"\n{len(CASES)} case(s); {failures} failure(s). Every batch ran in one process without gc(), "
          f"and each printed checksum {expected_checksum}.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
