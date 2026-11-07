#!/usr/bin/env python3
"""Runs the html5lib/WPT tree-construction corpus against Litebird's parser.

This is the project's conformance scoreboard. Every case feeds its `#data`
through `litebird --dump-dom-webplatform` and compares the emitted tree dump
with the expected `#document` section, byte for byte.

Usage:
    python3 tools/conformance/run_tree_construction.py [options]

Options:
    --binary PATH      Litebird binary (default: build/litebird)
    --corpus PATH      Corpus directory (default: tests/html5lib/tree-construction)
    --filter SUBSTR    Only run .dat files whose name contains SUBSTR
    --show N           Print up to N failing diffs (default: 0)
    --jobs N           Parallel parser processes (default: os.cpu_count())
    --quiet            Only print the summary line
    --json PATH        Write a machine-readable summary to PATH
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

DEFAULT_BINARY = "build/litebird"
DEFAULT_CORPUS = "tests/html5lib/tree-construction"


@dataclass
class Case:
    source: str
    index: int
    data: str
    document: str
    fragment_context: str | None = None
    scripting: bool | None = None


@dataclass
class Result:
    case: Case
    actual: str
    passed: bool
    returncode: int = 0
    stderr: str = ""


def parse_dat(path: Path) -> list[Case]:
    """Splits a .dat file into cases, per the corpus README."""
    text = path.read_text(encoding="utf-8", errors="surrogateescape")
    cases: list[Case] = []

    for index, block in enumerate(text.split("\n\n")):
        if "#data" not in block:
            continue
        lines = block.split("\n")
        sections: dict[str, list[str]] = {}
        current = None
        for line in lines:
            if line in ("#data", "#errors", "#new-errors", "#document",
                        "#document-fragment", "#script-on", "#script-off"):
                current = line
                sections.setdefault(current, [])
                continue
            if current is not None:
                sections[current].append(line)

        data_lines = sections.get("#data", [])
        if not data_lines:
            continue
        data = "\n".join(data_lines)
        document_lines = sections.get("#document", [])

        context = None
        if "#document-fragment" in sections:
            fragment_lines = [line for line in sections["#document-fragment"] if line.strip()]
            if fragment_lines:
                context = fragment_lines[0]

        scripting = None
        if "#script-off" in sections:
            scripting = False
        elif "#script-on" in sections:
            scripting = True

        cases.append(Case(
            source=path.name,
            index=index,
            data=data,
            document="\n".join(document_lines) + ("\n" if document_lines else ""),
            fragment_context=context,
            scripting=scripting,
        ))
    return cases


def run_case(binary: str, case: Case) -> Result:
    expected = case.document
    attempts = [case.scripting] if case.scripting is not None else [True, False]

    first: Result | None = None
    for scripting in attempts:
        with tempfile.NamedTemporaryFile("w", suffix=".html", delete=False,
                                         encoding="utf-8", errors="surrogateescape") as handle:
            handle.write(case.data)
            tmp_path = handle.name
        try:
            command = [binary, tmp_path, "--dump-dom-webplatform"]
            if case.fragment_context:
                command.append(f"--fragment-context={case.fragment_context}")
            if not scripting:
                command.append("--scripting=off")
            try:
                completed = subprocess.run(command, capture_output=True, timeout=20)
            except subprocess.TimeoutExpired:
                completed = subprocess.CompletedProcess(command, returncode=-1, stdout=b"", stderr=b"timeout")
            # The corpus contains bytes that are not valid UTF-8; compare raw bytes.
            completed.stdout = completed.stdout.decode("utf-8", "surrogateescape")
            completed.stderr = completed.stderr.decode("utf-8", "surrogateescape")
        finally:
            os.unlink(tmp_path)

        passed = completed.returncode == 0 and completed.stdout == expected
        result = Result(case=case, actual=completed.stdout, passed=passed,
                        returncode=completed.returncode, stderr=completed.stderr[:400])
        if first is None:
            first = result
        if passed:
            return result
    return first  # type: ignore[return-value]


def diff_block(result: Result, limit: int = 24) -> str:
    expected = result.case.document.splitlines()
    actual = result.actual.splitlines()
    out = [f"  case {result.case.source} #{result.case.index}",
           f"  data: {result.case.data!r}"]
    if result.case.fragment_context:
        out.append(f"  context: {result.case.fragment_context}")
    if result.returncode != 0:
        out.append(f"  exit: {result.returncode} {result.stderr}")
    import difflib
    lines = list(difflib.unified_diff(expected, actual, fromfile="expected",
                                      tofile="actual", lineterm=""))
    out.extend("  " + line for line in lines[:limit])
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--corpus", default=DEFAULT_CORPUS)
    parser.add_argument("--filter", default=None)
    parser.add_argument("--show", type=int, default=0)
    parser.add_argument("--jobs", type=int, default=min(16, os.cpu_count() or 4))
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--json", default=None)
    args = parser.parse_args()

    corpus = Path(args.corpus)
    if not corpus.is_dir():
        print(f"error: corpus directory not found: {corpus}", file=sys.stderr)
        return 2
    if not Path(args.binary).exists():
        print(f"error: binary not found: {args.binary} (run `make` first)", file=sys.stderr)
        return 2

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
        results = list(pool.map(lambda c: run_case(args.binary, c), cases))

    passed = sum(1 for r in results if r.passed)
    total = len(results)
    rate = 100.0 * passed / total

    per_file: dict[str, list[int]] = {}
    for result in results:
        entry = per_file.setdefault(result.case.source, [0, 0])
        entry[1] += 1
        if result.passed:
            entry[0] += 1

    if not args.quiet:
        print("Per-file results (passed/total):")
        for name in sorted(per_file, key=lambda n: per_file[n][0] / per_file[n][1]):
            ok, count = per_file[name]
            flag = " " if ok == count else "!"
            print(f" {flag} {name:<38} {ok:>4}/{count:<4} {100.0 * ok / count:5.1f}%")

        failures = [r for r in results if not r.passed]
        if args.show:
            print(f"\nFirst {min(args.show, len(failures))} failures:")
            for result in failures[:args.show]:
                print(diff_block(result))
                print()

    marker = "PASS" if passed == total else "FAIL"
    print(f"\nDOM tree-construction conformance: {passed}/{total} ({rate:.1f}%)  [{marker} on {len(files)} files]")

    if args.json:
        Path(args.json).write_text(json.dumps({
            "passed": passed,
            "total": total,
            "rate": rate,
            "per_file": {name: {"passed": v[0], "total": v[1]} for name, v in per_file.items()},
        }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
