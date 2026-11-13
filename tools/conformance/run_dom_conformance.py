#!/usr/bin/env python3
"""Runs the DOM corpus: page scripts, with the output a reference DOM gives.

This is the scoreboard for the DOM a script sees, the counterpart of
run_js_conformance.py (the language) and run_tree_construction.py (the
parser). Every case is a page and a script. The expected output was recorded
from jsdom, an independent implementation of the DOM standards, never from
Litebird. A case passes when

    litebird --run-script script.js --url=<PAGE_URL> page.html

prints exactly that output and neither crashes, runs out of time, nor prints
a sanitizer report. An uncaught exception is not a failure by itself: some
cases are about what a page sees after one. A case that fails for that reason
fails because its output is cut short.

Usage:
    python3 tools/conformance/run_dom_conformance.py [options]

Options:
    --binary PATH      Litebird binary (default: build/litebird)
    --corpus PATH      Corpus directory (default: tests/dom-conformance)
    --filter SUBSTR    Only run .dat files whose name contains SUBSTR
    --case SUBSTR      Only run cases whose name contains SUBSTR
    --show N           Print up to N failing cases with expected/actual output
    --jobs N           Parallel processes (default: os.cpu_count())
    --timeout SECS     Per-case timeout (default: 10)
    --quiet            Only print the summary lines
    --json PATH        Write a machine-readable summary to PATH
    --min-pass N       Exit 1 if fewer than N cases pass (the CI floor)
    --reference        Run the corpus in jsdom instead and compare with the
                       recorded expectations. Exits 1 on any difference. This
                       is how the expectations themselves are kept honest.
    --record           Run the corpus in jsdom and write what it prints into
                       the #expect sections. The only way an expectation is
                       ever written.
    --jsdom-dir PATH   Where jsdom is installed (default: build/jsdom). It is
                       installed there from the npm registry when missing.

Corpus format (see tests/dom-conformance/README.md):
    #fixture               (optional, once, before the first case)
    <the page of every case that has no #html>
    #name <title>
    #html                  (optional)
    <the page>
    #code
    <JavaScript>
    #expect
    <exact stdout>
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

DEFAULT_BINARY = "build/litebird"
DEFAULT_CORPUS = "tests/dom-conformance"
DEFAULT_JSDOM_DIR = "build/jsdom"
# The reference. Expectations are only comparable when everybody records with
# the same version, so it is pinned.
JSDOM_VERSION = "31.0.0"
# The address every page is loaded from, in both engines.
PAGE_URL = "https://example.test/app/page.html?tab=2#top"
# The page of a case that has neither #html nor a #fixture.
DEFAULT_HTML = "<html><head><title>Corpus page</title></head><body></body></html>"
# The fixed probe the headline number is reported on.
HEADLINE_FILE = "everyday.dat"
# jsdom runs a page until it has no timer left; this is how long it may take,
# in real milliseconds, before the case is refused. Litebird runs timers on a
# virtual clock and needs no such number.
MAX_PAGE_MS = 3000
RUNNER = Path(__file__).resolve().parent / "jsdom_run.mjs"


@dataclass
class Case:
    source: str
    name: str
    code: str
    expect: str | None
    html: str | None = None          # the case's own page, if it has one
    fixture: str | None = None       # the file's shared page, if it has one

    @property
    def page(self) -> str:
        """The page as both engines get it: always with a doctype."""
        html = self.html if self.html is not None else (
            self.fixture if self.fixture is not None else DEFAULT_HTML)
        if not html.lstrip().lower().startswith("<!doctype"):
            html = "<!DOCTYPE html>\n" + html
        return html


@dataclass
class Result:
    case: Case
    actual: str
    passed: bool
    returncode: int = 0
    stderr: str = ""
    timed_out: bool = False
    sanitizer: bool = False
    error: str | None = None         # reference runs: why the case is unusable


@dataclass
class DatFile:
    path: Path
    fixture: str | None = None
    cases: list[Case] = field(default_factory=list)


# What AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer print.
# UBSan reports and then carries on, so a case can print the right answer
# while the engine did something undefined; the runner has to read stderr.
SANITIZER_REPORT = re.compile(
    r"ERROR: (Address|Leak|Thread|Memory|UndefinedBehavior)Sanitizer"
    r"|WARNING: ThreadSanitizer"
    r"|: runtime error: ")


def parse_dat(path: Path) -> DatFile:
    """Splits a .dat file into its fixture and its cases.

    A case begins at a `#name ` line; `#html`, `#code` and `#expect` switch
    sections. Blank lines inside a section belong to it; blank lines at the
    end of a section are dropped, so cases can be separated by an empty line.
    """
    dat = DatFile(path)
    name: str | None = None
    sections: dict[str, list[str]] = {}
    mode: str | None = None
    fixture_lines: list[str] | None = None

    def flush() -> None:
        if name is None:
            return
        if "code" not in sections:
            raise ValueError(f"{path.name}: case {name!r} needs #code")
        code = "\n".join(sections["code"]).strip("\n")
        expect = "\n".join(sections["expect"]).rstrip("\n") if "expect" in sections else None
        html = "\n".join(sections["html"]).strip("\n") if "html" in sections else None
        dat.cases.append(Case(path.name, name, code, expect, html, dat.fixture))

    for line in path.read_text(encoding="utf-8").split("\n"):
        if line.startswith("#name "):
            flush()
            if fixture_lines is not None and dat.fixture is None:
                dat.fixture = "\n".join(fixture_lines).strip("\n")
            name, sections, mode = line[len("#name "):].strip(), {}, None
        elif line == "#fixture" and name is None and fixture_lines is None:
            fixture_lines = []
            mode = "fixture"
        elif line in ("#html", "#code", "#expect") and name is not None:
            mode = line[1:]
            sections[mode] = []
        elif mode == "fixture":
            fixture_lines.append(line)
        elif mode is not None:
            sections[mode].append(line)
    flush()
    names = [case.name for case in dat.cases]
    for duplicate in sorted({n for n in names if names.count(n) > 1}):
        raise ValueError(f"{path.name}: two cases are named {duplicate!r}")
    return dat


def write_dat(dat: DatFile) -> None:
    out: list[str] = []
    if dat.fixture is not None:
        out += ["#fixture", dat.fixture, ""]
    for case in dat.cases:
        out.append(f"#name {case.name}")
        if case.html is not None:
            out += ["#html", case.html]
        out += ["#code", case.code, "#expect"]
        if case.expect:
            out.append(case.expect)
        out.append("")
    dat.path.write_text("\n".join(out), encoding="utf-8")


def run_case(binary: str, case: Case, timeout: float, workdir: Path, index: int) -> Result:
    page = workdir / f"page-{index}.html"
    script = workdir / f"script-{index}.js"
    page.write_text(case.page, encoding="utf-8")
    script.write_text(case.code + "\n", encoding="utf-8")
    env = dict(os.environ, TZ="UTC")
    command = [binary, "--run-script", str(script), f"--url={PAGE_URL}", str(page)]
    try:
        completed = subprocess.run(command, capture_output=True, timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        return Result(case, "", False, returncode=-1, stderr="timeout", timed_out=True)
    actual = completed.stdout.decode("utf-8", "replace").rstrip("\n")
    stderr = completed.stderr.decode("utf-8", "replace").strip()
    sanitizer = bool(SANITIZER_REPORT.search(stderr))
    crashed = completed.returncode < 0
    passed = actual == case.expect and not sanitizer and not crashed
    return Result(case, actual, passed, completed.returncode, stderr, sanitizer=sanitizer)


def ensure_jsdom(jsdom_dir: Path) -> None:
    """Installs the pinned jsdom into `jsdom_dir` unless it is already there."""
    manifest = jsdom_dir / "node_modules" / "jsdom" / "package.json"
    if manifest.exists():
        try:
            if json.loads(manifest.read_text(encoding="utf-8")).get("version") == JSDOM_VERSION:
                return
        except ValueError:
            pass
    if not shutil.which("npm") or not shutil.which("node"):
        raise SystemExit("error: the reference needs Node.js and npm")
    jsdom_dir.mkdir(parents=True, exist_ok=True)
    if not (jsdom_dir / "package.json").exists():
        (jsdom_dir / "package.json").write_text('{"name": "litebird-dom-reference", "private": true}\n')
    print(f"installing jsdom {JSDOM_VERSION} into {jsdom_dir} ...", file=sys.stderr)
    subprocess.run(["npm", "install", "--no-audit", "--no-fund", "--silent", f"jsdom@{JSDOM_VERSION}"],
                   cwd=jsdom_dir, check=True)


def run_reference(cases: list[Case], jsdom_dir: Path, jobs: int) -> list[Result]:
    """Runs `cases` in jsdom, a few Node processes at a time."""
    ensure_jsdom(jsdom_dir)
    batch_size = 25
    batches = [cases[i:i + batch_size] for i in range(0, len(cases), batch_size)]

    def run_batch(batch: list[Case]) -> list[Result]:
        request = json.dumps({
            "url": PAGE_URL,
            "max_ms": MAX_PAGE_MS,
            "cases": [{"html": case.page, "code": case.code} for case in batch],
        })
        completed = subprocess.run(["node", str(RUNNER), str(jsdom_dir)], input=request.encode("utf-8"),
                                   capture_output=True, env=dict(os.environ, TZ="UTC"))
        if completed.returncode != 0:
            message = completed.stderr.decode("utf-8", "replace").strip()[-400:]
            return [Result(case, "", False, completed.returncode, message, error="the reference process failed")
                    for case in batch]
        answers = json.loads(completed.stdout.decode("utf-8"))
        results = []
        for case, answer in zip(batch, answers):
            actual = answer["output"].rstrip("\n")
            error = answer.get("error")
            results.append(Result(case, actual, error is None and actual == case.expect, error=error))
        return results

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        return [result for batch in pool.map(run_batch, batches) for result in batch]


def failure_block(result: Result) -> str:
    out = [f"  {result.case.source}: {result.case.name}"]
    if result.case.html is not None:
        for line in result.case.html.split("\n")[:8]:
            out.append(f"    < {line}")
    for line in result.case.code.split("\n")[:16]:
        out.append(f"    | {line}")
    out.append(f"    expected: {result.case.expect!r}")
    out.append(f"    actual:   {result.actual!r}")
    if result.error:
        out.append(f"    error:    {result.error}")
    if result.returncode < 0 and not result.timed_out:
        out.append(f"    signal:   {-result.returncode}")
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
    parser.add_argument("--case", default=None)
    parser.add_argument("--show", type=int, default=0)
    parser.add_argument("--jobs", type=int, default=min(16, os.cpu_count() or 4))
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--json", default=None)
    parser.add_argument("--min-pass", type=int, default=None)
    parser.add_argument("--reference", action="store_true")
    parser.add_argument("--record", action="store_true")
    parser.add_argument("--jsdom-dir", default=DEFAULT_JSDOM_DIR)
    args = parser.parse_args()

    corpus = Path(args.corpus)
    if not corpus.is_dir():
        print(f"error: corpus directory not found: {corpus}", file=sys.stderr)
        return 2

    paths = sorted(corpus.glob("*.dat"))
    if args.filter:
        paths = [p for p in paths if args.filter in p.name]
    try:
        files = [parse_dat(path) for path in paths]
    except ValueError as problem:
        print(f"error: {problem}", file=sys.stderr)
        return 2
    cases = [case for dat in files for case in dat.cases]
    if args.case:
        cases = [case for case in cases if args.case in case.name]
    if not cases:
        print("error: no test cases found", file=sys.stderr)
        return 2

    if args.record:
        if args.case:
            print("error: --record writes whole files; it cannot be combined with --case", file=sys.stderr)
            return 2
        results = run_reference(cases, Path(args.jsdom_dir), args.jobs)
        unusable = [r for r in results if r.error]
        changed = 0
        by_case = {id(r.case): r for r in results}
        for dat in files:
            for case in dat.cases:
                result = by_case[id(case)]
                if result.error:
                    continue
                if case.expect != result.actual:
                    changed += 1
                case.expect = result.actual
            write_dat(dat)
        print(f"recorded {len(results) - len(unusable)} of {len(results)} cases from jsdom {JSDOM_VERSION} "
              f"({changed} expectations written or changed)")
        for result in unusable:
            print(f"  not recorded: {result.case.source}: {result.case.name}: {result.error}")
        return 1 if unusable else 0

    missing = [case for case in cases if case.expect is None]
    if missing:
        print(f"error: {len(missing)} case(s) have no #expect (first: {missing[0].source}: "
              f"{missing[0].name}); run --record", file=sys.stderr)
        return 2

    if args.reference:
        label = f"jsdom {JSDOM_VERSION}"
        results = run_reference(cases, Path(args.jsdom_dir), args.jobs)
    else:
        if not Path(args.binary).exists():
            print(f"error: binary not found: {args.binary} (run `make` first)", file=sys.stderr)
            return 2
        label = "Litebird"
        with tempfile.TemporaryDirectory(prefix="litebird-dom-") as tmp:
            workdir = Path(tmp)
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
                results = list(pool.map(
                    lambda item: run_case(args.binary, item[1], args.timeout, workdir, item[0]),
                    enumerate(cases)))

    passed = sum(1 for r in results if r.passed)
    total = len(results)
    rate = 100.0 * passed / total

    per_file: dict[str, list[int]] = {}
    for result in results:
        entry = per_file.setdefault(result.case.source, [0, 0])
        entry[1] += 1
        if result.passed:
            entry[0] += 1

    headline = [r for r in results if r.case.source == HEADLINE_FILE]
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
    print(f"DOM conformance, full corpus: {passed}/{total} ({rate:.1f}%)  "
          f"[{len(per_file)} files, {crashes} crashes, {timeouts} timeouts]")
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
        if passed != total:
            print(f"error: {total - passed} expectation(s) are not what jsdom {JSDOM_VERSION} prints",
                  file=sys.stderr)
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
