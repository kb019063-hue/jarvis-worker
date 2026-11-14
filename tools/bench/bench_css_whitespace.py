#!/usr/bin/env python3
"""Measure CSS whitespace trimming through the real page pipeline.

Every case has a compact page and the same page with long runs of spaces in
places the CSS parser or style resolver used to trim by erasing the first
character repeatedly. The layout and paint dumps must agree at every size.
An optional baseline binary must also produce the same dump; its time is
reported beside the current binary's, so a speed claim can be reproduced.

Examples:
    python3 tools/bench/bench_css_whitespace.py
    python3 tools/bench/bench_css_whitespace.py --sizes 3000,30000 --reps 1
    python3 tools/bench/bench_css_whitespace.py --baseline build/litebird-before
    make bench-css

Times include process start-up, HTML parsing, script setup, style, layout and
paint. They are not measurements of a trim function in isolation. With
--max-ms, a slow, failed, timed-out or different result is a failure.
"""
import argparse
import hashlib
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def page(css, body):
    return f"<!doctype html><html><head><style>{css}</style></head><body>{body}</body></html>"


ITEMS = "<ul><li>one</li><li>two</li><li>three</li></ul>"
GRID = '<div id="grid"><div>one</div><div>two</div><div>three</div></div>'
BOX = '<div id="box" data-kind="card" data-ready>hello</div>'

# The placeholder is replaced by a run of ASCII spaces, never by markup.
CASES = [
    ("nth-child formula",
     "li:nth-child({p}2n{p}+1{p}){color:rgb(255,0,0)}", ITEMS),
    ("attribute name and value",
     'div[{p}data-kind{p}={p}"card"{p}]{color:rgb(255,0,0)}', BOX),
    ("attribute presence",
     "div[{p}data-ready{p}]{color:rgb(0,128,0)}", BOX),
    ("keyframe selector",
     "@keyframes fade{{0%{opacity:1}{p}50%{p}{opacity:0.5}100%{opacity:1}}}"
     "#box{animation-name:fade;animation-duration:1s}", BOX),
    ("grid minmax",
     "#grid{display:grid;width:600px;grid-template-columns:"
     "minmax({p}100px{p},{p}1fr{p}) minmax({p}200px{p},{p}2fr{p})}", GRID),
    ("grid repeat",
     "#grid{display:grid;width:600px;grid-template-columns:"
     "repeat({p}auto-fit{p},{p}minmax({p}100px{p},{p}1fr{p}){p})}", GRID),
    ("filter name, argument and colour",
     "#box{filter:{p}blur({p}5px{p}){p}drop-shadow({p}2px 3px 4px{p}#ff0000{p})}", BOX),
    ("shorthand tokens",
     "#box{margin:{p}1px {p}2px {p}3px {p}4px;font:{p}bold {p}20px {p}serif;"
     "border:{p}2px solid rgb(0,0,255)}", BOX),
    ("durations and grid area",
     "#box{animation-duration:{p}250ms{p};transition-duration:{p}500ms{p};"
     "grid-area:{p}1{p}/{p}2{p}/{p}3{p}/{p}4{p}}", BOX),
    ("transform functions and arguments",
     "#box{transform:{p}translate({p}10px{p},{p}20px{p}){p}"
     "rotate({p}30deg{p}){p}scale({p}2{p},{p}3{p})}", BOX),
]


def run(binary, path, timeout):
    start = time.perf_counter()
    try:
        proc = subprocess.run(
            [binary, "--dump-layout", "--dump-paint", str(path)],
            capture_output=True, timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return None, None, f"timeout after {timeout:g}s"
    elapsed = time.perf_counter() - start
    if proc.returncode != 0:
        detail = proc.stderr.decode("utf-8", "replace").strip()[:200]
        return elapsed, None, f"exit {proc.returncode}: {detail}"
    if b"Sanitizer" in proc.stderr or b"runtime error:" in proc.stderr:
        return elapsed, None, proc.stderr.decode("utf-8", "replace").strip()[:200]
    return elapsed, proc.stdout, ""


def best_of(binary, path, timeout, reps):
    best = None
    output = None
    for _ in range(reps):
        elapsed, dump, error = run(binary, path, timeout)
        if error:
            return None, None, error
        if output is not None and dump != output:
            return None, None, "nondeterministic layout/paint dump"
        output = dump
        best = elapsed if best is None else min(best, elapsed)
    return best, output, ""


def digest(dump):
    return hashlib.sha256(dump).hexdigest()[:12]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default="build/litebird")
    ap.add_argument("--baseline", default="", help="optional old binary: compare both output and time")
    ap.add_argument("--sizes", default="10000,100000,300000", help="spaces per placeholder (comma-separated)")
    ap.add_argument("--only", default="", help="cases whose name contains this text")
    ap.add_argument("--reps", type=int, default=3, help="report the best of this many runs (default 3)")
    ap.add_argument("--timeout", type=float, default=30.0, help="seconds per process (default 30)")
    ap.add_argument("--max-ms", type=float, default=0.0, help="fail above this time; 0 reports without a time limit")
    args = ap.parse_args()
    try:
        sizes = [int(value) for value in args.sizes.split(",")]
    except ValueError:
        ap.error("--sizes must be comma-separated non-negative integers")
    if not sizes or any(size < 0 for size in sizes) or args.reps < 1 or args.timeout <= 0:
        ap.error("sizes must be non-negative, reps positive, and timeout positive")
    for binary in [args.binary] + ([args.baseline] if args.baseline else []):
        if not Path(binary).is_file():
            ap.error(f"binary not found: {binary}")

    cases = [(name, css, body) for name, css, body in CASES if args.only in name]
    if not cases:
        ap.error("no cases match --only")
    failures = 0
    runs = 0
    print(f"{'case':36} {'spaces':>7} {'current':>10} {'baseline':>10} {'speedup':>8}  result")
    with tempfile.TemporaryDirectory(prefix="litebird-css-whitespace-") as directory:
        root = Path(directory)
        for name, template, body in cases:
            compact = root / "compact.html"
            padded = root / "padded.html"
            compact.write_text(page(template.replace("{p}", ""), body), encoding="utf-8")
            _, expected, error = best_of(args.binary, compact, args.timeout, 1)
            if error:
                print(f"{name:36} {'compact':>7}  FAIL: {error}")
                failures += 1
                continue
            for size in sizes:
                padded.write_text(page(template.replace("{p}", " " * size), body), encoding="utf-8")
                elapsed, dump, error = best_of(args.binary, padded, args.timeout, args.reps)
                runs += 1
                note = error
                if not note and dump != expected:
                    note = f"MISMATCH: compact {digest(expected)}, padded {digest(dump)}"
                if not note and args.max_ms and elapsed * 1000 > args.max_ms:
                    note = f"TOO SLOW (limit {args.max_ms:g}ms)"
                baseline_time = ""
                speedup = ""
                if args.baseline:
                    old_elapsed, old_dump, old_error = best_of(args.baseline, padded, args.timeout, 1)
                    if old_error:
                        baseline_time = "failed"
                        if not note:
                            note = f"baseline: {old_error}"
                    elif old_dump != expected:
                        baseline_time = f"{old_elapsed * 1000:8.0f}ms"
                        if not note:
                            note = "BASELINE MISMATCH"
                    else:
                        baseline_time = f"{old_elapsed * 1000:8.0f}ms"
                        if elapsed > 0:
                            speedup = f"{old_elapsed / elapsed:7.1f}x"
                if note:
                    failures += 1
                shown = f"{elapsed * 1000:8.0f}ms" if elapsed is not None else "failed"
                result = note or f"same dump {digest(dump)}"
                print(f"{name:36} {size:7} {shown:>10} {baseline_time:>10} {speedup:>8}  {result}", flush=True)
    print(f"\n{runs} padded runs; {failures} failure(s). Output compared with the compact page"
          + (" and the baseline binary." if args.baseline else "."))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
