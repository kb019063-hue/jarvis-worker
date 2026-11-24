#!/usr/bin/env python3
"""Checks the shape of ARCHITECTURE.md and AUDIT.md.

Why this exists
---------------
`AGENTS.md` asks an agent to add a block of its own and not re-wrap what is
there. That rule keeps two agents from colliding, and it also means nothing is
ever deleted, reordered or moved: `ARCHITECTURE.md` reached 408 KB, 84% of it
phase write-ups, and `AUDIT.md` 282 KB with 53 of its 61 findings resolved. A
rule written in prose does not stop that. A check that fails does.

Two things are checked.

size
    Neither document may grow past a budget. The budgets sit above what the
    files are today, so this passes now and fails on growth; they are meant to
    come down as the migration to `docs/` proceeds, not up.

drift
    No new `## Phase NN` heading may be added to `ARCHITECTURE.md`, and no new
    `## NN.` finding to `AUDIT.md`. New write-ups go in `docs/`
    (`docs/README.md` says where). The check compares a base ref with the
    working tree, so an uncommitted edit fails here as well as a committed one
    on a branch; it is skipped, with a note, when no base ref can be resolved
    — which is the case in a checkout that did not fetch one.

    python3 tools/ci/check_docs.py
    python3 tools/ci/check_docs.py --base origin/main

It exits 0 when every check passes and 1 when any fails.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_rows  # noqa: E402  (the rows of "Open tasks": tools/ci/check_rows.py)

ROOT = Path(__file__).resolve().parent.parent.parent

# Ceilings in bytes. Lowered as sections move to docs/, never raised to let a
# pull request pass — the same rule as a scoreboard floor in CI.
BUDGETS: dict[str, int] = {
    # Set after the migration moved 46 phase write-ups out of ARCHITECTURE.md
    # (435 KB -> 71 KB) and 56 resolved findings out of AUDIT.md (296 KB ->
    # 37 KB). The headroom now is for the documents describing *more*, not for
    # a log growing again; a write-up goes in docs/ instead.
    "ARCHITECTURE.md": 120_000,   # 71 KB today
    "AUDIT.md": 80_000,           # 37 KB today
}

# A heading that, added, means a write-up went into the wrong place.
DRIFT: dict[str, re.Pattern[str]] = {
    "ARCHITECTURE.md": re.compile(r"^##\s+Phase\s+\d+\b", re.IGNORECASE),
    "AUDIT.md": re.compile(r"^##\s+\d+\."),
}

# A line of the list "Resolved findings" at the end of AUDIT.md. The list has
# the findings that were moved out of that document and some that were added
# by hand since; every new line went to its end, so two pull requests that
# each added one were in conflict there (#254 and #281 against #285, on 23
# November 2026). The file in docs/findings/ is the entry; the list gets no
# more (AGENTS.md, "The files every pull request changes", point 3).
LISTED_FINDING = re.compile(r"^- \*\*Finding \d+\*\* — \[")


def _git(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args], cwd=ROOT, capture_output=True, text=True
    )


def resolve_base(explicit: str | None) -> tuple[str | None, str | None]:
    """The ref to diff against, or (None, why not)."""
    candidates: list[str] = []
    if explicit:
        candidates.append(explicit)
    base_ref = os.environ.get("GITHUB_BASE_REF")
    if base_ref:
        candidates.append(f"origin/{base_ref}")
    candidates.append("origin/main")
    for ref in candidates:
        if _git("rev-parse", "--verify", "--quiet", ref).returncode == 0:
            return ref, None
    return None, f"no base ref resolvable (tried: {', '.join(candidates)})"


def added_lines(base: str, path: str) -> list[str] | None:
    """Lines the working tree adds to a file over the base, or None if the diff
    could not be run.

    The base is compared with the working tree and not with HEAD, because a
    diff against HEAD sees only what is committed and would call an uncommitted
    edit clean. On a pull request the working tree is the merge commit, so what
    this sees is what the pull request changes.
    """
    out = _git("diff", "--unified=0", base, "--", path)
    if out.returncode != 0:
        return None
    return [
        line[1:]
        for line in out.stdout.splitlines()
        if line.startswith("+") and not line.startswith("+++")
    ]


def bytes_added(base: str | None, path: str) -> int | None:
    """How many bytes the working tree adds to a file over the base.

    None when the base cannot be resolved, which is what makes the size check
    lenient below: without this the budget would fail a pull request that only
    happens to land after the file passed it.
    """
    if base is None:
        return None
    out = _git("diff", "--numstat", base, "--", path)
    if out.returncode != 0:
        return None
    fields = out.stdout.split()
    if not fields or fields[0] == "-":
        return 0
    try:
        return int(fields[0])
    except ValueError:
        return None


def check_size(base: str | None) -> int:
    """The budget fails a pull request that grows the file past it, and only
    that pull request.

    An absolute size check would fail every later pull request once the file
    reached its budget, including ones that never touched it — which in a
    repository where several agents work at once means failing work that did
    nothing wrong. So: over budget and growing is a failure; over budget and
    not growing is reported, and left to whoever is moving sections out.
    """
    failures = 0
    for name, budget in BUDGETS.items():
        path = ROOT / name
        if not path.is_file():
            print(f"  ok    {name}: absent")
            continue
        size = path.stat().st_size
        if size <= budget:
            print(f"  ok    {name}: {size:,} / {budget:,} bytes "
                  f"({budget - size:,} left)")
            continue

        grew = bytes_added(base, name)
        over = size - budget
        if grew is None:
            print(f"  WARN  {name}: {size:,} bytes is over the "
                  f"{budget:,}-byte budget by {over:,}, and no base ref could "
                  f"be resolved to say whether this pull request grew it")
            continue
        if grew > 0:
            failures += 1
            print(f"  FAIL  {name}: {size:,} bytes is over the "
                  f"{budget:,}-byte budget by {over:,}, and this pull request "
                  f"adds {grew:,} lines to it")
            print(f"        new write-ups go in docs/ (docs/README.md)")
        else:
            print(f"  WARN  {name}: {size:,} bytes is over the "
                  f"{budget:,}-byte budget by {over:,}, but this pull request "
                  f"does not grow it — left for the migration")
    return failures


def check_drift(base: str | None, why_not: str | None) -> int:
    if base is None:
        print(f"  skip  new write-ups in the wrong place: {why_not}")
        return 0
    print(f"  diff  against {base}")
    failures = 0
    for name, pattern in DRIFT.items():
        lines = added_lines(base, name)
        if lines is None:
            print(f"  skip  {name}: the diff against {base} could not be run")
            continue
        bad = [line for line in lines if pattern.match(line)]
        if bad:
            failures += 1
            print(f"  FAIL  {name}: {len(bad)} new write-up heading(s) added")
            for line in bad[:5]:
                print(f"        + {line[:100]}")
            print(f"        a new write-up goes in "
                  f"{'docs/phases/' if name == 'ARCHITECTURE.md' else 'docs/findings/'}"
                  f" (docs/README.md)")
        else:
            print(f"  ok    {name}: no new write-up headings")
    # Two notes, and neither a failure: a pull request that is finished
    # would have to go round CI again for a line of an index or of a table.
    listed = [line for line in added_lines(base, "AUDIT.md") or [] if LISTED_FINDING.match(line)]
    if listed:
        print(f"  note  AUDIT.md: {len(listed)} new line(s) in the list \"Resolved findings\". "
              f"Add none: the file in docs/findings/ is the entry, and a line at the end of "
              f"that list is in conflict with every other pull request's "
              f"(AGENTS.md, \"The files every pull request changes\", point 3)")
    rows = check_rows.report((ROOT / "AGENTS.md").read_text(encoding="utf-8").split("\n"))
    if rows:
        print(rows)
    return failures


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--base", help="the ref to diff against "
                                   "(default: $GITHUB_BASE_REF, else origin/main)")
    args = ap.parse_args()

    print("check_docs: the shape of the documents")
    base, why_not = resolve_base(args.base)
    size_failures = check_size(base)
    drift_failures = check_drift(base, why_not)

    total = size_failures + drift_failures
    print(f"\ncheck_docs: {total} failure(s)")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
