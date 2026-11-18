#!/usr/bin/env python3
"""Checks that the numbers the documents publish still divide and still count.

Why this exists
---------------
`README.md` is the scoreboard, and it said ``1124 / 1150 (97.6%)`` while the
same two integers divide to 97.7. `ROADMAP.md`'s "honest baseline" quoted a
JavaScript corpus two phases out of date (1,020/1,034 where the tree holds
1,150 cases and 1,124 pass, and 27,872/47,967 of test262 where 29,450 pass).
Both numbers were copied from an earlier log by an agent in a hurry; neither
had been divided, and neither had been re-read against the corpus it names.
`AGENTS.md` already asks for the suite count to be corrected on merge. A rule
in prose is what an agent remembers; a check that fails is what it cannot
forget. `check_docs.py` guards the *shape* of the documents and
`next_ids.py` guards which *number is free*. This guards whether the number
that is written down is still true.

Three claims are checked, because three can be derived without running the
engine:

arithmetic
    Every ``A / B (P%)`` in `README.md` and `ROADMAP.md` must divide: P is
    A/B*100, rounded to the precision P is written with. This is the check
    that would have caught 97.6.

denominators
    The JavaScript and DOM rows of the scoreboard name their corpus in full.
    That total is counted from the corpus on disk with the same parser the
    runner uses (`run_js_conformance.parse_dat`), so the document and the
    corpus cannot disagree about what a case is. Adding cases without moving
    the total fails here.

suite count
    ``214 suites`` is ``grep -c '^    RUN_TEST(' tests/test_suite.cpp``, on
    the lines of `README.md` that speak in the present tense: its first 120
    lines, the table and the command list. The phase write-ups quote the count
    they had when they were written, and keep it.

What is not checked, and why
----------------------------
Whether a paragraph still describes the corpus it names. `README.md` keeps its
history, so a stale number is also a number that appears in it, and presence
is not a test of truth. `ROADMAP.md`'s baseline was corrected by hand in the
pull request that added this file; the next one has to be read by a person.

    python3 tools/ci/check_numbers.py

It exits 0 when every check passes and 1 when any fails.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# The runner's own parser, so "a case" means the same thing to both.
sys.path.insert(0, str(ROOT / "tools" / "conformance"))
from run_js_conformance import parse_dat  # noqa: E402

README = ROOT / "README.md"
ROADMAP = ROOT / "ROADMAP.md"
SUITE_SOURCE = ROOT / "tests" / "test_suite.cpp"

# Lines of README.md that speak in the present tense. Past 120 the phase
# write-ups begin, and they are quoting the count they had, not today's.
PRESENT_TENSE = 120

PERCENT = re.compile(r"(\d[\d,]*)\s*/\s*(\d[\d,]*)\s*\((\d+(?:\.\d+)?)%\)")
FRACTION = re.compile(r"(\d[\d,]*)\s*/\s*(\d[\d,]*)")
SUITE_CLAIM = re.compile(r"(\d[\d,]*)(?:\s*/\s*(\d[\d,]*))?\s+suites\b")
# "217 engine suites" and "All 217 engine test suites passed": the command
# list and the sample output say the count in the present tense too, and the
# first of them is what the general pattern above misses — a word stands
# between the number and the noun.
SUITE_CLAIM_ANYWHERE = re.compile(r"(\d[\d,]*)\s+engine(?:\s+test)?\s+suites\b")
RUN_TEST = re.compile(r"^    RUN_TEST\(", re.MULTILINE)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def corpus_cases(directory: Path) -> int:
    """The number of cases in a .dat corpus, as the runner counts them."""
    return sum(len(parse_dat(path)) for path in sorted(directory.glob("*.dat")))


def line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def check_arithmetic(problems: list[str]) -> int:
    checked = 0
    for name in ("README.md", "ROADMAP.md"):
        text = read(ROOT / name)
        for match in PERCENT.finditer(text):
            checked += 1
            numerator = int(match.group(1).replace(",", ""))
            denominator = int(match.group(2).replace(",", ""))
            stated = match.group(3)
            decimals = len(stated.split(".")[1]) if "." in stated else 0
            exact = numerator / denominator * 100
            # The claim is right if it is the division, rounded to the
            # precision it was written with.
            if abs(round(exact, decimals) - float(stated)) > 10 ** (-decimals) / 2 + 1e-9:
                problems.append(
                    f"{name}:{line_of(text, match.start())}: {match.group(0)}"
                    f" — {numerator:,}/{denominator:,} is {exact:.{decimals + 1}f}%"
                )
    return checked


def check_denominators(problems: list[str]) -> list[tuple[Path, int, int]]:
    """The scoreboard's corpus totals, against the corpora on disk."""
    text = read(README)
    found: list[tuple[Path, int, int]] = []
    rows = {
        "| **JavaScript corpus**": ROOT / "tests" / "js-conformance",
        "| **DOM corpus**": ROOT / "tests" / "dom-conformance",
    }
    for anchor, directory in rows.items():
        line = next((ln for ln in text.split("\n") if ln.startswith(anchor)), None)
        if line is None:
            problems.append(f"README.md: no {anchor!r} row to check")
            continue
        match = FRACTION.search(line)
        if match is None:
            problems.append(f"README.md: the {anchor!r} row names no total")
            continue
        stated = int(match.group(2).replace(",", ""))
        actual = corpus_cases(directory)
        found.append((directory, actual, stated))
        if stated != actual:
            problems.append(
                f"README.md: the {anchor[3:].strip('* ')} row names {stated:,} cases;"
                f" {directory.relative_to(ROOT)} holds {actual:,}"
            )
    return found


def check_suite_count(problems: list[str]) -> int:
    registered = len(RUN_TEST.findall(read(SUITE_SOURCE)))
    lines = read(README).split("\n")
    reported: set[int] = set()

    def report(number: int, text: str, digits: str) -> None:
        if number in reported:
            return
        reported.add(number)
        if int(digits.replace(",", "")) != registered:
            problems.append(
                f"README.md:{number}: {text.strip()};"
                f" tests/test_suite.cpp registers {registered}"
            )

    for number, line in enumerate(lines[:PRESENT_TENSE], start=1):
        for match in SUITE_CLAIM.finditer(line):
            for group in (match.group(1), match.group(2)):
                if group is not None:
                    report(number, match.group(0), group)
    for number, line in enumerate(lines, start=1):
        for match in SUITE_CLAIM_ANYWHERE.finditer(line):
            report(number, match.group(0), match.group(1))
    return registered


def main() -> int:
    arithmetic: list[str] = []
    percentages = check_arithmetic(arithmetic)

    denominators: list[str] = []
    corpora = check_denominators(denominators)

    suites: list[str] = []
    registered = check_suite_count(suites)

    print("check_numbers: the numbers the documents publish")
    if arithmetic:
        print(f"  FAIL  README.md, ROADMAP.md: {percentages} percentage claims")
    else:
        print(f"  ok    README.md, ROADMAP.md: {percentages} percentage claims divide")
    if denominators:
        print(f"  FAIL  {len(denominators)} corpus total(s) disagree with the corpus")
    else:
        for directory, actual, stated in corpora:
            print(
                f"  ok    {directory.relative_to(ROOT)}: {actual:,} cases,"
                f" README.md names {stated:,}"
            )
    if suites:
        print(f"  FAIL  tests/test_suite.cpp registers {registered} suites")
    else:
        print(f"  ok    tests/test_suite.cpp registers {registered} suites, as README.md says")

    problems = arithmetic + denominators + suites
    if problems:
        print()
        for problem in problems:
            print(f"  {problem}")
        print(f"\ncheck_numbers: {len(problems)} failure(s)")
        return 1
    print("\ncheck_numbers: 0 failure(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
