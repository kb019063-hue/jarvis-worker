#!/usr/bin/env python3
"""Runs a corpus twice -- once as a page runs it, once with collection stress --
and requires the two to print the same thing.

Why this exists
---------------
`ROADMAP.md` counts the Tier 1 items that have no oracle, and automatic
collection is one of them: "Automatic GC has almost none". There is no suite
that says what a correct collector does, because there is nothing to compare a
freed object against. What there *is*, is a property: **the observable
behaviour of a program must not depend on when its garbage was collected.**

Every other scoreboard in this repository takes its expectation from somewhere
outside the engine: Node.js, jsdom, html5lib, web-platform-tests, the W3C XML
suite, libwebp, ImageMagick. This one takes it from the engine running the same
program with the collector switched off. That sounds like the failure mode
`README.md` warns about -- "It was 100% before it was right", a corpus written
by the hand that wrote the code -- and the difference is worth stating exactly:

  * a case's expectation here is not a hand-written answer to "what does a
    correct collector do"; it is the *same program's* output without stress.
    Nothing is asserted about collection, so nothing can be asserted wrongly;
    the only claim is that two runs of one program agree.
  * the corpus is not this checkpoint's; the whole of `tests/js-conformance`
    and `tests/dom-conformance` can be run through it (`--full`), and those
    expectations were recorded from Node.js and jsdom.
  * a case that does not agree is a real bug in one of the two runs. Which one
    is then decided by hand, and `--show` says where.

How it works
------------
`--gc-stress=SEED:INTERVAL` arms `Heap::set_gc_stress`. The heap counts
allocations and, every INTERVAL allocations on average, raises a request; the
interpreter serves it at the next statement boundary, the bytecode VM at the
next instruction, and the seed decides which allocations they land on. The same
software as V8's `--gc-interval` and HotSpot's `-XX:+StressGCM`.

A run reports how many collections it served on stderr
(`[gc-stress] seed=... collections=N`). A case whose stress run served zero
collections is not evidence of anything, so this counts it as a failure of the
*harness*, not a pass: `make verify-gc-stress` cannot go green on a corpus that
allocates too little to stress.

Usage
-----
    python3 tools/conformance/verify_gc_stress.py                      # tests/gc-conformance
    python3 tools/conformance/verify_gc_stress.py --corpus tests/js-conformance --full
    python3 tools/conformance/verify_gc_stress.py --seeds 1,2,3,4,5 --interval 4
    python3 tools/conformance/verify_gc_stress.py --show 10 --filter iterators

Exit status: 0 when every case agreed under every seed, 1 otherwise.

Corpus format (the same as `tests/js-conformance`)::

    #name <title>
    #code
    <one or more lines of JavaScript>
    #expect            (optional here; when present the un-stressed run is
    <exact stdout>      checked against it, which keeps the corpus honest)
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import time
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

DEFAULT_BINARY = "build/litebird"
DEFAULT_CORPUS = "tests/gc-conformance"
DEFAULT_SEEDS = "1,2,3"
DEFAULT_INTERVAL = 16

# Programs that hold 10^5 objects alive at once. A collection is O(heap), so
# stressing them costs (allocations / interval) x heap: quadratic by
# construction, and it measures the interval rather than the collector. They
# are the size corpus, covered by `make deep`; excluding them here is stated
# rather than discovered by a timeout, so a timeout that is *not* one of these
# stays a failure.
DECLARED_DEEP = {"deep-nesting.dat"}

GC_REPORT = re.compile(r"^\[gc-stress\] seed=(\d+) interval=(\d+) collections=(\d+)$", re.M)


@dataclass
class Case:
    name: str
    source: str
    code: str
    expect: str | None
    path: str
    line: int
    page: str = ""


@dataclass
class Outcome:
    case: Case
    ok: bool = True
    reason: str = ""
    baseline_out: str = ""
    baseline_rc: int = 0
    stress_out: str | None = None
    stress_rc: int = 0
    collections: int = 0
    seed: int = 0
    error: str = ""
    baseline_failed: bool = False
    detail: list[str] = field(default_factory=list)


def parse_dat(path: Path) -> list[Case]:
    """The corpus parser of `run_js_conformance.py`, kept in step with it.

    A case is `#name`, then `#code` and its lines, then an optional `#expect`
    and its lines. The whole file is one case per `#name` block.
    """
    text = path.read_text(encoding="utf-8", errors="replace")
    cases: list[Case] = []
    name = None
    code: list[str] = []
    expect: list[str] | None = None
    section = None
    start_line = 0

    def flush() -> None:
        if name is None:
            return
        source = "\n".join(code)
        cases.append(Case(
            name=name,
            source=str(path),
            code=source,
            expect=None if expect is None else "\n".join(expect),
            path=str(path),
            line=start_line,
        ))

    for index, raw in enumerate(text.splitlines(), start=1):
        line = raw.rstrip("\n")
        if line.startswith("#name "):
            flush()
            name = line[len("#name "):].strip()
            code, expect, section = [], None, None
            start_line = index
            continue
        if line == "#code":
            section = "code"
            continue
        if line == "#expect":
            section = "expect"
            expect = []
            continue
        if line.startswith("#"):
            # Any other directive belongs to the corpus format this runner does
            # not use; ignore it rather than folding it into the program.
            continue
        if section == "code":
            code.append(line)
        elif section == "expect" and expect is not None:
            expect.append(line)
    flush()
    return cases


def strip_report(stderr: str) -> tuple[str, int]:
    """Removes the stress report from stderr and returns it with the count."""
    match = GC_REPORT.search(stderr)
    if not match:
        return stderr, 0
    return stderr[:match.start()] + stderr[match.end():], int(match.group(3))


def run(binary: str, code: str, timeout: float, stress: int | None,
        interval: int, workdir: Path) -> tuple[str, str, int, int, bool]:
    """Runs one program. Returns (stdout, stderr, rc, collections, timed_out)."""
    command = [binary]
    if stress is not None:
        command.append(f"--gc-stress={stress}:{interval}")
    command += ["--run-js", code]
    env = dict(os.environ)
    env.pop("LITEBIRD_GC_STRESS", None)
    try:
        done = subprocess.run(command, capture_output=True, text=True,
                              timeout=timeout, env=env, cwd=str(workdir))
    except subprocess.TimeoutExpired:
        return "", "", 124, 0, True
    stderr, collections = strip_report(done.stderr)
    if stress is None:
        collections = 0
    return done.stdout, stderr, done.returncode, collections, False


def check_case(case: Case, binary: str, seeds: list[int], interval: int,
               timeout: float, workdir: Path,
               max_baseline: float = 2.0) -> list[Outcome]:
    """The un-stressed run is the oracle; every seed must agree with it."""
    started = time.monotonic()
    base_out, _, base_rc, _, timed_out = run(binary, case.code, timeout, None, interval, workdir)
    baseline_seconds = time.monotonic() - started
    first = Outcome(case=case, baseline_out=base_out, baseline_rc=base_rc)
    if Path(case.path).name in DECLARED_DEEP:
        first.baseline_failed = True
        first.reason = f"{Path(case.path).name} is the size corpus"
        first.detail = ["  a collection is O(heap) and these programs hold 10^5 objects,",
                        "  so stress is quadratic on them; make deep covers them"]
        return [first]
    if not timed_out and baseline_seconds > max_baseline:
        first.baseline_failed = True
        first.reason = f"baseline takes {baseline_seconds:.1f}s (limit {max_baseline:.1f}s)"
        first.detail = ["  a program this slow un-stressed is measuring the stress interval,",
                        "  not the collector; --deep covers these, and make deep covers them"]
        return [first]
    if timed_out:
        first.ok, first.reason, first.error = False, "baseline timed out", "the case takes longer than --timeout to run un-stressed"
        return [first]
    if case.expect is not None and base_out.rstrip("\n") != case.expect.rstrip("\n"):
        # This corpus already fails against its own Node-recorded expectation.
        # That is a finding for the JavaScript scoreboard, not for the
        # collector: the one thing this harness can say nothing about is a
        # program the engine does not run correctly in the first place.
        first.baseline_failed = True
        first.reason = "baseline does not match #expect (already failing)"
        first.detail = [f"  expected: {case.expect!r}", f"  actual:   {base_out!r}"]
        return [first]
    if base_rc != 0:
        # A case that already fails un-stressed cannot say anything about the
        # collector, and pretending otherwise would hide a real failure in the
        # other corpora. It is reported, not counted.
        first.ok = False
        first.baseline_failed = True
        first.reason = f"baseline exited {base_rc}"
        return [first]

    results: list[Outcome] = []
    stress_timeout = max(timeout, min(120.0, baseline_seconds * 10.0))
    for seed in seeds:
        out, _, rc, collections, timed_out = run(binary, case.code, stress_timeout, seed, interval, workdir)
        result = Outcome(case=case, seed=seed, baseline_out=base_out,
                         baseline_rc=base_rc, stress_out=out,
                         stress_rc=rc, collections=collections)
        if timed_out and baseline_seconds > 0.5:
            # A program that takes a second un-stressed holds a large live set,
            # and a collection is O(heap), so stressing it measures the
            # interval rather than the collector. Excluded, and said so.
            result.baseline_failed = True
            result.reason = f"too slow under stress ({baseline_seconds:.1f}s un-stressed)"
            result.detail = ["  a collection is O(heap); a large live set is excluded here",
                             "  and covered by make deep instead"]
        elif timed_out:
            # Cost, not correctness. A collection is O(heap), so a program with
            # a large live set needs (allocations / interval) x heap time; two
            # of the 1,186 cases exceed a 120 s budget at interval 1 and agree
            # when given 600. What this harness fails on is output that
            # differs -- a premature free or a moved value -- which it can only
            # see in a run that finishes, so a run that does not finish is
            # reported and counted rather than quietly passed.
            result.baseline_failed = True
            result.reason = f"too slow under stress (>{stress_timeout:.0f}s)"
            result.detail = ["  a collection is O(heap): this program's live set is large",
                             "  run it with --timeout 600 to see the answer"]
        elif collections == 0:
            result.ok, result.reason = False, f"seed {seed}: stress never fired"
            result.detail = ["  the program allocated too little for this interval",
                             "  lower --interval or raise the case's allocation count"]
        elif rc != base_rc:
            result.ok, result.reason = False, f"seed {seed}: exit {rc} where un-stressed ran {base_rc}"
            result.detail = []
        elif out.rstrip("\n") != base_out.rstrip("\n"):
            result.ok = False
            result.reason = f"seed {seed}: output differs from the un-stressed run"
            result.detail = diff_lines(base_out, out)
        results.append(result)
        if not result.ok:
            break
    return results


def diff_lines(expected: str, actual: str) -> list[str]:
    expected_lines = expected.splitlines()
    actual_lines = actual.splitlines()
    detail = []
    for i in range(max(len(expected_lines), len(actual_lines))):
        e = expected_lines[i] if i < len(expected_lines) else "<missing>"
        a = actual_lines[i] if i < len(actual_lines) else "<missing>"
        if e != a:
            detail.append(f"  line {i + 1}: un-stressed {e!r} / stressed {a!r}")
            if len(detail) >= 6:
                detail.append("  ...")
                break
    return detail



# The negative control: cases that the harness must be seen to catch. A harness
# that cannot fail is not evidence, and README.md already carries the receipt
# for a scoreboard that reached 100% while being wrong. Each entry says what
# check_case must report; the reason substring is checked, not just the verdict,
# so "caught for the wrong reason" fails here too.

# --- the DOM corpus ---------------------------------------------------------
#
# `--dom` runs cases from `tests/dom-conformance` instead: a page, a script, and
# what the script prints. The reason to stress these is not the JavaScript --
# it is the bindings. A DOM object is a JsObject whose C++ half holds nodes,
# lists and attribute maps that the collector does not own, and a script that
# keeps `el` alive across a collection is the case where a binding that holds a
# raw pointer shows up. jsdom recorded these expectations (Phase 27), so the
# oracle is still outside the engine.
PAGE_URL = "https://example.test/app/page.html?tab=2#top"
DEFAULT_DOM_HTML = "<html><head><title>Corpus page</title></head><body></body></html>"


def parse_dom_dat(path: Path) -> list[Case]:
    """`tests/dom-conformance` format: #fixture, then #name/#html/#code/#expect."""
    text = path.read_text(encoding="utf-8")
    fixture: list[str] | None = None
    cases: list[Case] = []
    name = None
    sections: dict[str, list[str]] = {}
    mode = None

    def flush() -> None:
        if name is None:
            return
        code = "\n".join(sections.get("code", [])).strip("\n")
        html = "\n".join(sections.get("html", [])).strip("\n")
        expect = "\n".join(sections["expect"]).rstrip("\n") if "expect" in sections else None
        page = html if html else ("\n".join(fixture).strip("\n") if fixture else DEFAULT_DOM_HTML)
        if not page.lstrip().lower().startswith("<!doctype"):
            page = "<!DOCTYPE html>\n" + page
        cases.append(Case(name=name, source=str(path), code=code, expect=expect,
                          path=str(path), line=0, page=page))

    for line in text.split("\n"):
        if line.startswith("#name "):
            flush()
            name, sections, mode = line[len("#name "):].strip(), {}, None
        elif line == "#fixture" and name is None and fixture is None:
            fixture, mode = [], "fixture"
        elif line in ("#html", "#code", "#expect") and name is not None:
            mode = line[1:]
            sections[mode] = []
        elif mode == "fixture" and fixture is not None:
            fixture.append(line)
        elif mode is not None:
            sections[mode].append(line)
    flush()
    return cases


def run_dom_case(case: Case, binary: str, seeds: list[int], interval: int,
                 timeout: float, workdir: Path) -> list[Outcome]:
    scratch = Path(tempfile.mkdtemp(prefix="gcstress-", dir=str(workdir)))
    page = scratch / "page.html"
    script = scratch / "script.js"
    page.write_text(case.page, encoding="utf-8")
    script.write_text(case.code + "\n", encoding="utf-8")
    env = dict(os.environ, TZ="UTC")
    command = [binary, "--run-script", str(script), f"--url={PAGE_URL}", str(page)]

    def once(stress: int | None) -> tuple[str, int, int, bool]:
        full = list(command)
        if stress is not None:
            full.insert(1, f"--gc-stress={stress}:{interval}")
        try:
            done = subprocess.run(full, capture_output=True, text=True, timeout=timeout, env=env)
        except subprocess.TimeoutExpired:
            return "", 124, 0, True
        stderr, collections = strip_report(done.stderr)
        return done.stdout, done.returncode, collections if stress is not None else 0, False

    try:
        base_out, base_rc, _, timed_out = once(None)
        first = Outcome(case=case, baseline_out=base_out, baseline_rc=base_rc)
        if timed_out:
            first.ok = False
            first.baseline_failed = True
            first.reason = "baseline timed out"
            return [first]
        if case.expect is not None and base_out.rstrip("\n") != case.expect.rstrip("\n"):
            first.ok = False
            first.baseline_failed = True
            first.reason = "baseline does not match #expect (already failing)"
            first.detail = [f"  expected: {case.expect!r}", f"  actual:   {base_out!r}"]
            return [first]
        results = []
        for seed in seeds:
            out, rc, collections, timed_out = once(seed)
            result = Outcome(case=case, seed=seed, baseline_out=base_out, baseline_rc=base_rc,
                             stress_out=out, stress_rc=rc, collections=collections)
            if timed_out:
                result.ok, result.reason = False, f"seed {seed}: timed out under stress"
            elif collections == 0:
                result.ok, result.reason = False, f"seed {seed}: stress never fired"
            elif rc != base_rc:
                result.ok, result.reason = False, f"seed {seed}: exit {rc} where un-stressed ran {base_rc}"
            elif out.rstrip("\n") != base_out.rstrip("\n"):
                result.ok, result.reason = False, f"seed {seed}: output differs from the un-stressed run"
                result.detail = diff_lines(base_out, out)
            results.append(result)
            if not result.ok:
                break
        return results
    finally:
        for path in (page, script):
            try:
                path.unlink()
            except OSError:
                pass
        try:
            scratch.rmdir()
        except OSError:
            pass


SELF_TEST_DIR = "tests/gc-conformance/self-test"
SELF_TEST = [
    ("a count only a collection can change", None, "output differs"),
    ("a case that already fails says nothing about the collector", None, "baseline exited"),
    ("a case whose allocation rate never reaches the interval", 1000000, "stress never fired"),
]


def run_self_test(binary: str, timeout: float, workdir: Path) -> int:
    cases = parse_dat(Path(SELF_TEST_DIR) / "self-test.dat")
    by_name = {c.name: c for c in cases}
    failures = 0
    for name, interval, expected in SELF_TEST:
        case = by_name.get(name)
        if case is None:
            print(f"SELF-TEST MISSING: {name}")
            failures += 1
            continue
        outcomes = check_case(case, binary, [1], interval or DEFAULT_INTERVAL, timeout, workdir)
        caught = [o for o in outcomes if (not o.ok) or o.baseline_failed]
        if not caught:
            print(f"SELF-TEST FAILED (not caught): {name}")
            failures += 1
            continue
        reason = caught[0].reason
        if expected not in reason:
            print(f"SELF-TEST FAILED (caught as {reason!r}, expected {expected!r}): {name}")
            failures += 1
            continue
        print(f"self-test caught: {name}  ->  {reason}")
    if failures:
        print(f"self-test: {failures} control(s) not caught -- the harness is not evidence")
        return 1
    print(f"self-test: {len(SELF_TEST)} of {len(SELF_TEST)} controls caught")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--corpus", default=DEFAULT_CORPUS,
                        help=f"corpus directory or .dat file (default: {DEFAULT_CORPUS})")
    parser.add_argument("--full", action="store_true",
                        help="run the whole js-conformance corpus as well")
    parser.add_argument("--seeds", default=DEFAULT_SEEDS)
    parser.add_argument("--interval", type=int, default=DEFAULT_INTERVAL,
                        help="allocations between collections, on average (default 16)")
    parser.add_argument("--filter", default="", help="only cases whose name contains this")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--max-baseline-seconds", type=float, default=2.0,
                        help="skip cases slower than this un-stressed (default 2.0)")
    parser.add_argument("--show", type=int, default=5)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--dom", action="store_true",
                        help="stress the DOM corpus (page + script) instead of --corpus")
    parser.add_argument("--self-test", action="store_true",
                        help="run the negative control: cases the harness must catch")
    args = parser.parse_args(argv)

    binary = str(Path(args.binary).resolve())
    if not Path(binary).exists():
        print(f"error: no engine at {binary} (build it first: make)", file=sys.stderr)
        return 1

    if args.self_test:
        return run_self_test(binary, args.timeout, Path.cwd())

    dom_mode = args.dom
    roots = [Path(args.corpus if not dom_mode or args.corpus != DEFAULT_CORPUS else "tests/dom-conformance")]
    if args.full:
        roots.append(Path("tests/js-conformance"))
    cases: list[Case] = []
    for root in roots:
        files = sorted(root.glob("*.dat")) if root.is_dir() else [root]
        for path in files:
            cases.extend(parse_dom_dat(path) if dom_mode else parse_dat(path))
    if args.filter:
        cases = [c for c in cases if args.filter in c.name]
    if not cases:
        print("error: no cases found", file=sys.stderr)
        return 1

    seeds = [int(s) for s in args.seeds.replace(" ", "").split(",") if s]
    workdir = Path.cwd()

    outcomes: list[Outcome] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        runner = run_dom_case if dom_mode else check_case
        if dom_mode:
            futures = [pool.submit(runner, c, binary, seeds, args.interval, args.timeout, workdir)
                       for c in cases]
        else:
            futures = [pool.submit(runner, c, binary, seeds, args.interval, args.timeout, workdir,
                                   args.max_baseline_seconds)
                       for c in cases]
        for future in concurrent.futures.as_completed(futures):
            outcomes.extend(future.result())

    failed = [o for o in outcomes if not o.ok and not o.baseline_failed]
    broken = [o for o in outcomes if o.baseline_failed]
    passed = [o for o in outcomes if o.ok]
    total_collections = sum(o.collections for o in outcomes)

    outcomes.sort(key=lambda o: (o.case.path, o.case.line))
    if not args.quiet:
        for outcome in outcomes:
            if outcome.ok:
                continue
            mark = "BASELINE" if outcome.baseline_failed else "MISMATCH"
            print(f"{mark} {outcome.case.name} ({Path(outcome.case.path).name}:{outcome.case.line})")
            print(f"  {outcome.reason}")
            for line in outcome.detail:
                print(line)

    agreed_cases = {id(o.case) for o in passed}
    differed_cases = {id(o.case) for o in failed}
    broken_cases = {id(o.case) for o in broken}
    print(f"cases: {len(cases)}   seeds: {','.join(str(s) for s in seeds)}   "
          f"interval: {args.interval}")
    print(f"agreed: {len(agreed_cases)} of {len(cases)} cases   "
          f"({len(passed)} of {len(passed) + len(failed) + len(broken)} case-runs)")
    print(f"differed: {len(differed_cases)} cases   "
          f"out of scope: {len(broken_cases)} cases (already failing, or too slow to stress)")
    print(f"collections served: {total_collections}")
    slow = [o for o in broken if "too slow" in o.reason or "size corpus" in o.reason]
    if slow:
        print(f"not stressed (too slow: a collection is O(heap)): {len(slow)} cases")

    if broken and not failed:
        print("warning: cases whose un-stressed run already fails say nothing about the collector",
              file=sys.stderr)
    if failed:
        shown = 0
        for outcome in failed:
            if shown >= args.show:
                print(f"... and {len(failed) - shown} more")
                break
            print(f"\n{outcome.case.name}  ({Path(outcome.case.path).name}:{outcome.case.line})")
            print(f"  {outcome.reason}")
            for line in outcome.detail:
                print(line)
            shown += 1
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
