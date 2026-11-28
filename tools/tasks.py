#!/usr/bin/env python3
"""The table "Open tasks" of AGENTS.md, without reading it.

    python3 tools/tasks.py             the open tasks: one line each, and what the row says is still open
    python3 tools/tasks.py N           row N whole: its task, files, where it is described, its note
    python3 tools/tasks.py WORD ...    the rows that have every WORD (one row: printed whole)
    python3 tools/tasks.py --all       the list with the rows that are done as well
    python3 tools/tasks.py --sizes     what each section of AGENTS.md weighs
    python3 tools/tasks.py --self-test

Why. An agent begins by reading AGENTS.md, and on 28 November 2026 that
file was 383 KB, some 97,000 tokens. 105 KB of it were the rules. The other
277 KB were this table: 97 rows, 29 of them done and struck through, 3 KB
each on average and the longest 14 KB, every one a paragraph that tells
what the phases behind it did and ends with what is still open. In the 47
open rows that name what is still open, those words were 26 KB of 183. An
agent needs the rules, a list to choose from, and one row. Read whole, the
table took more room in its context than everything else it would read
that day, and the person had just said that one agent's long chat had
begun to tell on its work.

It is the story of ARCHITECTURE.md and AUDIT.md over again ("Notice", at
the top of AGENTS.md: 731 KB read to find two integers). Those were moved
into files. This table is still one, because every pull request rewrites
its row and the tools that keep two of them from colliding read rows
(check_rows.py, check_docs.py, tools/watch/carry_rows.py): this reads it
for the agent instead, and changes nothing.

N is the row's place in the table as it is now, counted without the empty
rows. It is good for the list just printed, not for tomorrow: a new row
goes under the row it came from. A pull request names its task by the
row's first words, as before.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SECTION = "## Open tasks"


def cells(line: str) -> list[str]:
    """The cells of one row. A `|` inside a code span, or after a backslash, is text."""
    out, cell, in_code, i = [], [], False, 0
    while i < len(line):
        ch = line[i]
        if ch == "\\" and i + 1 < len(line) and line[i + 1] == "|":
            cell.append("\\|")
            i += 2
            continue
        if ch == "`":
            in_code = not in_code
        if ch == "|" and not in_code:
            out.append("".join(cell).strip())
            cell = []
        else:
            cell.append(ch)
        i += 1
    out.append("".join(cell).strip())
    return out[1:-1] if len(out) >= 2 else out


def rows(text: str) -> list[dict]:
    """The rows of the table, in order, without the header and the empty rows."""
    start = text.find(f"\n{SECTION}\n")
    if start < 0:
        return []
    found = []
    for number, line in enumerate(text[:start].count("\n") * [""] + text[start:].split("\n")):
        if not line.startswith("|"):
            continue
        parts = cells(line)
        if not any(parts) or parts[0] == "Task" or set("".join(parts)) <= set(":- "):
            continue
        first = parts[0]
        done = first.startswith("~~")
        bold = re.search(r"\*\*(.+?)\*\*", first)
        title = (bold.group(1) if bold else first[:120]).strip()
        found.append({"line": number, "title": title, "done": done, "size": len(line.encode()),
                      "cells": parts, "text": line})
    return found


def still_open(row: dict, width: int = 230) -> str:
    """What the row says is still open: from its last "still open" on, cut to `width`."""
    text = row["cells"][0] if row["cells"] else row["text"]
    at = [m.start() for m in re.finditer(r"(?i)\bstill open\b|\bremain(?:s|ing)? open\b|\bstays open\b", text)]
    if not at:
        return ""
    piece = re.sub(r"\s+", " ", text[at[-1]:]).strip(" *:.")
    return piece if len(piece) <= width else piece[:width].rsplit(" ", 1)[0] + " ..."


def listing(found: list[dict], everything: bool) -> list[str]:
    out = []
    for place, row in enumerate(found, 1):
        if row["done"] and not everything:
            continue
        mark = "done " if row["done"] else ""
        out.append(f"{place:>3}  {row['size'] / 1024:4.1f} KB  {mark}{row['title'][:110]}")
        rest = still_open(row)
        if rest and not row["done"]:
            out.append(f"       {rest}")
    return out


def whole(place: int, row: dict) -> list[str]:
    names = ("Task", "Files", "Described in", "Note")
    out = [f"row {place} of the table, line {row['line'] + 1} of AGENTS.md, {row['size'] / 1024:.1f} KB"
           + (" (done)" if row["done"] else ""), ""]
    for name, cell in zip(names, row["cells"] + [""] * 4):
        if cell:
            out += [f"{name}:", cell, ""]
    return out


def sizes(text: str) -> list[str]:
    parts = re.split(r"(?m)^(## .*)$", text)
    total = len(text.encode())
    out = [f"{len(parts[0].encode()) / 1024:6.1f} KB        (before the first section)"]
    for i in range(1, len(parts), 2):
        size = len(parts[i + 1].encode())
        out.append(f"{size / 1024:6.1f} KB  {100 * size / total:4.1f}%  {parts[i][3:80]}")
    found = rows(text)
    out.append(f"{total / 1024:6.1f} KB in all, about {total // 4000},000 tokens. The table has {len(found)} rows, "
               f"{sum(1 for r in found if r['done'])} of them done; the longest is {max((r['size'] for r in found), default=0) / 1024:.1f} KB")
    return out


def self_test() -> int:
    table = "\n".join([
        "# Rules", "", "text | with a bar", "", SECTION, "", "Rows that share files...", "",
        "| Task | Files | Described in | Note |", "| :--- | :--- | :--- | :--- |",
        "| **A first task since Phase 5** (`docs/phases/5-a.md`): what was done. **Still open:** the second half, "
        "and `a | b` in code. | `src/a.cpp` | `docs/phases/5-a.md` | a note |",
        "| | | | |",
        "| ~~**A task that was open**~~ **Done in Phase 7**: all of it. Still open: nothing. | `src/b.cpp` | | |",
        "| | | | |",
        "| **A third, which names no remainder** and an escaped \\| bar | `src/c.cpp` | | |",
        "",
    ])
    found = rows(table)
    cases = [
        ("three rows, the header and the empty rows left out", len(found), 3),
        ("a row's title is its first bold words", [r["title"] for r in found],
         ["A first task since Phase 5", "A task that was open", "A third, which names no remainder"]),
        ("a struck-through row is done", [r["done"] for r in found], [False, True, False]),
        ("a bar inside a code span, or after a backslash, does not end a cell", [len(r["cells"]) for r in found], [4, 4, 4]),
        ("...and the cells are the row's own", found[0]["cells"][1:], ["`src/a.cpp`", "`docs/phases/5-a.md`", "a note"]),
        ("what is still open is read from the row's last such words", still_open(found[0]),
         "Still open:** the second half, and `a | b` in code"),
        ("a row that names no remainder says nothing", still_open(found[2]), ""),
        ("the list leaves the done rows out, and keeps each row's place in the table",
         [line[:4].strip() for line in listing(found, False) if line[:4].strip()], ["1", "3"]),
        ("...unless they are asked for", len([line for line in listing(found, True) if line[:4].strip()]), 3),
        ("a row is printed whole under the names of its cells", whole(1, found[0])[2:4],
         ["Task:", found[0]["cells"][0]]),
        ("the line is the row's line in the file", found[0]["line"] + 1, 11),
        ("a file with no such table has no rows", rows("# nothing\n\n| a | b |\n"), []),
        ("the sections are weighed, and the table's rows counted", sizes(table)[-1].split(". ", 1)[1][:38],
         "The table has 3 rows, 1 of them done; "),
    ]
    failed = 0
    for name, got, wanted in cases:
        if got == wanted:
            print(f"  [ok  ] {name}")
        else:
            failed += 1
            print(f"  [FAIL] {name}\n         wanted {wanted!r}\n         got    {got!r}")
    print(f"{len(cases) - failed}/{len(cases)} pass")
    return 1 if failed else 0


def main(arguments: list[str]) -> int:
    if arguments == ["--self-test"]:
        return self_test()
    if arguments and arguments[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    text = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
    if arguments == ["--sizes"]:
        print("\n".join(sizes(text)))
        return 0
    found = rows(text)
    if not found:
        print(f'no table under "{SECTION}" in AGENTS.md', file=sys.stderr)
        return 1
    everything = "--all" in arguments
    words = [a for a in arguments if a != "--all"]
    if not words:
        lines = listing(found, everything)
        open_ones = sum(1 for r in found if not r["done"])
        print("\n".join(lines))
        print(f"\n{open_ones} open task(s) of {len(found)} rows. `python3 tools/tasks.py N` prints row N whole; "
              "`python3 tools/tasks.py WORD ...` finds rows.")
        return 0
    if len(words) == 1 and words[0].isdigit():
        place = int(words[0])
        if not 1 <= place <= len(found):
            print(f"the table has rows 1 to {len(found)}", file=sys.stderr)
            return 1
        print("\n".join(whole(place, found[place - 1])))
        return 0
    hits = [(place, row) for place, row in enumerate(found, 1)
            if all(word.lower() in row["text"].lower() for word in words)]
    if not hits:
        print("no row has " + " and ".join(repr(w) for w in words), file=sys.stderr)
        return 1
    if len(hits) == 1:
        print("\n".join(whole(*hits[0])))
        return 0
    for place, row in hits:
        mark = "done " if row["done"] else ""
        print(f"{place:>3}  {row['size'] / 1024:4.1f} KB  {mark}{row['title'][:110]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
