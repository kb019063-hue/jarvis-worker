#!/usr/bin/env python3
"""Checks the shape of ARCHITECTURE.md and AUDIT.md.

Why this exists
---------------
`AGENTS.md` asks an agent to add a block of its own and not re-wrap what is
there. That rule keeps two agents from colliding, and it also means nothing is
ever deleted, reordered or moved: `ARCHITECTURE.md` reached 408 KB, 84% of it
phase write-ups, and `AUDIT.md` 282 KB with 53 of its 61 findings resolved. A
rule written in prose does not stop that. A check that fails does.

Three things are checked.

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

kept
    `AGENTS.md` has what the base has: the rows of "Open tasks" and the lines
    of the rules (MAY_LOSE_ROWS, MAY_LOSE_LINES, below). A pull request that
    wrote the file from an old or a cut copy fails here; nothing else read
    the file for what was no longer in it.

    python3 tools/ci/check_docs.py
    python3 tools/ci/check_docs.py --base origin/main
    python3 tools/ci/check_docs.py --base origin/main --kept TREE
                                   only "kept", for the AGENTS.md of a tree
                                   or a commit (the landing session asks it
                                   of `git merge-tree --write-tree`, before
                                   a pull request goes into a lot)

It exits 0 when every check passes and 1 when any fails.
"""

from __future__ import annotations

import argparse
import collections
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


# What a pull request may take out of AGENTS.md before this fails. A pull
# request rewrites its own row of "Open tasks" (one line out, one in) and now
# and then a paragraph it is about; nobody takes a row out, a finished task's
# row is struck through and stays.
#
# On 26 November 2026 #452 came back, after it had been sent back for 109
# lines of the same, with a copy of the file from the afternoon before, cut
# off after line 1442, on top of that morning's `main`: 206 lines were not in
# it, 58 of the 86 rows of the table and six passages the rules had gained
# since. It merged cleanly. Every check that read the file said ok:
# an empty row stood between the 28 rows that were left. The `#include` lines
# of its suite file were all that kept its run from passing (with them out,
# every step of "Documentation shape" passed on that tree), and a pull
# request whose run has passed is merged as it is.
#
# If lines are meant to go (a section rewritten, the table tidied), that is a
# pull request of its own, and it changes these two numbers for itself, with
# the reason here, and the next one puts them back.
MAY_LOSE_ROWS = 2
MAY_LOSE_LINES = 40


def rows_of(lines: list[str]) -> list[str]:
    """The rows of "Open tasks", the empty ones left out."""
    lines, _ = check_rows.mended(lines)
    span = check_rows.table_span(lines)
    if span is None:
        return []
    return [line for line in lines[span[0]:span[1]] if not check_rows.is_empty_row(line)]


def lost(base_text: str, here_text: str) -> list[str]:
    """What an AGENTS.md has lost of the one on the base, as the lines to
    print under FAIL; none when it has kept it. A line counts as kept when it
    stands anywhere in the file, so a passage that moved has lost nothing."""
    base, here = base_text.split("\n"), here_text.split("\n")
    problems: list[str] = []
    rows_base, rows_here = rows_of(base), rows_of(here)
    if len(rows_here) < len(rows_base) - MAY_LOSE_ROWS:
        titles = {check_rows.title_of(row) for row in rows_here}
        gone = [check_rows.title_of(row) or row[2:60] for row in rows_base if check_rows.title_of(row) not in titles]
        which = f': "{gone[0][:60]}"' + (f" and {len(gone) - 1} more" if len(gone) > 1 else "") if gone else ""
        problems.append(f"\"Open tasks\" has {len(rows_base)} rows on the base and {len(rows_here)} here{which}")
    there = collections.Counter(here)
    gone_lines = []
    for line in base:
        if line.startswith("|") or not line.strip():
            continue  # the table is counted in rows; a blank line is nobody's
        if there[line] > 0:
            there[line] -= 1
        else:
            gone_lines.append(line)
    if len(gone_lines) > MAY_LOSE_LINES:
        problems.append(f"{len(gone_lines)} lines of the base outside the table are not here, the first: "
                        f"\"{gone_lines[0].strip()[:70]}\"")
    return problems


def check_kept(base: str | None, tree: str | None = None) -> int:
    """AGENTS.md, in the working tree or in `tree`, has what the base has."""
    name = check_rows.DOCUMENT
    if base is None:
        return 0  # check_drift has said why nothing is compared
    against, which = base, base
    if tree is None:
        # A branch that is behind the base has not lost what the base got
        # since it left: the merge brings that. It is held to the base as it
        # left it. (In CI the working tree is the merge with the base, and in
        # a clone without the history `git merge-base` fails: the base itself.)
        fork = _git("merge-base", "HEAD", base).stdout.strip()
        tip = _git("rev-parse", "--verify", "--quiet", f"{base}^{{commit}}").stdout.strip()
        if fork and tip and fork != tip:
            against, which = fork, f"{base} as this branch left it ({fork[:7]})"
    old = _git("show", f"{against}:{name}")
    new = _git("show", f"{tree}:{name}") if tree else None
    if old.returncode != 0 or (new is not None and new.returncode != 0):
        print(f"  skip  {name}: it could not be read at {against if old.returncode != 0 else tree}")
        return 0
    here = new.stdout if new is not None else (ROOT / name).read_text(encoding="utf-8")
    problems = lost(old.stdout, here)
    if not problems:
        print(f"  ok    {name}: it has the rows and the lines of {which}")
        return 0
    print(f"  FAIL  {name}: " + problems[0])
    for problem in problems[1:]:
        print(f"        {problem}")
    short = against if against == base else against[:7]
    print(f"        (the base is {which}.) A pull request changes its own row and takes nothing out. This is\n"
          f"        what a copy of the file from before those lines does, or one that a tool cut short, when it\n"
          f"        is written over the base's: `git checkout {short} -- {name}`, then make your own change\n"
          f"        again as one line (a patch or `sed`, not the whole file). `git diff {short} --stat -- {name}`\n"
          f"        then shows one or two lines.")
    return 1


def own_cases() -> int:
    """`lost` on fixed texts: how many of its cases fail. Run with every call;
    it reads nothing but this file."""
    def document(rows: int, rules: int) -> str:
        table = "\n| | | | |\n".join(f"| **Task {n}** is open | `a.cpp` | | |" for n in range(rows))
        return ("# Rules\n\n" + "\n".join(f"A line of the rules, the {n}th." for n in range(rules))
                + "\n\n## Open tasks\n\n| Task | Files | Described in | Note |\n| --- | --- | --- | --- |\n" + table + "\n")
    whole = document(30, 100)
    struck = whole.replace("| **Task 7** is open", "| ~~**Task 7**~~ **Fixed in Phase 9**")
    cases = [
        ("the same file", lost(whole, whole), []),
        ("a row rewritten, a row added, a paragraph added", lost(whole, document(31, 110).replace("**Task 7** is open", "**Task 7** is done")), []),
        ("a finished task's row is struck through and stays", lost(whole, struck), []),
        ("two rows gone: not asked about", len(lost(whole, document(28, 100))), 0),
        ("the table cut off: asked about, and the first row that is gone is named",
         lost(whole, document(12, 100)), ['"Open tasks" has 30 rows on the base and 12 here: "Task 12" and 17 more']),
        ("forty lines of the rules gone: not asked about", len(lost(whole, document(30, 60))), 0),
        ("a copy from before 45 lines of the rules: asked about",
         lost(whole, document(30, 55)), ['45 lines of the base outside the table are not here, the first: "A line of the rules, the 55th."']),
        ("both at once: two lines", len(lost(whole, document(12, 55))), 2),
        ("a passage that moved has lost nothing",
         lost(whole, whole.replace("A line of the rules, the 3th.\n", "") + "\nA line of the rules, the 3th.\n"), []),
    ]
    failed = 0
    for name, got, wanted in cases:
        if got != wanted:
            failed += 1
            print(f"  FAIL  check_docs.py's own case \"{name}\": wanted {wanted!r}, got {got!r}")
    if not failed:
        print(f"  ok    check_docs.py's own cases ({len(cases)})")
    return failed


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
    ap.add_argument("--kept", metavar="TREE", help="only whether the AGENTS.md of this tree or commit "
                                                   "has what the base has")
    args = ap.parse_args()

    base, why_not = resolve_base(args.base)
    if args.kept:
        if base is None:
            print(f"  skip  {why_not}")
            return 0
        return 1 if check_kept(base, args.kept) else 0

    print("check_docs: the shape of the documents")
    own_failures = own_cases()
    size_failures = check_size(base)
    drift_failures = check_drift(base, why_not)
    kept_failures = check_kept(base)

    total = own_failures + size_failures + drift_failures + kept_failures
    print(f"\ncheck_docs: {total} failure(s)")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
