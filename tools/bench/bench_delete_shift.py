#!/usr/bin/env python3
"""Times object deletion and array front edits in Litebird, and in a reference engine when one is installed.

These are the patterns that used to grow with the square of the input:
`delete` from a large property map rebuilt a hash index on every erase, and
`shift()` memmoved the whole tail of an array. Each script prints a result,
and the two engines must print the same one.

Usage:
    python3 tools/bench/bench_delete_shift.py                 # every case
    python3 tools/bench/bench_delete_shift.py --only shift    # cases whose name contains "shift"
    python3 tools/bench/bench_delete_shift.py --reps 3        # best of 3
    python3 tools/bench/bench_delete_shift.py --max-ms 2500   # exit 1 if any case is slower or wrong
    python3 tools/bench/bench_delete_shift.py --reference ""  # do not run a reference engine

Times are for the whole process, start-up included.
"""
import argparse
import shutil
import subprocess
import sys
import time

CASES = [
    ("delete: 50k keys",
     "var o={};for(var i=0;i<50000;i++)o['k'+i]=i;var ks=Object.keys(o);for(var i=0;i<ks.length;i++)delete o[ks[i]];"
     "console.log(Object.keys(o).length)"),
    ("delete: reinsert after erase",
     "var o={};for(var i=0;i<50000;i++)o['k'+i]=i;for(var i=0;i<50000;i++)delete o['k'+i];"
     "o.last='last';console.log(Object.keys(o).join(','))"),
    ("shift: until an array is empty",
     "var a=[];for(var i=0;i<50000;i++)a.push(i);var s=0;while(a.length)s+=a.shift();console.log(s)"),
    ("unshift: 20k times",
     "var a=[];for(var i=0;i<20000;i++)a.unshift(i);console.log(a[0],a.length)"),
    ("splice: remove the first half",
     "var a=[];for(var i=0;i<50000;i++)a.push(i);a.splice(0,25000);console.log(a.length,a[0])"),
    ("enumerate: order after delete and insert",
     "var o={};o.a='a';o.b='b';o.c='c';o.d='d';delete o.b;o.b='B';console.log(Object.keys(o).join(','))"),
]


def run(command, code, timeout):
    start = time.perf_counter()
    try:
        proc = subprocess.run(command + [code], capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "timeout"
    elapsed = time.perf_counter() - start
    out = " | ".join(proc.stdout.strip().splitlines())[:64]
    if proc.returncode != 0:
        out = f"exit {proc.returncode}: {out}"
    return elapsed, out


def best_of(command, code, timeout, reps):
    best, out = None, ""
    for _ in range(reps):
        elapsed, out = run(command, code, timeout)
        if elapsed is None:
            return None, out
        best = elapsed if best is None else min(best, elapsed)
    return best, out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default="build/litebird")
    ap.add_argument("--reference", default="node -e",
                    help="reference engine command; the script is appended (default: 'node -e'). Empty: none")
    ap.add_argument("--only", default="", help="run cases whose name contains this")
    ap.add_argument("--timeout", type=float, default=20.0, help="seconds per case (default 20)")
    ap.add_argument("--reps", type=int, default=1, help="repetitions; the best time is reported")
    ap.add_argument("--max-ms", type=float, default=0.0,
                    help="fail when a Litebird case takes longer than this, times out or prints a different result")
    args = ap.parse_args()

    reference = args.reference.split()
    if reference and not shutil.which(reference[0]):
        print(f"note: {reference[0]} is not installed; running Litebird only", file=sys.stderr)
        reference = []

    failures = 0
    print(f"{'case':44} {'litebird':>10} {'reference':>10}  result")
    for name, code in CASES:
        if args.only and args.only not in name:
            continue
        elapsed, out = best_of([args.binary, "--run-js"], code, args.timeout, args.reps)
        shown = f"{elapsed * 1000:8.0f}ms" if elapsed is not None else f"  >{args.timeout:.0f}s"
        ref_shown, note = "", ""
        if reference:
            ref_elapsed, ref_out = best_of(reference, code, 60.0, args.reps)
            ref_shown = f"{ref_elapsed * 1000:8.0f}ms" if ref_elapsed is not None else "timeout"
            if elapsed is not None and out != ref_out:
                note = f"   MISMATCH, reference printed: {ref_out}"
                failures += 1
        if elapsed is None or out.startswith("exit ") or (args.max_ms and elapsed * 1000 > args.max_ms):
            if not note:
                note = "   TOO SLOW" if elapsed is None or not out.startswith("exit ") else "   FAILED"
            failures += 1
        print(f"{name:44} {shown:>10} {ref_shown:>10}  {out}{note}", flush=True)

    if failures and args.max_ms:
        print(f"\n{failures} case(s) failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
