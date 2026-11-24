#!/usr/bin/env python3
"""Carry a pull request whose only conflict is in the table "Open tasks".

    git merge --no-ff --no-commit origin/prN      # stops: conflict in AGENTS.md
    python3 tools/watch/carry_rows.py origin/prN  # resolves AGENTS.md, or refuses
    git add AGENTS.md && git commit

(AGENTS.md, "Working together", "Carried".) What the pull request did to the
table is read from its own diff against the commit it left `main` at, and is
done again to the table as it is here:

- a row it rewrote is rewritten here, found by its title (the title it had
  before, or the one it has now);
- a row it added is put in directly under the row it stood under there, as
  an empty row and then the row;
- empty rows and blank lines it added or took out are left out: where the
  empty rows stand is the landing session's (`tools/ci/check_rows.py`).

Anything else it did to AGENTS.md (text outside the table, a row taken out,
a row that cannot be found here) is not for this tool: it says so and
changes nothing. A conflict in any other file is never the landing
session's to resolve.

    python3 tools/watch/carry_rows.py --check ONTO origin/prN

asks only, with no merge begun and nothing written: could the conflict of
origin/prN with the commit ONTO be carried? It answers in one line and
exits 0 for yes. `watch.sh plan` asks it for every pull request that
conflicts in AGENTS.md alone, so that what to do with one (carry it, or
send it back with the one row named) is read and not worked out by hand.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools" / "ci"))
import check_rows as rows  # noqa: E402


def git(*args: str) -> str:
    done = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    if done.returncode != 0:
        sys.exit(f"git {' '.join(args)} failed: {done.stderr.strip()[:200]}")
    return done.stdout


def changes(base: list[str], theirs: list[str], diff: str) -> tuple[list[tuple[str, str]], list[tuple[str | None, str]]]:
    """(rows rewritten as (old, new), rows added as (the row above it there, the row)).
    Exits when the diff does anything else."""
    rewritten: list[tuple[str, str]] = []
    added: list[tuple[str | None, str]] = []
    span = rows.table_span(theirs)
    hunk: list[str] = []
    hunks: list[list[str]] = []
    for line in diff.splitlines():
        if line.startswith("@@"):
            hunk = [line]
            hunks.append(hunk)
        elif hunk and (line.startswith("+") or line.startswith("-")) and not line.startswith(("+++", "---")):
            hunk.append(line)
    for hunk in hunks:
        start = int(hunk[0].split("+")[1].split(",")[0].split(" ")[0])     # first line of the hunk in theirs, 1-based
        minus = [l[1:] for l in hunk[1:] if l.startswith("-") and l[1:].strip() and not rows.is_empty_row(l[1:])]
        plus = [l[1:] for l in hunk[1:] if l.startswith("+") and l[1:].strip() and not rows.is_empty_row(l[1:])]
        if not minus and not plus:
            continue                                    # empty rows and blank lines only
        if any(not l.startswith("|") for l in minus + plus):
            sys.exit(f"it changes text outside the table (near line {start}): not a conflict to carry")
        if span is None or not (span[0] < start <= span[1] + 1):
            sys.exit(f"it changes a table other than \"Open tasks\" (near line {start}): not a conflict to carry")
        if len(minus) > len(plus):
            sys.exit(f"it takes a row out ({(rows.title_of(minus[0]) or minus[0][:40])}): not a conflict to carry")
        for old, new in zip(minus, plus):
            rewritten.append((old, new))
        for new in plus[len(minus):]:
            at = theirs.index(new)
            above = next((theirs[i] for i in range(at - 1, span[0] - 1, -1)
                          if not rows.is_empty_row(theirs[i]) and theirs[i].strip()), None)
            added.append((above, new))
    return rewritten, added


def find(lines: list[str], row: str | None, also: str | None = None) -> int | None:
    """Where a row is here: the same line, or the row with the same title."""
    if row is None:
        return None
    if row in lines:
        return lines.index(row)
    span = rows.table_span(lines)
    titles = {rows.title_of(r) for r in (row, also) if r and rows.title_of(r)}
    for i in range(*span):
        if rows.title_of(lines[i]) in titles:
            return i
    return None


def carried(ours: list[str], rewritten: list[tuple[str, str]], added: list[tuple[str | None, str]]) -> list[str]:
    out = list(ours)
    for old, new in rewritten:
        at = find(out, old, new)
        if at is None:
            sys.exit(f"the row it rewrote is not here: {rows.title_of(old) or old[:50]}")
        out[at] = new
    for above, new in added:
        if new in out:
            continue
        at = find(out, above)
        if at is None:
            sys.exit(f"the row it put a new row under is not here: {(rows.title_of(above) if above else None) or 'none'}")
        out[at + 1:at + 1] = [rows.EMPTY_ROW, new]
    return out


def self_test() -> int:
    head = ["# x", "", rows.HEADING, "", "| Task | Files | Described in | Note |", "| :--- | :--- | :--- | :--- |"]
    a, b, c = "| **A long first task.** x | f | d | n |", "| **The second task of three** | f | d | n |", "| **The third of them** | f | d | n |"
    e = rows.EMPTY_ROW
    base = head + [a, e, b, e, c]
    b2 = "| ~~**The second task of three**~~ done | f | d | n |"
    new = "| **What the second task found** | f | d | n |"
    ours = head + [a.replace(" x ", " y "), e, b, e, c]          # main rewrote the first row since
    checks = [
        ("a rewritten row is read from the diff",
         changes(base, head + [a, e, b2, e, c], "@@ -9 +9 @@\n-" + b + "\n+" + b2), ([(b, b2)], [])),
        ("an added row, and the row it stands under",
         changes(base, head + [a, e, b, e, new, e, c], "@@ -10,0 +11,2 @@\n+" + new + "\n+" + e), ([], [(b, new)])),
        ("empty rows and blank lines alone are nothing",
         changes(base, head + [a, e, e, b, e, c], "@@ -7,0 +8 @@\n+" + e), ([], [])),
        ("a rewritten row is rewritten here, beside a row main rewrote",
         carried(ours, [(b, b2)], []), head + [a.replace(" x ", " y "), e, b2, e, c]),
        ("found by its title when main changed its words too",
         carried(head + [a, e, b.replace("| f |", "| g |"), e, c], [(b, b2)], []), head + [a, e, b2, e, c]),
        ("an added row goes under the row it stood under, as an empty row and the row",
         carried(ours, [], [(b, new)]), head + [a.replace(" x ", " y "), e, b, e, new, e, c]),
        ("a row that is here already is not put in twice",
         carried(head + [a, e, b, e, new, e, c], [], [(b, new)]), head + [a, e, b, e, new, e, c]),
    ]
    failed = 0
    for what, got, want in checks:
        if got != want:
            failed += 1
            print(f"  FAIL  {what}: got {got!r}, expected {want!r}")
    print(f"carry_rows --self-test: {len(checks) - failed} of {len(checks)} passed")
    return 1 if failed else 0


def main(arguments: list[str]) -> int:
    if arguments == ["--self-test"]:
        return self_test()
    check = arguments[:1] == ["--check"]
    if len(arguments) != (3 if check else 1):
        sys.exit(__doc__)
    onto, ref = (arguments[1], arguments[2]) if check else ("HEAD", arguments[0])
    base = git("merge-base", onto, ref).strip()
    old = git("show", f"{base}:{rows.DOCUMENT}").split("\n")
    theirs = git("show", f"{ref}:{rows.DOCUMENT}").split("\n")
    rewritten, added = changes(old, theirs, git("diff", "--unified=0", base, ref, "--", rows.DOCUMENT))
    ours = git("show", f"{onto}:{rows.DOCUMENT}").split("\n")
    out = carried(ours, rewritten, added)
    if check:
        titles = [(rows.title_of(new) or new[2:50])[:60] for _, new in rewritten] + \
                 [(rows.title_of(new) or new[2:50])[:60] for _, new in added]
        print(f"rows only, can be carried: {len(rewritten)} rewritten, {len(added)} put in ({'; '.join(titles)})")
        return 0
    (ROOT / rows.DOCUMENT).write_text("\n".join(out), encoding="utf-8")
    for old_row, new in rewritten:
        print(f"rewritten here: {(rows.title_of(new) or new[:50])[:70]}")
    for above, new in added:
        print(f"put in: {(rows.title_of(new) or new[:50])[:60]}  (under: {((rows.title_of(above) if above else None) or '-')[:40]})")
    print(f"{rows.DOCUMENT}: this branch's table, with {len(rewritten)} row(s) of {ref} rewritten and {len(added)} put in")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
