#!/usr/bin/env python3
"""Runs the everyday-JavaScript corpus against Litebird's JS engine.

This is the JavaScript scoreboard, the counterpart of run_tree_construction.py.
Every case is a small script whose expected standard output was taken from a
reference engine (Node.js), never from Litebird itself. A case passes when
`litebird --run-js <code>` prints exactly that output and exits 0.

Usage:
    python3 tools/conformance/run_js_conformance.py [options]

Options:
    --binary PATH      Litebird binary (default: build/litebird)
    --corpus PATH      Corpus directory (default: tests/js-conformance)
    --filter SUBSTR    Only run .dat files whose name contains SUBSTR
    --show N           Print up to N failing cases with expected/actual output
    --jobs N           Parallel engine processes (default: os.cpu_count())
    --timeout SECS     Per-case timeout (default: 10)
    --quiet            Only print the summary lines
    --json PATH        Write a machine-readable summary to PATH
    --min-pass N       Exit 1 if fewer than N cases pass (the CI floor)

Whatever the floor says, the run fails if any case crashes the engine or
prints an AddressSanitizer / UBSan / ThreadSanitizer report: those are
memory-safety bugs, and a case that prints the right answer after UBSan
complained is counted as a failure.
    --reference CMD    Run the corpus under a reference engine instead, e.g.
                       --reference "node -e". Exits 1 on any mismatch. This is
                       how the expectations themselves are kept honest.

Corpus format (see tests/js-conformance/README.md):
    #name <title>
    #code
    <one or more lines of JavaScript>
    #expect
    <exact stdout>
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

DEFAULT_BINARY = "build/litebird"
DEFAULT_CORPUS = "tests/js-conformance"
# The fixed 90-case probe the headline number is reported on.
HEADLINE_PREFIX = "everyday-"


@dataclass
class Case:
    source: str
    name: str
    code: str
    expect: str


@dataclass
class Result:
    case: Case
    actual: str
    passed: bool
    returncode: int = 0
    stderr: str = ""
    timed_out: bool = False
    sanitizer: bool = False


# What AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer print.
# UBSan in particular reports and then carries on, so a case can print the
# right answer and exit 0 while the engine did something undefined. A pass
# count alone would never show that; the runner has to read stderr.
SANITIZER_REPORT = re.compile(
    r"ERROR: (Address|Leak|Thread|Memory|UndefinedBehavior)Sanitizer"
    r"|WARNING: ThreadSanitizer"
    r"|: runtime error: ")


def parse_dat(path: Path) -> list[Case]:
    """Splits a .dat file into cases.

    A case begins at a `#name ` line. `#code` and `#expect` switch sections.
    Blank lines inside a section belong to it; trailing blank lines are
    dropped so cases can be separated by an empty line.
    """
    cases: list[Case] = []
    name: str | None = None
    sections: dict[str, list[str]] = {}
    mode: str | None = None

    def flush() -> None:
        if name is None:
            return
        if "code" not in sections or "expect" not in sections:
            raise ValueError(f"{path.name}: case {name!r} needs #code and #expect")
        code = "\n".join(sections["code"]).strip("\n")
        expect = "\n".join(sections["expect"]).rstrip("\n")
        cases.append(Case(path.name, name, code, expect))

    for line in path.read_text(encoding="utf-8").split("\n"):
        if line.startswith("#name "):
            flush()
            name, sections, mode = line[len("#name "):].strip(), {}, None
        elif line == "#code" and name is not None:
            mode = "code"
            sections[mode] = []
        elif line == "#expect" and name is not None:
            mode = "expect"
            sections[mode] = []
        elif mode is not None:
            sections[mode].append(line)
    flush()
    return cases


def run_case(command: list[str], case: Case, timeout: float) -> Result:
    env = dict(os.environ, TZ="UTC")
    try:
        completed = subprocess.run(command + [case.code], capture_output=True,
                                   timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        return Result(case, "", False, returncode=-1, stderr="timeout", timed_out=True)
    actual = completed.stdout.decode("utf-8", "replace").rstrip("\n")
    stderr = completed.stderr.decode("utf-8", "replace").strip()
    sanitizer = bool(SANITIZER_REPORT.search(stderr))
    passed = completed.returncode == 0 and actual == case.expect and not sanitizer
    return Result(case, actual, passed, completed.returncode, stderr, sanitizer=sanitizer)


def failure_block(result: Result) -> str:
    out = [f"  {result.case.source}: {result.case.name}"]
    for line in result.case.code.split("\n")[:12]:
        out.append(f"    | {line}")
    out.append(f"    expected: {result.case.expect!r}")
    out.append(f"    actual:   {result.actual!r}")
    if result.returncode != 0:
        out.append(f"    exit:     {result.returncode}")
    if result.sanitizer:
        report = next(line for line in result.stderr.splitlines() if SANITIZER_REPORT.search(line))
        out.append(f"    sanitizer: {report[:240]}")
    elif result.stderr:
        out.append(f"    stderr:   {result.stderr.splitlines()[-1][:200]}")
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--corpus", default=DEFAULT_CORPUS)
    parser.add_argument("--filter", default=None)
    parser.add_argument("--show", type=int, default=0)
    parser.add_argument("--jobs", type=int, default=min(16, os.cpu_count() or 4))
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--json", default=None)
    parser.add_argument("--min-pass", type=int, default=None)
    parser.add_argument("--reference", default=None)
    args = parser.parse_args()

    corpus = Path(args.corpus)
    if not corpus.is_dir():
        print(f"error: corpus directory not found: {corpus}", file=sys.stderr)
        return 2

    if args.reference:
        command = shlex.split(args.reference)
        label = f"reference `{args.reference}`"
    else:
        if not Path(args.binary).exists():
            print(f"error: binary not found: {args.binary} (run `make` first)", file=sys.stderr)
            return 2
        command = [args.binary, "--run-js"]
        label = "Litebird"

    files = sorted(corpus.glob("*.dat"))
    if args.filter:
        files = [f for f in files if args.filter in f.name]
    cases: list[Case] = []
    for path in files:
        cases.extend(parse_dat(path))
    if not cases:
        print("error: no test cases found", file=sys.stderr)
        return 2

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda c: run_case(command, c, args.timeout), cases))

    passed = sum(1 for r in results if r.passed)
    total = len(results)
    rate = 100.0 * passed / total

    per_file: dict[str, list[int]] = {}
    for result in results:
        entry = per_file.setdefault(result.case.source, [0, 0])
        entry[1] += 1
        if result.passed:
            entry[0] += 1

    headline = [r for r in results if r.case.source.startswith(HEADLINE_PREFIX)]
    headline_passed = sum(1 for r in headline if r.passed)
    crashes = sum(1 for r in results if r.returncode < 0 and not r.timed_out)
    timeouts = sum(1 for r in results if r.timed_out)
    sanitizer_hits = [r for r in results if r.sanitizer]

    if not args.quiet:
        print(f"Per-file results for {label} (passed/total):")
        for name in sorted(per_file, key=lambda n: (per_file[n][0] / per_file[n][1], n)):
            ok, count = per_file[name]
            flag = " " if ok == count else "!"
            print(f" {flag} {name:<30} {ok:>4}/{count:<4} {100.0 * ok / count:5.1f}%")

        failures = [r for r in results if not r.passed]
        if args.show:
            print(f"\nFirst {min(args.show, len(failures))} failures:")
            for result in failures[:args.show]:
                print(failure_block(result))
                print()

    print()
    if headline:
        print(f"Everyday probe (fixed {len(headline)} cases): {headline_passed}/{len(headline)} "
              f"({100.0 * headline_passed / len(headline):.1f}%)")
    print(f"JS conformance, full corpus: {passed}/{total} ({rate:.1f}%)  "
          f"[{len(files)} files, {crashes} crashes, {timeouts} timeouts]")
    if sanitizer_hits:
        # Printed even with --quiet: this is never noise.
        print(f"\nSanitizer reports in {len(sanitizer_hits)} case(s):")
        for result in sanitizer_hits[:20]:
            print(failure_block(result))
            print()

    if args.json:
        Path(args.json).write_text(json.dumps({
            "passed": passed,
            "total": total,
            "rate": rate,
            "headline": {"passed": headline_passed, "total": len(headline)},
            "crashes": crashes,
            "timeouts": timeouts,
            "sanitizer_reports": [f"{r.case.source}: {r.case.name}" for r in sanitizer_hits],
            "per_file": {name: {"passed": v[0], "total": v[1]} for name, v in per_file.items()},
            "failures": [f"{r.case.source}: {r.case.name}" for r in results if not r.passed],
        }, indent=2))

    if args.reference:
        return 0 if passed == total else 1
    # A crash or a sanitizer report is a memory-safety bug in the engine, not
    # a missing feature, so the floor's slack does not apply to it.
    if sanitizer_hits:
        print(f"error: {len(sanitizer_hits)} case(s) produced a sanitizer report", file=sys.stderr)
        return 1
    if crashes:
        print(f"error: {crashes} case(s) crashed the engine", file=sys.stderr)
        return 1
    if args.min_pass is not None and passed < args.min_pass:
        print(f"error: {passed} cases pass, below the floor of {args.min_pass}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
