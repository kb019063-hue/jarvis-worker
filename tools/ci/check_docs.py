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

ROOT = Path(__file__).resolve().parent.parent.parent

# Ceilings in bytes. Lowered as sections move to docs/, never raised to let a
# pull request pass — the same rule as a scoreboard floor in CI.
BUDGETS: dict[str, int] = {
    "ARCHITECTURE.md": 512_000,   # 408 KB today
    "AUDIT.md": 350_000,          # 282 KB today
}

# A heading that, added, means a write-up went into the wrong place.
DRIFT: dict[str, re.Pattern[str]] = {
    "ARCHITECTURE.md": re.compile(r"^##\s+Phase\s+\d+\b", re.IGNORECASE),
    "AUDIT.md": re.compile(r"^##\s+\d+\."),
}


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


def check_size() -> tuple[int, list[str]]:
    failures = 0
    for name, budget in BUDGETS.items():
        path = ROOT / name
        if not path.is_file():
            print(f"  ok    {name}: absent")
            continue
        size = path.stat().st_size
        if size > budget:
            failures += 1
            print(f"  FAIL  {name}: {size:,} bytes is over the "
                  f"{budget:,}-byte budget by {size - budget:,}")
            print(f"        new write-ups go in docs/ (docs/README.md)")
        else:
            print(f"  ok    {name}: {size:,} / {budget:,} bytes "
                  f"({budget - size:,} left)")
    return failures, list(BUDGETS)


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
    return failures


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--base", help="the ref to diff against "
                                   "(default: $GITHUB_BASE_REF, else origin/main)")
    args = ap.parse_args()

    print("check_docs: the shape of the documents")
    size_failures = check_size()[0]
    base, why_not = resolve_base(args.base)
    drift_failures = check_drift(base, why_not)

    total = size_failures + drift_failures
    print(f"\ncheck_docs: {total} failure(s)")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
