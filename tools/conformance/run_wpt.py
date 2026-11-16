#!/usr/bin/env python3
"""Runs Web Platform Tests in Litebird, with the tests' own harness.

The files under tests/wpt/ are copied from web-platform-tests/wpt and laid
out as they are there: resources/testharness.js is the real harness, and each
test is an HTML page that includes it. Litebird's command line does not fetch
a page's external scripts, so every <script src> of a test is put into the
page inline before the page is run; nothing else of a test is changed. In
place of resources/testharnessreport.js the page gets a reporter that prints
one line for each subtest.

A subtest that fails is either listed in tests/wpt/expected-failures.txt with
the reason, or it is an error. A listed subtest that passes is an error too:
the list says what is wrong today, and has to be kept true.

The tests are checked against tests/wpt/MANIFEST.txt first, which has the Git
blob id of every file as upstream has it: a test that was edited, added or
removed stops the run.

A line of the expectations file is three fields separated by tabs: the test file, the
name of the subtest, and a word for the reason (tests/wpt/README.md says what
each word means). A name that starts with "glob:" is a pattern for many
subtests of the file (fnmatch, case-sensitive); every subtest it matches has
to fail, and it has to match one.

Usage:
    python3 tools/conformance/run_wpt.py [options]

    --binary PATH        Litebird binary (default: build/litebird)
    --suite DIR          the tests (default: tests/wpt)
    --expectations FILE  the failures that are known
                         (default: <suite>/expected-failures.txt)
    --filter TEXT        only the test files whose path contains TEXT
    --show N             print the first N failures that are not expected
    --list-failures      print every failing subtest in the form of the
                         expectations file, to copy from
    --jobs N             processes at a time (default: the CPU count)
    --timeout SECONDS    for one test file (default: 60)
    --min-pass N         fail unless at least N subtests pass
    --json FILE          write the results there
    --quiet              the summary only

Exit status: 0 when every failure is a listed one, no listed subtest passes
and the floor is met; 1 otherwise; 2 for a wrong invocation.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import fnmatch
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_BINARY = REPO / "build" / "litebird"
DEFAULT_SUITE = REPO / "tests" / "wpt"
ORIGIN = "https://web-platform.test"

# Printed by the page for each subtest and once for the harness. The fields
# are separated by tabs; the harness's own status names are kept.
REPORTER = r"""
(function () {
  var names = ['PASS', 'FAIL', 'TIMEOUT', 'NOTRUN', 'PRECONDITION_FAILED'];
  var harness = ['OK', 'ERROR', 'TIMEOUT', 'PRECONDITION_FAILED'];
  function one_line(text) {
    return String(text === null || text === undefined ? '' : text).replace(/\s+/g, ' ');
  }
  add_completion_callback(function (tests, status) {
    for (var i = 0; i < tests.length; i++) {
      console.log('WPT-SUBTEST\t' + names[tests[i].status] + '\t' + one_line(tests[i].name) +
                  '\t' + one_line(tests[i].message));
    }
    console.log('WPT-HARNESS\t' + harness[status.status] + '\t' + one_line(status.message));
  });
})();
"""

SCRIPT_SRC = re.compile(
    rb"""<script\b([^>]*?)\bsrc\s*=\s*("[^"]*"|'[^']*'|[^\s>]+)([^>]*?)(?:>\s*</script\s*>|/\s*>)""",
    re.IGNORECASE | re.DOTALL,
)

HARNESS_ROW = "(harness)"
CRASH_ROW = "(does not crash)"


@dataclass
class Subtest:
    name: str
    status: str
    message: str = ""


@dataclass
class FileResult:
    path: str
    subtests: list[Subtest] = field(default_factory=list)
    note: str = ""


def verify_copy(suite: Path) -> list[str]:
    """What differs between the files and MANIFEST.txt; empty when nothing does."""
    manifest = suite / "MANIFEST.txt"
    if not manifest.is_file():
        return ["there is no MANIFEST.txt"]
    listed: dict[str, str] = {}
    for line in manifest.read_text(encoding="utf-8").split("\n"):
        if line.strip() and not line.startswith("#"):
            blob, name = line.split(None, 1)
            listed[name] = blob
    ours = {"README.md", "expected-failures.txt", "MANIFEST.txt"}
    problems = []
    for name, blob in sorted(listed.items()):
        path = suite / name
        if not path.is_file():
            problems.append(f"missing: {name}")
            continue
        data = path.read_bytes()
        # The id Git gives a file: so the list can be compared with upstream.
        if hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest() != blob:
            problems.append(f"changed: {name}")
    for path in sorted(suite.rglob("*")):
        name = path.relative_to(suite).as_posix()
        if path.is_file() and name not in listed and name not in ours:
            problems.append(f"not in the manifest: {name}")
    return problems


def is_test_file(path: Path, suite: Path) -> bool:
    relative = path.relative_to(suite).parts
    if relative[0] in ("resources", "common"):
        return False
    if any(part in ("resources", "support") for part in relative[:-1]):
        return False
    return path.suffix in (".html", ".xhtml")


def inline_scripts(page: Path, suite: Path) -> tuple[bytes, bool]:
    """The page with each external script put inline; and whether it uses the harness."""
    raw = page.read_bytes()
    uses_harness = False
    is_xml = page.suffix == ".xhtml"

    def wrap_script_body(attrs_before: bytes, attrs_after: bytes, body: bytes) -> bytes:
        if is_xml:
            return b"<script" + attrs_before + attrs_after + b"><![CDATA[" + body.replace(b"]]>", b"]]]]><![CDATA[>") + b"]]></script>"
        body = re.sub(rb"</(script)", rb"<\\/\1", body, flags=re.IGNORECASE)
        return b"<script" + attrs_before + attrs_after + b">" + body + b"</script>"

    def replace(match: re.Match) -> bytes:
        nonlocal uses_harness
        source = match.group(2).decode("ascii", "ignore").strip("\"'")
        if source.endswith("/resources/testharnessreport.js"):
            return wrap_script_body(b"", b"", REPORTER.encode("utf-8"))
        if source.endswith("/resources/testharness.js"):
            uses_harness = True
        target = suite / source.lstrip("/") if source.startswith("/") else page.parent / source
        if not target.is_file():
            # Left as it is: the engine will not load it, and the test says so.
            return match.group(0)
        body = target.read_bytes()
        return wrap_script_body(match.group(1), match.group(3), body)

    return SCRIPT_SRC.sub(replace, raw), uses_harness


def sidecar_content_type(page: Path) -> str | None:
    sidecar = page.parent / (page.name + ".headers")
    if not sidecar.is_file():
        return None
    for line in sidecar.read_text(encoding="utf-8", errors="replace").splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            if k.strip().lower() == "content-type":
                return v.strip()
    return None


def run_file(binary: str, suite: Path, page: Path, workdir: Path, timeout: float) -> FileResult:
    relative = page.relative_to(suite).as_posix()
    result = FileResult(relative)
    raw, uses_harness = inline_scripts(page, suite)
    staged = workdir / (relative.replace("/", "__"))
    staged.write_bytes(raw)
    empty = workdir / "empty.js"
    command = [
        binary,
        "--run-script", str(empty),
        f"--map-origin={ORIGIN}={suite}",
        f"--url={ORIGIN}/{relative}",
    ]
    ct = sidecar_content_type(page)
    if ct:
        command.append(f"--content-type={ct}")
    command.append(str(staged))
    env = dict(os.environ, TZ="UTC")
    try:
        completed = subprocess.run(command, capture_output=True, timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        result.subtests.append(Subtest(HARNESS_ROW if uses_harness else CRASH_ROW, "TIMEOUT",
                                       f"no result in {timeout:g} s"))
        return result
    output = completed.stdout.decode("utf-8", "replace")
    if completed.returncode < 0:
        result.subtests.append(Subtest(HARNESS_ROW if uses_harness else CRASH_ROW, "CRASH",
                                       f"signal {-completed.returncode}"))
        return result
    if not uses_harness:
        result.subtests.append(Subtest(CRASH_ROW, "PASS"))
        return result
    harness = None
    for line in output.split("\n"):
        fields = line.split("\t")
        if fields[0] == "WPT-SUBTEST" and len(fields) >= 3:
            result.subtests.append(Subtest(fields[2], fields[1], fields[3] if len(fields) > 3 else ""))
        elif fields[0] == "WPT-HARNESS" and len(fields) >= 2:
            harness = (fields[1], fields[2] if len(fields) > 2 else "")
    if harness is None:
        stderr = completed.stderr.decode("utf-8", "replace").strip().split("\n")
        tail = (output.strip().split("\n")[-1:] + stderr[-1:])
        result.subtests.append(Subtest(HARNESS_ROW, "ERROR", "the harness reported nothing: " + " | ".join(tail)[:300]))
    elif harness[0] != "OK":
        result.subtests.append(Subtest(HARNESS_ROW, harness[0], harness[1]))
    elif not result.subtests:
        result.subtests.append(Subtest(HARNESS_ROW, "ERROR", "the page has no subtests"))
    return result


class Expectations:
    """The failures that are known: by name, and by pattern."""

    def __init__(self, path: Path):
        self.exact: dict[tuple[str, str], str] = {}
        self.patterns: list[tuple[str, str, str]] = []
        self.used: set[tuple[str, str]] = set()
        if not path.is_file():
            return
        for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
            if not line.strip() or line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) != 3:
                print(f"error: {path}:{number}: expected three fields separated by tabs", file=sys.stderr)
                sys.exit(2)
            if fields[1].startswith("glob:"):
                self.patterns.append((fields[0], fields[1][5:], fields[2]))
            else:
                self.exact[(fields[0], fields[1])] = fields[2]

    def reason(self, path: str, name: str) -> str | None:
        """Why this subtest is expected to fail; None when it is not."""
        if (path, name) in self.exact:
            self.used.add((path, name))
            return self.exact[(path, name)]
        for file, pattern, reason in self.patterns:
            if file == path and fnmatch.fnmatchcase(name, pattern):
                self.used.add((file, "glob:" + pattern))
                return reason
        return None

    def unused(self, only: str) -> list[tuple[str, str]]:
        """Lines that matched no subtest that was run."""
        lines = list(self.exact) + [(file, "glob:" + pattern) for file, pattern, _ in self.patterns]
        return [line for line in lines if line not in self.used and (not only or only in line[0])]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=str(DEFAULT_BINARY))
    parser.add_argument("--suite", default=str(DEFAULT_SUITE))
    parser.add_argument("--expectations")
    parser.add_argument("--filter", default="")
    parser.add_argument("--show", type=int, default=0)
    parser.add_argument("--list-failures", action="store_true")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--min-pass", type=int, default=0)
    parser.add_argument("--json")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    suite = Path(args.suite).resolve()
    if not (suite / "resources" / "testharness.js").is_file():
        print(f"error: no resources/testharness.js under {suite}", file=sys.stderr)
        return 2
    if not Path(args.binary).exists():
        print(f"error: binary not found: {args.binary} (run `make` first)", file=sys.stderr)
        return 2
    # The tests are upstream's, unchanged: a test edited until it passes
    # would say nothing.
    problems = verify_copy(suite)
    if problems:
        for problem in problems[:20]:
            print(f"error: {suite.name}: {problem}", file=sys.stderr)
        print("error: the tests are not the copy MANIFEST.txt describes", file=sys.stderr)
        return 2
    expectations_path = Path(args.expectations) if args.expectations else suite / "expected-failures.txt"
    expected = Expectations(expectations_path)

    pages = sorted(p for p in suite.rglob("*") if p.is_file() and is_test_file(p, suite))
    if args.filter:
        pages = [p for p in pages if args.filter in p.relative_to(suite).as_posix()]
    if not pages:
        print("error: no test files", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="litebird-wpt-") as tmp:
        workdir = Path(tmp)
        (workdir / "empty.js").write_text("", encoding="utf-8")
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            results = list(pool.map(lambda page: run_file(args.binary, suite, page, workdir, args.timeout), pages))

    passed = failed_expected = 0
    unexpected: list[tuple[str, Subtest]] = []
    unexpected_passes: list[tuple[str, str]] = []
    reasons: dict[tuple[str, str], str] = {}
    by_reason: dict[str, int] = {}
    rows = []
    for result in results:
        file_pass = 0
        for subtest in result.subtests:
            reason = expected.reason(result.path, subtest.name)
            if subtest.status == "PASS":
                passed += 1
                file_pass += 1
                if reason is not None:
                    unexpected_passes.append((result.path, subtest.name))
            elif reason is not None:
                failed_expected += 1
                reasons[(result.path, subtest.name)] = reason
                by_reason[reason] = by_reason.get(reason, 0) + 1
            else:
                unexpected.append((result.path, subtest))
        rows.append((result.path, file_pass, len(result.subtests)))
    # A line that matches no subtest is a stale line.
    stale = expected.unused(args.filter)
    total = sum(count for _, _, count in rows)

    if not args.quiet:
        width = max(len(path) for path, _, _ in rows)
        for path, file_pass, count in rows:
            mark = " " if file_pass == count else "*"
            print(f" {mark} {path:<{width}}  {file_pass:>4}/{count:<4}")
        print()

    if args.list_failures:
        for result in results:
            for subtest in result.subtests:
                if subtest.status != "PASS":
                    reason = reasons.get((result.path, subtest.name), "?")
                    print(f"{result.path}\t{subtest.name}\t{reason}")
                    if reason == "?":
                        print(f"#   {subtest.status}: {subtest.message[:300]}")
        print()

    if args.show and unexpected:
        print(f"First {min(args.show, len(unexpected))} failures that are not in {expectations_path.name}:")
        for path, subtest in unexpected[: args.show]:
            print(f"  {path}: {subtest.name}")
            print(f"    {subtest.status}: {subtest.message[:400]}")
        print()

    for path, name in unexpected_passes:
        print(f"listed as failing and passes (take it off the list): {path}: {name}")
    for path, name in stale:
        print(f"listed and not run (no such subtest): {path}: {name}")

    if by_reason and not args.quiet:
        print("Listed failures, by reason:")
        for reason, count in sorted(by_reason.items(), key=lambda item: (-item[1], item[0])):
            print(f"  {count:>5}  {reason}")
        print()
    print(f"Web Platform Tests: {passed}/{total} subtests pass in {len(rows)} files; "
          f"{failed_expected} fail as listed, {len(unexpected)} fail and are not listed")

    if args.json:
        Path(args.json).write_text(json.dumps({
            "passed": passed, "total": total, "files": len(rows),
            "expected_failures": failed_expected, "unexpected_failures": len(unexpected),
            "expected_failures_by_reason": by_reason,
            "results": [{"path": r.path, "subtests": [vars(s) for s in r.subtests]} for r in results],
        }, indent=1), encoding="utf-8")

    ok = not unexpected and not unexpected_passes and not stale
    if args.min_pass and passed < args.min_pass:
        print(f"FAIL: {passed} subtests pass, the floor is {args.min_pass}")
        ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
