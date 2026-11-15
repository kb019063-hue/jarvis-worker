#!/usr/bin/env python3
"""Times attribute-list removals through the DOM, with correctness checks.

The old list kept items and index numbers in vectors. Removing the first
attribute shifted both tails, so draining 50,000 attributes from the front
took seconds while draining from the back was milliseconds. Each case builds
an element, removes attributes in a stated order, and prints a small checksum.
The checksum must match the expected result and, when supplied, the baseline
binary. Times are whole-process times (CLI start-up included).

Usage:
    python3 tools/bench/bench_attributes.py
    python3 tools/bench/bench_attributes.py --only front --reps 3
    python3 tools/bench/bench_attributes.py --max-ms 3000
    python3 tools/bench/bench_attributes.py --baseline build/litebird-before
    python3 tools/bench/bench_attributes.py --size 100000

There is no Node reference by default: these are DOM operations, not plain
JavaScript. Use --baseline to compare two Litebird builds; expected checksums
are checked either way.
"""
import argparse
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def cases(size):
    half = size // 2
    updated = set(range(half, size, 997))
    last_value = f"u{size - 1}" if (size - 1) in updated else str(size - 1)
    empty = '1|z|last|false|null|<div z="last"></div>'
    build = "var e=document.createElement('div');for(var i=0;i<%d;i++)e.setAttribute('a'+i,String(i));" % size
    finish = "e.setAttribute('z','last');console.log([e.attributes.length,e.attributes[0].name,e.attributes[0].value,e.hasAttribute('a0'),String(e.getAttribute('a%d')),e.outerHTML].join('|'));" % (size - 1)
    return [
        ("front: remove a0..aN", build +
         "for(var i=0;i<%d;i++)e.removeAttribute('a'+i);" % size + finish, empty),
        ("back: remove aN..a0", build +
         "for(var i=%d;i>=0;i--)e.removeAttribute('a'+i);" % (size - 1) + finish, empty),
        ("ends: alternate front/back", build +
         "for(var i=0;i<%d;i++){e.removeAttribute('a'+i);e.removeAttribute('a'+(%d-i));}" % (half, size - 1) +
         ("if(%d%%2)e.removeAttribute('a'+%d);" % (size, half) if size % 2 else "") + finish, empty),
        ("front half, then update/reinsert", build +
         "for(var i=0;i<%d;i++)e.removeAttribute('a'+i);" % half +
         "for(var i=%d;i<%d;i+=997)e.setAttribute('a'+i,'u'+i);" % (half, size) +
         "e.setAttribute('z','last');console.log([e.attributes.length,e.attributes[0].name,e.attributes[e.attributes.length-1].name,e.getAttribute('a%d'),e.hasAttribute('a0'),e.getAttribute('a%d')].join('|'));" % (half, size - 1),
         f"{size - half + 1}|a{half}|z|u{half}|false|{last_value}"),
        ("namespaces: front removal",
         "var e=document.createElementNS('urn:x','e');for(var i=0;i<%d;i++)e.setAttributeNS('urn:n'+(i%%7),'p'+(i%%7)+':a'+i,String(i));" % size +
         "for(var i=0;i<%d;i++)e.removeAttributeNS('urn:n'+(i%%7),'a'+i);" % size +
         "console.log([e.attributes.length,e.hasAttributeNS('urn:n0','a0'),e.hasAttribute('p0:a0'),e.outerHTML].join('|'));",
         "0|false|false|<e></e>"),
        ("short lists: churn below index",
         "var e=document.createElement('div'),rounds=Math.max(1,Math.floor(%d/10));" % size +
         "for(var r=0;r<rounds;r++){for(var i=0;i<8;i++)e.setAttribute('b'+i,String(r));for(var i=0;i<8;i++)e.removeAttribute('b'+i);}"
         "e.setAttribute('final','ok');console.log([e.attributes.length,e.attributes[0].name,e.getAttribute('final'),e.hasAttribute('b0')].join('|'));",
         "1|final|ok|false"),
    ]


def run(binary, script, page, timeout):
    start = time.perf_counter()
    try:
        proc = subprocess.run([binary, "--run-script", str(script), str(page)],
                              capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "", f"timeout after {timeout:g}s"
    elapsed = time.perf_counter() - start
    out = proc.stdout.strip()
    if proc.returncode != 0:
        return elapsed, out, f"exit {proc.returncode}: {(proc.stderr or out).strip()[:120]}"
    return elapsed, out, ""


def best_of(binary, script, page, timeout, reps):
    best = None
    out = ""
    error = ""
    for _ in range(reps):
        elapsed, out, error = run(binary, script, page, timeout)
        if error:
            return None, out, error
        best = elapsed if best is None else min(best, elapsed)
    return best, out, ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default="build/litebird")
    ap.add_argument("--baseline", default="", help="optional older Litebird binary to compare")
    ap.add_argument("--only", default="", help="run cases whose name contains this")
    ap.add_argument("--size", type=int, default=50000, help="attributes per large case (default 50000)")
    ap.add_argument("--timeout", type=float, default=30.0, help="seconds per run (default 30)")
    ap.add_argument("--reps", type=int, default=1, help="repetitions; best time is reported")
    ap.add_argument("--max-ms", type=float, default=0.0, help="fail when a case exceeds this whole-process time")
    args = ap.parse_args()
    if args.size < 2 or args.reps < 1 or args.timeout <= 0:
        ap.error("--size must be >=2, --reps >=1, --timeout >0")
    for binary in [args.binary] + ([args.baseline] if args.baseline else []):
        if not Path(binary).is_file():
            ap.error(f"binary not found: {binary}")
    selected = [(name, code, expected) for name, code, expected in cases(args.size) if args.only in name]
    if not selected:
        ap.error("no cases match --only")

    failures = 0
    runs = 0
    print(f"{'case':34} {'size':>7} {'current':>10} {'baseline':>10} {'speedup':>8}  result")
    with tempfile.TemporaryDirectory(prefix="litebird-attributes-") as directory:
        root = Path(directory)
        page = root / "page.html"
        page.write_text("<!doctype html><body></body>", encoding="utf-8")
        script = root / "case.js"
        for name, code, expected in selected:
            script.write_text(code, encoding="utf-8")
            elapsed, out, error = best_of(args.binary, script, page, args.timeout, args.reps)
            runs += 1
            note = error
            if not note and out != expected:
                note = f"MISMATCH: expected {expected!r}, got {out!r}"
            if not note and args.max_ms and elapsed * 1000 > args.max_ms:
                note = f"TOO SLOW (limit {args.max_ms:g}ms)"
            baseline_time = ""
            speedup = ""
            if args.baseline:
                old_elapsed, old_out, old_error = best_of(args.baseline, script, page, args.timeout, 1)
                if old_error:
                    baseline_time = "failed"
                    if not note:
                        note = f"baseline: {old_error}"
                elif old_out != expected:
                    baseline_time = f"{old_elapsed * 1000:8.0f}ms"
                    if not note:
                        note = "BASELINE MISMATCH"
                else:
                    baseline_time = f"{old_elapsed * 1000:8.0f}ms"
                    if elapsed and elapsed > 0:
                        speedup = f"{old_elapsed / elapsed:7.1f}x"
            if note:
                failures += 1
            shown = f"{elapsed * 1000:8.0f}ms" if elapsed is not None else "failed"
            print(f"{name:34} {args.size:7} {shown:>10} {baseline_time:>10} {speedup:>8}  {note or out}", flush=True)
    print(f"\n{runs} case(s); {failures} failure(s). Checksums compared with the expected DOM result"
          + (" and the baseline binary." if args.baseline else "."))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
