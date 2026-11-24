#!/usr/bin/env python3
"""The rows of "Open tasks" in AGENTS.md, and the empty row between them.

Git merges two changes to one file only when a line that neither of them
changed stands between the two. A row of the table "Open tasks" is one line,
so two pull requests that each rewrote a row were in conflict whenever their
rows touched, and so were one that rewrote a row and one that added a row
under it. In the watch of the evening of 23 November that held up six of the
twenty-one pull requests that landed (#261, #281, #292, #293, #296, #298):
each was finished and green, and went round CI again, or waited for the end
of the watch, for one line of this table.

So an empty row stands between every two rows:

    | **A task** ... | files | described in | note |
    | | | | |
    | **The next task** ... | files | described in | note |

A pull request that rewrites its row changes one line, and the lines above
and below it are empty rows that nobody changes. A new row goes directly
under the row of the task it came from, as two lines, an empty row and then
the new row:

    | **A task** ... | files | described in | note |
    | | | | |                                          <- new
    | **What this task found** ... | ... | ... | ... |  <- new
    | | | | |
    | **The next task** ... | files | described in | note |

That place belongs to one pull request, as its task does, and the empty row
that was there still stands between the new row and the next task.

    check_rows.py                  rows that touch, as a note (exit 0)
    check_rows.py --write          put an empty row where two rows touch, but
                                   not beside a row that an open pull request
                                   holds: one it changed or put something
                                   next to, or one its `Task:` line names. An
                                   empty row put there would be a change that
                                   touches theirs, and would put that pull
                                   request in conflict once. (`--write --open`
                                   is the same.) The landing session runs
                                   this at the end of a watch; nobody else
                                   needs to. On 24 November a pull request
                                   that had put eleven empty rows in by hand
                                   was in conflict with two others for it.
    check_rows.py --write --except N[,N...]
                                   the same, but those pull requests hold
                                   nothing: the ones the landing session's
                                   branch has merged already, whose rows are
                                   in the tree it writes to
    check_rows.py --write --keep-clear-of REF [REF ...]
                                   the same, with the REFs in place of the
                                   open pull requests
    check_rows.py --self-test
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
DOCUMENT = "AGENTS.md"
HEADING = "## Open tasks"
EMPTY_ROW = "| | | | |"

_EMPTY = re.compile(r"^\|(\s*\|)+\s*$")
_DELIMITER = re.compile(r"^\|(\s*:?-{3,}:?\s*\|)+\s*$")
_HUNK = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+\d+(?:,\d+)? @@")
_TITLE = re.compile(r"^\|\s*(?:~~)?\*\*(.+?)\*\*")
_TASK = re.compile(r"(?m)^Task:\s*(.+)$")


def is_empty_row(line: str) -> bool:
    return bool(_EMPTY.match(line))


def table_span(lines: list[str]) -> tuple[int, int] | None:
    """The first row and the line after the last one of the table under the
    heading, the header and delimiter rows left out. None when there is none."""
    try:
        at = next(i for i, line in enumerate(lines) if line.strip() == HEADING)
    except StopIteration:
        return None
    i = at + 1
    while i < len(lines) and not lines[i].startswith("|"):
        if lines[i].startswith("## "):
            return None
        i += 1
    if i + 1 >= len(lines) or not _DELIMITER.match(lines[i + 1]):
        return None
    first = i + 2
    end = first
    while end < len(lines) and lines[end].startswith("|"):
        end += 1
    return first, end


def breaks(lines: list[str]) -> list[int]:
    """Indexes of the blank lines that cut the table in two: blank lines
    under the last row with a row under them. Markdown ends a table at a
    blank line, so the rows under one are shown as a paragraph of pipes, and
    no tool that reads the table sees them. (One had stood in the middle of
    this table, with 23 rows under it, until 24 November 2026. The same day
    two stood there together, with 29 rows under them, and this function,
    which looked for one blank line with a row right under it, saw neither
    and said nothing: it counted 43 rows where there were 72.)"""
    span = table_span(lines)
    found = []
    while span is not None:
        end = span[1]
        under = end
        while under < len(lines) and lines[under].strip() == "":
            under += 1
        if under > end and under < len(lines) and lines[under].startswith("|") \
                and not (under + 1 < len(lines) and _DELIMITER.match(lines[under + 1])):
            found.extend(range(end + len(found), under + len(found)))
            lines = lines[:end] + lines[under:]
            span = table_span(lines)
        else:
            break
    return found


def mended(lines: list[str]) -> tuple[list[str], int]:
    """The lines without the blank lines that cut the table, and how many."""
    cut = breaks(lines)
    return [line for i, line in enumerate(lines) if i not in set(cut)], len(cut)


def touching(lines: list[str]) -> list[int]:
    """Indexes of the rows that have another row directly under them. The
    last row of the table has nothing under it and is never one of them."""
    span = table_span(lines)
    if span is None:
        return []
    first, end = span
    return [
        i for i in range(first, end - 1)
        if not is_empty_row(lines[i]) and not is_empty_row(lines[i + 1])
    ]


def separated(lines: list[str], keep_clear: set[int] | None = None) -> tuple[list[str], int, int]:
    """The lines with an empty row between every two rows that touched, how
    many were put in, and how many places were left alone because the row
    above or the row below is in `keep_clear`."""
    keep_clear = keep_clear or set()
    todo = set(touching(lines))
    out: list[str] = []
    added = left = 0
    for i, line in enumerate(lines):
        out.append(line)
        if i not in todo:
            continue
        if i in keep_clear or i + 1 in keep_clear:
            left += 1
        else:
            out.append(EMPTY_ROW)
            added += 1
    return out, added, left


def title_of(row: str) -> str | None:
    found = _TITLE.match(row)
    return found.group(1).strip() if found else None


def plain(text: str) -> str:
    """Words only: no backticks, quotes, asterisks or tildes, one space
    between words, small letters."""
    return " ".join(re.sub(r"[`'\"‘’“”*~]", "", text).lower().split()).rstrip(".")


def rows_named(tasks: list[str], lines: list[str]) -> set[int]:
    """Indexes of the rows that a claim's `Task:` line names: the row's bold
    title stands in the line, whatever the line puts around it and however
    it quotes it (`Task: AGENTS.md row — 'with' statements are a parse
    error (ECMA-262 …)` names the row **`with` statements are a parse
    error.**). A title of fewer than 16 letters names nothing: it would be
    found in lines that only happen to use its words."""
    span = table_span(lines)
    if span is None:
        return set()
    said = [plain(task) for task in tasks if task]
    held = set()
    for i in range(*span):
        title = title_of(lines[i])
        if title is None or len(plain(title)) < 16:
            continue
        if any(plain(title) in line for line in said):
            held.add(i)
    return held


def rows_like(tasks: list[str], lines: list[str]) -> set[int]:
    """Indexes of the rows a `Task:` line is most probably about when it
    names none: for each line, the three rows that share the most of its
    telling words with it, those of them that share two at least. A telling
    word has five letters or more and stands in fewer than one row in five.
    (`Task:
    Finish label activation by forwarding a click on a <label> to its
    associated control` is about the row **A click on a `<label>` activates
    its labeled control.**) A wrong guess costs an empty row that is put in
    at the next watch and not at this one; a row not guessed costs its pull
    request a conflict."""
    span = table_span(lines)
    if span is None:
        return set()

    def words(text: str) -> set[str]:
        return set(re.findall(r"[a-z0-9][a-z0-9-]{4,}", plain(text)))

    rows = {i: words(lines[i]) for i in range(*span) if not is_empty_row(lines[i])}
    seen: dict[str, int] = {}
    for mine in rows.values():
        for word in mine:
            seen[word] = seen.get(word, 0) + 1
    common = {word for word, count in seen.items() if count * 5 > max(len(rows), 5)}
    held = set()
    for task in tasks:
        telling = words(task) - common
        scored = sorted(((len(telling & mine), -i) for i, mine in rows.items()), reverse=True)
        held |= {-i for shared, i in scored[:3] if shared >= 2}
    return held


def _git(*args: str) -> str:
    done = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    if done.returncode != 0:
        sys.exit(f"git {' '.join(args)} failed: {done.stderr.strip()[:200]}")
    return done.stdout


def lines_changed(old: list[str], diff: str) -> set[str]:
    """The lines of `old` that a `git diff --unified=0` from it changed or
    removed, and the one above and the one below each such place (for a pure
    insertion: the line it follows and the line it precedes)."""
    wanted: set[str] = set()
    for line in diff.splitlines():
        hunk = _HUNK.match(line)
        if not hunk:
            continue
        start = int(hunk.group(1))
        count = 1 if hunk.group(2) is None else int(hunk.group(2))
        low, high = (start - 1, start + count) if count else (start, start + 1)
        for number in range(low, high + 1):
            if 1 <= number <= len(old):
                wanted.add(old[number - 1])
    return wanted


def rows_a_ref_holds(ref: str, lines: list[str]) -> set[int]:
    """Indexes, in `lines`, of the rows that `ref` changed since it left this
    branch, and of the rows it put something beside."""
    base = _git("merge-base", "HEAD", ref).strip()
    old = _git("show", f"{base}:{DOCUMENT}").split("\n")
    wanted = lines_changed(old, _git("diff", "--unified=0", base, ref, "--", DOCUMENT))
    return {i for i, line in enumerate(lines)
            if line in wanted and line.startswith("|") and not is_empty_row(line)}


def open_pull_requests() -> list[dict]:
    done = subprocess.run(
        ["gh", "pr", "list", "--state", "open", "--limit", "100", "--json", "number,title,body"],
        cwd=ROOT, capture_output=True, text=True)
    if done.returncode != 0:
        sys.exit(f"gh pr list failed: {done.stderr.strip()[:200]}")
    return [pr for pr in json.loads(done.stdout)
            if not re.match(r"^(BOARD|WATCH):", pr["title"])]


def report(lines: list[str]) -> str | None:
    """One line for check_docs.py, or None when there is nothing to say."""
    span = table_span(lines)
    if span is None:
        return None
    first, end = span
    if not any(is_empty_row(line) for line in lines[first:end]):
        return None  # a tree from before the empty rows: nothing to hold it to
    rows = sum(1 for line in lines[first:end] if not is_empty_row(line))
    cut = breaks(lines)
    if cut:
        return (f"  note  {DOCUMENT}: a blank line at line {cut[0] + 1} cuts the table \"Open tasks\" in two: "
                f"the rows under it are not rows of the table any more. Take the blank line out "
                f"(tools/ci/check_rows.py --write does)")
    bad = touching(lines)
    if not bad:
        return f"  ok    {DOCUMENT}: an empty row stands between the {rows} rows of \"Open tasks\""
    names = "; ".join(f"line {i + 1} ({(title_of(lines[i]) or lines[i][2:40])[:40]})" for i in bad[:3])
    return (f"  note  {DOCUMENT}: {len(bad)} of the {rows} rows of \"Open tasks\" have a row directly under "
            f"them: {names}. Nothing for you to do but this: put `{EMPTY_ROW}` above a row you add. "
            f"The others are beside rows that open pull requests hold, and the landing session puts "
            f"them in at the end of a watch; one put in by hand conflicts with those pull requests")


def self_test() -> int:
    head = ["# x", "", HEADING, "", "Words.", "", "| Task | Files | Described in | Note |",
            "| :--- | :--- | :--- | :--- |"]
    a, b, c = "| **A long first task.** x | f | d | n |", "| ~~**The second task of three**~~ | f | d | n |", "| **c** | f | d | n |"
    table = head + [a, b, c]
    done = head + [a, EMPTY_ROW, b, EMPTY_ROW, c]
    old = ["one", "two", "three", "four", "five"]
    checks: list[tuple[str, object, object]] = [
        ("an empty row is pipes and spaces", is_empty_row("| | | | |"), True),
        ("and may be written tight", is_empty_row("||||"), True),
        ("a row with a word in it is not empty", is_empty_row("| | x | | |"), False),
        ("the delimiter row is not an empty row", is_empty_row("| :--- | :--- |"), False),
        ("the table is found under its heading", table_span(table), (8, 11)),
        ("no heading, no table", table_span(["| a |", "| :--- |", "| b |"]), None),
        ("a heading without a table has none", table_span(["## Open tasks", "", "## Next", "| a |", "| :--- |"]), None),
        ("two of three rows have a row under them", touching(table), [8, 9]),
        ("none when an empty row stands between", touching(done), []),
        ("the last row wants nothing under it", touching(head + [a, EMPTY_ROW, b]), []),
        ("writing puts one between each two", separated(table), (done, 2, 0)),
        ("writing twice changes nothing", separated(done), (done, 0, 0)),
        ("a held row keeps both its sides", separated(table, {9}), (table, 0, 2)),
        ("holding the first row leaves one place", separated(table, {8}), (head + [a, b, EMPTY_ROW, c], 1, 1)),
        ("lines after the table are not rows", touching(table + ["", "| x | y |", "| z | w |"]), [8, 9]),
        ("a tree without empty rows gets no report", report(table), None),
        ("a table in order is reported as ok", (report(done) or "").startswith("  ok "), True),
        ("a row that touches is a note", (report(head + [a, EMPTY_ROW, b, c]) or "").startswith("  note "), True),
        ("a row's title is its bold beginning", title_of(a), "A long first task."),
        ("struck or not", title_of(b), "The second task of three"),
        ("a Task line names its row", rows_named(["**A long first task.** In words"], table), {8}),
        ("however it quotes it", rows_named(["AGENTS.md row — 'the SECOND task of three' (ECMA-262)"], table), {9}),
        ("a line that names no row holds none", rows_named(["Implement PBKDF2 in SubtleCrypto"], table), set()),
        ("a short title names nothing", rows_named(["c", "**c**"], table), set()),
        ("a line that names no row is about the one it shares words with",
         rows_like(["Finish the longest first tasks: words about nothing"], head + ["| **A long first task.** words, nothing, longest, tasks | f | d | n |", EMPTY_ROW, b]), {8}),
        ("one shared word is too few", rows_like(["Finish the longest ones"], head + ["| **A long first task.** words, nothing, longest, tasks | f | d | n |", EMPTY_ROW, b]), set()),
        ("and about none when it shares too few", rows_like(["Implement PBKDF2 in SubtleCrypto"], table), set()),
        ("the Task line is read from a claim", _TASK.findall("Task: **A long first task.** In words\nFiles: x"), ["**A long first task.** In words"]),
        ("words only", plain("**`with`** 'statements'  are a ~~parse~~ error."), "with statements are a parse error"),
        ("a blank line with a row under it cuts the table", breaks(table + ["", c, a]), [11]),
        ("two of them are two", breaks(table + ["", c, "", a]), [11, 13]),
        ("a blank line with another table under it does not", breaks(table + ["", "| x | y |", "| :--- | :--- |", "| z | w |"]), []),
        ("two blank lines with a row under them cut it too, and both are found", breaks(table + ["", "", c, a]), [11, 12]),
        ("blank lines in two places are all found", breaks(table + ["", c, "", "", "", a]), [11, 13, 14, 15]),
        ("blank lines at the end of the table, with text under them, are not a cut", breaks(table + ["", "", "Text."]), []),
        ("two blank lines with another table under them are not a cut",
         breaks(table + ["", "", "| x | y |", "| :--- | :--- |", "| z | w |"]), []),
        ("nor one with words under it", breaks(table + ["", "Words."]), []),
        ("mending takes it out", mended(table + ["", c, a]), (table + [c, a], 1)),
        ("a cut table is a note", "cuts the table" in (report(done + ["", a]) or ""), True),
        ("a changed line holds itself and its neighbours", lines_changed(old, "@@ -3 +3 @@\n-three\n+3"), {"two", "three", "four"}),
        ("an insertion holds the lines it stands between", lines_changed(old, "@@ -2,0 +3 @@\n+new"), {"two", "three"}),
        ("a removal of two holds four", lines_changed(old, "@@ -2,2 +1,0 @@\n-two\n-three"), {"one", "two", "three", "four"}),
    ]
    failed = 0
    for what, got, want in checks:
        if got != want:
            failed += 1
            print(f"  FAIL  {what}: got {got!r}, expected {want!r}")
    print(f"check_rows --self-test: {len(checks) - failed} of {len(checks)} passed")
    return 1 if failed else 0


def main(arguments: list[str]) -> int:
    if arguments == ["--self-test"]:
        return self_test()
    path = ROOT / DOCUMENT
    lines = path.read_text(encoding="utf-8").split("\n")
    if table_span(lines) is None:
        sys.exit(f"{DOCUMENT} has no table under \"{HEADING}\"")
    if not arguments:
        print(report(lines) or f"  note  {DOCUMENT}: no empty row stands between the rows of \"Open tasks\": "
                               f"a tree from before 24 November 2026 (tools/ci/check_rows.py --write)")
        return 0
    if arguments[0] != "--write":
        sys.exit(__doc__)
    rest = arguments[1:]
    lines, joined = mended(lines)
    if joined:
        print(f"check_rows: {joined} blank line(s) that cut the table in two taken out")
    held: set[int] = set()
    if rest in ([], ["--open"]) or (len(rest) == 2 and rest[0] == "--except"):
        merged = {int(n) for n in rest[1].split(",")} if rest[:1] == ["--except"] else set()
        for pr in open_pull_requests():
            if pr["number"] in merged:
                continue
            ref = f"refs/remotes/origin/pr{pr['number']}"
            _git("fetch", "-q", "origin", f"+refs/pull/{pr['number']}/head:{ref}")
            tasks = _TASK.findall(pr["body"] or "")
            mine = rows_a_ref_holds(ref, lines) | rows_named(tasks, lines)
            mine = mine or rows_like(tasks, lines)
            if mine:
                print(f"  #{pr['number']} holds: " + "; ".join((title_of(lines[i]) or lines[i][2:40])[:44] for i in sorted(mine)))
            held |= mine
    elif rest[:1] == ["--keep-clear-of"] and len(rest) > 1:
        for ref in rest[1:]:
            held |= rows_a_ref_holds(ref, lines)
    else:
        sys.exit(__doc__)
    out, added, left = separated(lines, held)
    if added or joined:
        path.write_text("\n".join(out), encoding="utf-8")
    print(f"check_rows: {added} empty row(s) put in; {left} place(s) left alone beside a row that an open pull request holds")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
