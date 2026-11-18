#!/usr/bin/env python3
"""Measure peak memory and time for style-heavy inline pages.

Every case repeats an inline construct N times (by default 100,000, matching
the `word <b>bold</b>` workload from AUDIT.md finding 12) and runs the real
page pipeline through HTML parsing, style resolution, layout, and display-list
generation (`--dump-paint`).

On Linux, peak resident set size (`ru_maxrss`) is collected directly from the
child process via `os.wait4`. An optional `--baseline` binary is measured beside
the current binary and checked for byte-identical `--dump-paint` output.

Usage:
    python3 tools/bench/bench_shared_style.py build/litebird --max-rss-mb 600 --max-seconds 10
    python3 tools/bench/bench_shared_style.py build/litebird --baseline .cache/litebird-main
    make bench-styles
"""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def make_page(css: str, unit: str, count: int) -> str:
    head = f"<style>{css}</style>" if css else ""
    body = unit * count
    return f"<!doctype html><html><head>{head}</head><body><p>{body}</p></body></html>"


CASES = (
    ("word_100k", "", "word <b>bold</b> ", 100000),
    ("link_100k", "", '<a href="#">link</a>, ', 100000),
    ("span_bg_100k", ".hl{background-color:#ff0}", '<span class="hl">hi</span> ', 100000),
)


def rss_mb_from_ru(ru) -> float:
    kb = ru.ru_maxrss / 1024.0 if sys.platform == "darwin" else float(ru.ru_maxrss)
    return kb / 1024.0


def measure_child(binary: Path, html_path: Path, timeout: float):
    started = time.perf_counter()
    with tempfile.TemporaryFile() as out_f, tempfile.TemporaryFile() as err_f:
        proc = subprocess.Popen(
            [str(binary), str(html_path), "--dump-paint"],
            stdout=out_f,
            stderr=err_f,
            close_fds=True,
        )
        pid = proc.pid
        deadline = started + timeout
        ru = None
        status = 0
        while True:
            got_pid, st, r = os.wait4(pid, os.WNOHANG)
            if got_pid == pid:
                status = st
                ru = r
                break
            if time.perf_counter() >= deadline:
                proc.kill()
                _, status, ru = os.wait4(pid, 0)
                proc._handle_exitstatus(status)
                return None, rss_mb_from_ru(ru), None, f"timeout after {timeout:g}s"
            time.sleep(0.01)
        elapsed = time.perf_counter() - started
        proc._handle_exitstatus(status)
        out_f.seek(0)
        err_f.seek(0)
        stdout = out_f.read()
        stderr = err_f.read()

    rss_mb = rss_mb_from_ru(ru)
    if proc.returncode != 0:
        detail = stderr.decode("utf-8", "replace").strip()[:180]
        return elapsed, rss_mb, None, f"exit {proc.returncode}: {detail}"
    if b"Sanitizer" in stderr or b"runtime error:" in stderr:
        return elapsed, rss_mb, None, stderr.decode("utf-8", "replace").strip()[:180]
    if b"Total commands:" not in stdout:
        return elapsed, rss_mb, None, "missing display list output"
    return elapsed, rss_mb, stdout, ""


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()[:12]


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("binary", nargs="?", default="build/litebird", help="Litebird binary (default: build/litebird)")
    parser.add_argument("--baseline", default="", help="optional old binary to compare RSS, time, and paint output")
    parser.add_argument("--scale", type=float, default=1.0, help="scale default 100k repetition counts (default: 1.0)")
    parser.add_argument("--timeout", type=float, default=30.0, help="seconds per child process (default: 30)")
    parser.add_argument("--max-rss-mb", type=float, default=0.0, help="fail if current peak RSS exceeds this (MiB)")
    parser.add_argument("--max-seconds", type=float, default=0.0, help="fail if current wall time exceeds this (s)")
    args = parser.parse_args()

    if args.scale <= 0 or args.timeout <= 0 or args.max_rss_mb < 0 or args.max_seconds < 0:
        parser.error("arguments must be non-negative (--scale and --timeout positive)")

    binary = Path(args.binary)
    if not binary.is_file():
        parser.error(f"binary not found: {binary}")
    baseline = Path(args.baseline) if args.baseline else None
    if baseline and not baseline.is_file():
        parser.error(f"baseline binary not found: {baseline}")

    failures = 0
    print(f"{'case':14} {'count':>7} {'rss':>9} {'time':>8} {'base_rss':>10} {'base_t':>8} {'rss_chg':>8}  result")
    with tempfile.TemporaryDirectory(prefix="litebird-shared-style-") as tmpdir:
        root = Path(tmpdir)
        for name, css, unit, base_count in CASES:
            count = max(1, int(round(base_count * args.scale)))
            html_path = root / f"{name}.html"
            html_path.write_text(make_page(css, unit, count), encoding="utf-8")

            elapsed, rss_mb, dump, error = measure_child(binary, html_path, args.timeout)
            note = error
            if not note and args.max_rss_mb and rss_mb > args.max_rss_mb:
                note = f"RSS {rss_mb:.1f} MiB exceeds limit {args.max_rss_mb:g} MiB"
            if not note and args.max_seconds and elapsed > args.max_seconds:
                note = f"time {elapsed:.2f}s exceeds limit {args.max_seconds:g}s"

            base_rss_s = ""
            base_t_s = ""
            chg_s = ""
            if baseline:
                b_elapsed, b_rss, b_dump, b_err = measure_child(baseline, html_path, args.timeout)
                if b_err:
                    base_rss_s = "failed"
                    if not note:
                        note = f"baseline: {b_err}"
                else:
                    base_rss_s = f"{b_rss:6.1f} MB"
                    base_t_s = f"{b_elapsed:6.2f}s"
                    if b_rss > 0 and rss_mb is not None:
                        pct = (rss_mb - b_rss) / b_rss * 100.0
                        chg_s = f"{pct:+6.1f}%"
                    if not note and dump != b_dump:
                        note = f"PAINT MISMATCH: {digest(dump)} vs {digest(b_dump)}"

            if note:
                failures += 1
            rss_s = f"{rss_mb:6.1f} MB" if rss_mb is not None else "failed"
            t_s = f"{elapsed:6.2f}s" if elapsed is not None else "failed"
            status_s = note or (f"paint {digest(dump)}" if dump else "ok")
            print(f"{name:14} {count:7d} {rss_s:>9} {t_s:>8} {base_rss_s:>10} {base_t_s:>8} {chg_s:>8}  {status_s}", flush=True)

    print(f"\n{len(CASES)} case(s); {failures} failure(s).")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
