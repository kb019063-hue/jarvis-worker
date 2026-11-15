#!/usr/bin/env python3
"""Run directories of test262 in one binary, or in two to see what changed.

    python3 tools/conformance/run_test262.py
    python3 tools/conformance/run_test262.py built-ins/Object language/expressions/super
    python3 tools/conformance/run_test262.py --binary before=/tmp/litebird-main --binary now=build/litebird

test262 (https://github.com/tc39/test262) is the conformance suite of the
language's standard. It is not a scoreboard here: Litebird passes about two
thirds of the directories below and a little over half of `built-ins`,
`language` and `annexB` together, and whole features the suite tests do not
exist (BigInt, Proxy, typed arrays, modules).

What this is for is the difference between two builds. `tests/js-conformance`
was written alongside the engine; these tests were written by nobody who has
seen it, so a change to how functions, classes or properties work can be
checked against them: with two --binary arguments the run lists every test
that passes in the first and not in the second, and exits 1 if there is one.

The suite is downloaded once from GitHub, at a pinned commit, and unpacked
under build/test262/. Nothing of it is vendored.

How a test is run:
  - its harness files (`includes:`, plus assert.js and sta.js) and the test
    go to `--run-js` as one script;
  - in strict mode when it is flagged `onlyStrict`, as written otherwise (a
    test that could run both ways is run once);
  - `module` tests and tests that need a harness file that does not exist
    are skipped; `raw` tests get no harness;
  - a test with a U+0000 in its source (seven in the suite, none in the
    default directories) cannot be one argument and is counted as `skip`;
  - a negative test passes when the run fails and names the expected error;
  - an async test passes when it prints Test262:AsyncTestComplete;
  - `$262` (other realms, agents, detached buffers) is not provided, so
    tests that need it fail.

Options:
    --binary NAME=PATH   A build to run. Repeat for a second one. Default:
                         litebird=build/litebird.
    --suite DIR          An unpacked test262 (the directory that has `test/`
                         and `harness/`). Default: downloaded.
    --timeout SECONDS    Per test and binary. Default 20.
    --jobs N             Tests run at once. Default: the number of cores.
    --show N             List up to N failures of the last binary.
    --json PATH          Write every result.
    --min-pass N         Exit 1 if the last binary passes fewer than N.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# The commit of tc39/test262 the numbers in the documents were measured at.
COMMIT = "2e58f87d8350ad51224617a041c6e041d52f8444"
ARCHIVE_URL = "https://codeload.github.com/tc39/test262/tar.gz/" + COMMIT

# What a change to functions, classes, prototypes or property definition can
# move (ARCHITECTURE.md, Phase 34). 15,686 tests at the commit above.
DEFAULT_DIRECTORIES = [
    "built-ins/Object", "built-ins/Reflect", "built-ins/Function", "built-ins/Error",
    "built-ins/NativeErrors", "built-ins/Symbol",
    "language/statements/class", "language/expressions/class", "language/expressions/super",
    "language/expressions/new.target", "language/expressions/instanceof", "language/expressions/new",
    "language/statements/function", "language/expressions/function", "language/expressions/object",
    "language/expressions/assignment", "language/expressions/call", "language/expressions/delete",
    "language/expressions/in", "language/expressions/typeof", "language/expressions/property-accessors",
]

PRELUDE = "if (typeof print === 'undefined') { var print = function () { console.log.apply(console, arguments); }; }\n"


def fetch_suite() -> Path:
    """The unpacked suite, downloaded if it is not there yet."""
    target = ROOT / "build" / "test262" / ("test262-" + COMMIT)
    if (target / "harness" / "assert.js").exists():
        return target
    sys.stderr.write("downloading test262 at %s ...\n" % COMMIT[:12])
    try:
        with urllib.request.urlopen(ARCHIVE_URL, timeout=120) as reply:
            data = reply.read()
    except OSError as error:
        sys.stderr.write("test262 could not be downloaded: %s\nGet %s, unpack it and pass --suite DIR.\n"
                         % (error, ARCHIVE_URL))
        sys.exit(2)
    target.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        # Only the tests and their harness; the archive's one top directory
        # is named after the commit.
        members = [m for m in archive.getmembers()
                   if "/test/" in m.name or "/harness/" in m.name]
        for member in members:
            parts = Path(member.name).parts
            if ".." in parts or member.name.startswith("/") or not (member.isfile() or member.isdir()):
                continue
            member.name = str(Path(*parts[1:]))
            archive.extract(member, target)
    return target


def front_matter(source: str) -> dict:
    """The fields of a test's `/*--- ... ---*/` block that decide how it is
    run. The block is YAML; these four are all that is read from it, and they
    are written in two forms only: `[a, b]` on the line, or `- a` below it."""
    found = re.search(r"/\*---(.*?)---\*/", source, re.S)
    meta = {"flags": [], "includes": [], "features": [], "negative": None}
    if not found:
        return meta
    lines = found.group(1).split("\n")
    index = 0
    while index < len(lines):
        line = lines[index]
        key = re.match(r"^(flags|includes|features):\s*(.*)$", line)
        if key:
            name, rest = key.group(1), key.group(2).strip()
            if rest.startswith("["):
                while "]" not in rest and index + 1 < len(lines):      # a list broken over lines
                    index += 1
                    rest += " " + lines[index].strip()
                meta[name] = [item.strip() for item in rest.strip("[]").split(",") if item.strip()]
            else:
                items = []
                while index + 1 < len(lines) and re.match(r"^\s*-\s+", lines[index + 1]):
                    index += 1
                    items.append(re.sub(r"^\s*-\s+", "", lines[index]).strip())
                meta[name] = items
        elif re.match(r"^negative:", line):
            negative = {}
            while index + 1 < len(lines) and re.match(r"^\s+\S", lines[index + 1]):
                index += 1
                field = re.match(r"^\s+(\w+):\s*(\S+)", lines[index])
                if field:
                    negative[field.group(1)] = field.group(2)
            meta["negative"] = negative
        index += 1
    return meta


class Suite:
    def __init__(self, root: Path):
        self.root = root
        self.harness_cache: dict[str, str] = {}

    def harness(self, name: str) -> str:
        if name not in self.harness_cache:
            self.harness_cache[name] = (self.root / "harness" / name).read_text(encoding="utf-8")
        return self.harness_cache[name]

    def script(self, source: str, meta: dict) -> str | None:
        """The whole text handed to --run-js, or None for a test to skip."""
        flags = meta["flags"]
        if "module" in flags:
            return None
        if "raw" in flags:
            return source
        parts = [PRELUDE, self.harness("assert.js"), self.harness("sta.js")]
        if "async" in flags:
            parts.append(self.harness("doneprintHandle.js"))
        for name in meta["includes"]:
            try:
                parts.append(self.harness(name))
            except OSError:
                return None
        code = "\n".join(parts) + "\n" + source
        if "onlyStrict" in flags:
            code = '"use strict";\n' + code
        return code


def run_one(binary: str, code: str, meta: dict, timeout: float) -> tuple[str, str]:
    """('pass' | 'fail' | 'crash' | 'timeout' | 'skip', why)."""
    try:
        done = subprocess.run([binary, "--run-js", code], capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "timeout", ""
    except (OSError, ValueError) as error:
        # A script that cannot be one argument: too long, or with a U+0000 in
        # it (a few tests have one in their source).
        return "skip", str(error)
    out = done.stdout.decode("utf-8", "replace")
    err = done.stderr.decode("utf-8", "replace")
    if done.returncode < 0 or done.returncode > 128:
        return "crash", "exit %d %s" % (done.returncode, err.strip()[-200:])
    negative = meta["negative"]
    if negative:
        wanted = negative.get("type", "")
        if done.returncode != 0 and wanted in err + out:
            return "pass", ""
        return "fail", "expected %s; exit %d %s" % (wanted, done.returncode, (err or out).strip()[-200:])
    if done.returncode != 0:
        return "fail", (err or out).strip()[-300:]
    if "Test262:AsyncTestFailure" in out:
        return "fail", out.strip()[-200:]
    if "async" in meta["flags"] and "Test262:AsyncTestComplete" not in out:
        return "fail", "the async test did not complete: " + (out + err).strip()[-200:]
    return "pass", ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", action="append", metavar="NAME=PATH")
    parser.add_argument("--suite")
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--show", type=int, default=0)
    parser.add_argument("--json")
    parser.add_argument("--min-pass", type=int, default=0)
    parser.add_argument("directories", nargs="*")
    args = parser.parse_args()

    binaries = []
    for spec in args.binary or ["litebird=" + str(ROOT / "build" / "litebird")]:
        name, _, path = spec.partition("=")
        if not path:
            name, path = Path(spec).name, spec
        if not Path(path).exists():
            sys.stderr.write("error: %s does not exist\n" % path)
            return 2
        binaries.append((name, str(Path(path).resolve())))
    if len({name for name, _ in binaries}) != len(binaries):
        sys.stderr.write("error: two binaries have the same name\n")
        return 2

    suite = Suite(Path(args.suite).resolve() if args.suite else fetch_suite())
    tests_root = suite.root / "test"
    files: list[Path] = []
    for directory in args.directories or DEFAULT_DIRECTORIES:
        base = tests_root / directory
        if not base.exists():
            sys.stderr.write("error: %s is not in the suite\n" % directory)
            return 2
        found = [base] if base.is_file() else base.rglob("*.js")
        files.extend(path for path in found if "_FIXTURE" not in path.name)
    files = sorted(set(files))

    def work(path: Path):
        source = path.read_text(encoding="utf-8", errors="surrogateescape")
        meta = front_matter(source)
        code = suite.script(source, meta)
        name = str(path.relative_to(tests_root))
        if code is None:
            return name, None
        return name, {label: run_one(binary, code, meta, args.timeout) for label, binary in binaries}

    results: dict[str, dict] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for name, outcome in pool.map(work, files):
            if outcome is not None:
                results[name] = outcome

    print("test262 at %s: %d tests run, %d skipped (modules)" % (COMMIT[:12], len(results), len(files) - len(results)))
    passed = {}
    for label, _ in binaries:
        counts: dict[str, int] = {}
        for outcome in results.values():
            counts[outcome[label][0]] = counts.get(outcome[label][0], 0) + 1
        passed[label] = counts.get("pass", 0)
        others = "  ".join("%s %d" % item for item in sorted(counts.items()) if item[0] != "pass")
        print("  %-10s %d pass (%.1f%%)  [%s]" % (label, passed[label], 100.0 * passed[label] / max(1, len(results)), others))

    last = binaries[-1][0]
    if args.show:
        shown = 0
        for name in sorted(results):
            kind, why = results[name][last]
            if kind != "pass" and shown < args.show:
                shown += 1
                print("  %s: %s %s" % (name, kind, why.replace("\n", " ")[:160]))

    status = 0
    if len(binaries) >= 2:
        first = binaries[0][0]
        lost = sorted(name for name, outcome in results.items()
                      if outcome[first][0] == "pass" and outcome[last][0] != "pass")
        gained = sum(1 for outcome in results.values() if outcome[first][0] != "pass" and outcome[last][0] == "pass")
        print("%d pass in %s and not in %s; %d pass in %s and not in %s" % (gained, last, first, len(lost), first, last))
        for name in lost:
            kind, why = results[name][last]
            print("  LOST %s: %s %s" % (name, kind, why.replace("\n", " ")[:200]))
        if lost:
            status = 1

    if args.json:
        Path(args.json).parent.mkdir(parents=True, exist_ok=True)
        Path(args.json).write_text(json.dumps({"commit": COMMIT, "results": results}, indent=1))
    if args.min_pass and passed[last] < args.min_pass:
        sys.stderr.write("error: %s passes %d, fewer than --min-pass %d\n" % (last, passed[last], args.min_pass))
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
