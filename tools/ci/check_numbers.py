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
    ``214 suites`` is ``grep -c '^    RUN_TEST(' tests/test_suite.cpp`` and,
    since 23 November 2026, the suites that are files of their own with it:
    ``grep -c '^TEST(' tests/suites/*.cpp`` (tests/suites/README.md). On
    the lines of `README.md` that speak in the present tense: its first 120
    lines, the table and the command list. The phase write-ups quote the count
    they had when they were written, and keep it.

    **Since 22 November 2026 a count that is behind is a note, not a failure,
    unless ``--suites`` is given**, and ``--write`` leaves it alone unless
    ``--suites`` is given. Nearly every pull request adds a suite, so nearly
    every pull request edited the same six places of `README.md`: each merge
    put every other finished pull request in conflict there, and when git saw
    no conflict (two branches that had each written the same new number)
    `main` failed this check after the merge: #91, #150, #146 and #170. The
    count is the landing session's now (AGENTS.md, "Working together"): it
    runs ``--write --suites`` as the last act of a watch. A pull request does
    not touch the count, and nothing fails for its being behind.

The ledger and the markers
--------------------------
``docs/metrics.json`` holds what had to be *measured* — a score from running a
corpus — with the command that produced it and the day it did. What can be
counted is derived here instead: the suite count from `tests/test_suite.cpp`
and `tests/suites/`, the corpus totals from the corpus on disk.

A document that publishes one of those figures marks it, so that no reader has
to guess which number is the live one and no agent has to find the eight places
that repeat it::

    **1137 / 1156 (98.4%)**<!--metrics:js/frac_pct-->

The marker is an HTML comment: invisible in the rendered document, and the text
immediately before it is the claim the format names. ``--write`` rewrites that
text from the ledger and the tree; without it, a claim that disagrees with
either is a failure. ``make metrics`` is the write. A marker naming a figure the
ledger does not have is a failure too — that is how a typo in a marker is caught
rather than quietly refreshing nothing.

The ``--min-pass`` floors of `.github/workflows/ci.yml` are read the same way:
they are the score the tree currently reaches, and ``--write`` raises them to
it. A floor is never lowered: if the tree would lower one, that is a
regression, and it is reported instead of written.

What is not checked, and why
-----------------------------
Whether a paragraph still describes the corpus it names. `README.md` keeps its
history, so a stale number is also a number that appears in it, and presence
is not a test of truth. `ROADMAP.md`'s baseline was corrected by hand in the
pull request that added this file. Every *live* figure in `README.md` is a
marker now; an unmarked one is history, and history is read by a person.

    python3 tools/ci/check_numbers.py            # check
    python3 tools/ci/check_numbers.py --write    # refresh the marked claims
    python3 tools/ci/check_numbers.py --suites   # check, the suite count too
    python3 tools/ci/check_numbers.py --write --suites   # refresh, the suite count too

It exits 0 when every check passes and 1 when any fails.
"""

from __future__ import annotations

import json
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
SUITE_DIR = ROOT / "tests" / "suites"
LEDGER = ROOT / "docs" / "metrics.json"
CI_WORKFLOW = ROOT / ".github" / "workflows" / "ci.yml"
MAKEFILE = ROOT / "Makefile"

# The documents whose marked claims are refreshed. ARCHITECTURE.md and AUDIT.md
# are frozen append targets, and a phase row quotes the figures of its own day.
DOCUMENTS = (README, ROADMAP)

# `<!--metrics:js/frac_pct-->`: the key names a figure, the format names the
# shape of the claim the marker follows.
MARKER = re.compile(r"<!--metrics:([\w.]+)/(\w+)-->")
# Anything that begins like a marker and is not one. A marker the pattern does
# not match is a marker that refreshes nothing, silently, which is worse than a
# figure nobody marked.
LOOKS_LIKE_MARKER = re.compile(r"<!--metrics:[^>]*-->")

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
# A suite that is a file of its own: `TEST(name)` at the start of a line of a
# file of tests/suites/. It has no `RUN_TEST` line; the list the suites keep
# themselves runs it (tests/test_suite.cpp, `ListedSuite`).
OWN_FILE_SUITE = re.compile(r"^TEST\(\w+\)", re.MULTILINE)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def suite_count(source: Path | None = None, directory: Path | None = None) -> int:
    """The suites `make test` runs.

    Those `main()` has a `RUN_TEST` line for, in tests/test_suite.cpp, and
    those that are files of their own in tests/suites/. Neither is counted
    twice: a suite of tests/suites/ cannot have a `RUN_TEST` line, because
    `main()` is compiled before the file is read.
    """
    source = SUITE_SOURCE if source is None else source
    directory = SUITE_DIR if directory is None else directory
    listed = len(RUN_TEST.findall(read(source)))
    own = sum(len(OWN_FILE_SUITE.findall(read(path))) for path in sorted(directory.glob("*.cpp")))
    return listed + own


def corpus_cases(directory: Path) -> int:
    """The number of cases in a .dat corpus, as the runner counts them."""
    return sum(len(parse_dat(path)) for path in sorted(directory.glob("*.dat")))


def line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def visible(text: str) -> str:
    """The document as a reader sees it: markers removed.

    Removed rather than replaced by a placeholder, so the patterns below see the
    same numbers they did before the markers existed. No marker holds a newline,
    so line numbers survive.
    """
    return MARKER.sub("", text)


# ---------------------------------------------------------------------------
# The ledger, and the figures that are derived rather than measured
# ---------------------------------------------------------------------------

class UnknownFigure(Exception):
    """A marker names a figure the ledger does not have, or a shape it is not."""


def figures() -> tuple[dict[str, int], set[str]]:
    """Every figure a document may mark, by name, and which of them were measured.

    Measured ones come from docs/metrics.json; counted ones are derived from the
    tree here, so a document cannot name a count that the tree does not have.
    """
    ledger = json.loads(read(LEDGER))["measured"]
    out: dict[str, int] = {}
    measured: set[str] = set()

    out["unit_suites"] = suite_count()
    out["js.total"] = corpus_cases(ROOT / "tests" / "js-conformance")
    out["js.pass"] = int(ledger["js_conformance"]["pass"])
    out["dom.total"] = corpus_cases(ROOT / "tests" / "dom-conformance")
    out["dom.pass"] = int(ledger["dom_conformance"]["pass"])
    out["probe.pass"] = int(ledger["everyday_probe"]["pass"])
    out["probe.total"] = int(ledger["everyday_probe"]["of"])
    out["url.pass"] = int(ledger["url_conformance"]["pass"])
    out["url.total"] = int(ledger["url_conformance"]["of"])
    out["wpt.pass"] = int(ledger["wpt"]["pass"])
    out["wpt.total"] = int(ledger["wpt"]["of"])
    for prefix, key in (
        ("t262.default", "test262_default_directories"),
        ("t262.class", "test262_class_directories"),
    ):
        row = ledger[key]
        out[f"{prefix}.before"] = int(row["before_pass"])
        out[f"{prefix}.before_total"] = int(row["before_of"])
        out[f"{prefix}.now"] = int(row["now_pass"])
        out[f"{prefix}.total"] = int(row["now_of"])
        out[f"{prefix}.fixed"] = int(row["fixed"])
    whole = ledger["test262_whole_suite"]
    out["t262.full.pass"] = int(whole["pass"])
    out["t262.full.total"] = int(whole["of"])
    measured |= {
        "js.pass", "dom.pass", "probe.pass", "url.pass", "url.total", "wpt.pass", "wpt.total",
        "t262.default.before", "t262.default.before_total", "t262.default.now", "t262.default.total", "t262.default.fixed",
        "t262.class.before", "t262.class.before_total", "t262.class.now", "t262.class.total", "t262.class.fixed",
        "t262.full.pass", "t262.full.total",
    }
    return out, measured


def percent(passed: int, total: int) -> str:
    """The percentage as the documents write it: one decimal, always.

    `98.4%`, `87.0%`, `65.0%`: the decimal is kept when it is zero, because the
    column is read by eye and `65%` next to `98.4%` reads as a different
    precision rather than as the same measure.
    """
    value = round(passed / total * 100, 1)
    # `100%` is the one percentage the documents write without a decimal: it is
    # the only exact one, and it is read as a slogan rather than as a measure.
    return "100" if value == 100 else f"{value:.1f}"


def num(value: int) -> str:
    return f"{value:,}"


def resolve(name: str, values: dict[str, int]) -> tuple[int, int | None]:
    """The figure a name means, and the total it is a fraction of, if any.

    `js.pass` is the pass count of a corpus whose total is `js.total`, and `js`
    is the same figure, because a bare corpus name means its score;
    `t262.default` is the same as `t262.default.now`.
    `t262.default.before` has a total of `t262.default.before_total`, and a
    `_pct` suffix asks for the same figure as a percentage.
    """
    base = name[: -len("_pct")] if name.endswith("_pct") else name
    # A total is a figure, not a score: `js.total` is 1,156, not 1,156/1,156.
    if base.endswith((".total", "_total")) and base in values:
        return values[base], None
    candidates = [base] if base in values else []
    if not base.endswith((".pass", ".now", ".before")):
        candidates += [base + suffix for suffix in (".pass", ".now") if base + suffix in values]
    for candidate in candidates:
        if candidate.endswith(".before"):
            total = values.get(candidate + "_total")
        else:
            total = values.get(candidate.rsplit(".", 1)[0] + ".total")
        if total is not None:
            return values[candidate], total
    if base in values:
        return values[base], None
    raise UnknownFigure(f"{name!r} is not a figure: docs/metrics.json has no such key")


def claim_text(name: str, shape: str, values: dict[str, int]) -> tuple[str, re.Pattern[str]]:
    """The text a marker's claim should read, and the shape it replaces."""
    try:
        value, total = resolve(name, values)
    except UnknownFigure:
        raise
    pct_of = (lambda: f"{percent(value, total)}%") if total is not None else None
    if shape == "int":
        return f"{value}", re.compile(r"\d[\d,]*\Z")
    if shape == "num":
        return num(value), re.compile(r"\d[\d,]*\Z")
    if shape == "pct":
        if total is None:
            raise UnknownFigure(f"{name!r} has no total to divide by")
        return pct_of(), re.compile(r"\d+(?:\.\d+)?%\Z")
    if shape in ("frac", "frac_pct", "of", "of_pct"):
        if total is None:
            raise UnknownFigure(f"{shape!r} needs a total: {name!r} has none")
        joiner = " / " if shape.startswith("frac") else " of "
        # The slash fractions are four figures and are written plain
        # (`1137 / 1156`); the `of` ones are test262 counts and carry the
        # separators (`13,654 of 15,686`).
        style = str if shape.startswith("frac") else num
        body = joiner.join((style(value), style(total)))
        pattern = r"\d[\d,]*\s*(?:/|of)\s*\d[\d,]*"
        if shape.endswith("_pct"):
            body += f" ({pct_of()})"
            pattern += r"\s*\(\d+(?:\.\d+)?%\)"
        return body, re.compile(pattern + r"\Z")
    if shape == "pair":
        # "260/260": the same count twice, as the sample output writes it.
        return f"{value}/{value}", re.compile(r"\d[\d,]*/\d[\d,]*\Z")
    raise UnknownFigure(f"{shape!r} is not a shape: use int, num, pct, frac, frac_pct, of, of_pct or pair")


# The figure that is not a pull request's to keep right (see "suite count" at
# the top): behind is a note unless `--suites`.
LANDING_SESSIONS = {"unit_suites"}


def check_markers(problems: list[str], values: dict[str, int],
                  behind: list[str] | None = None) -> int:
    """`behind`, when given, takes the marked suite counts that disagree."""
    checked = 0
    for path in DOCUMENTS:
        raw = read(path)
        for match in LOOKS_LIKE_MARKER.finditer(raw):
            if MARKER.fullmatch(match.group(0)) is None:
                problems.append(
                    f"{path.name}:{line_of(raw, match.start())}: {match.group(0)} is not a"
                    f" marker: use <!--metrics:<figure>/<shape>-->"
                )
        for match in MARKER.finditer(raw):
            checked += 1
            name, shape = match.group(1), match.group(2)
            where = f"{path.name}:{line_of(raw, match.start())}"
            try:
                expected, shape_re = claim_text(name, shape, values)
            except UnknownFigure as exc:
                problems.append(f"{where}: {exc}")
                continue
            written = shape_re.search(raw[: match.start()])
            if written is None:
                problems.append(
                    f"{where}: nothing shaped like {shape!r} before the marker for {name}"
                    f" (expected {expected!r})"
                )
            elif written.group(0) != expected:
                late = behind if behind is not None and name in LANDING_SESSIONS else problems
                late.append(
                    f"{where}: {written.group(0)} but the tree says {expected}"
                    f" ({name}, {shape})"
                )
    return checked


def first_number(text: str) -> int:
    return int(re.match(r"[\d,]+", text).group(0).replace(",", ""))


def write_markers(values: dict[str, int], measured: set[str], problems: list[str],
                  allow_lowering: bool, suites: bool = True) -> list[str]:
    """Rewrite every marked claim; return what changed, as readable lines.

    A measured score is only ever *raised* by this: a marked figure that would
    go down is either a regression or a ledger that has not been re-measured,
    and both are decisions for a person, taken with `--scores`.

    Without `suites` the suite count is left as it is written: it is the
    landing session's to refresh, and a pull request that rewrote it would be
    in conflict with every other one that did.
    """
    changes: list[str] = []
    for path in DOCUMENTS:
        text = read(path)
        pieces: list[tuple[int, int, str]] = []
        for match in MARKER.finditer(text):
            name, shape = match.group(1), match.group(2)
            if name in LANDING_SESSIONS and not suites:
                continue
            where = f"{path.name}:{line_of(text, match.start())}"
            try:
                expected, shape_re = claim_text(name, shape, values)
            except UnknownFigure as exc:
                problems.append(f"{where}: {exc}")
                continue
            written = shape_re.search(text[: match.start()])
            if written is None:
                problems.append(f"{where}: nothing shaped like {shape!r} before the marker for {name}")
                continue
            if written.group(0) != expected:
                lowered = (
                    name in measured
                    and first_number(written.group(0)) > first_number(expected)
                )
                if lowered and not allow_lowering:
                    problems.append(
                        f"{where}: {written.group(0)} would become {expected}; that is a"
                        f" lower score for a measured figure — re-measure it, or pass"
                        f" --scores to record the regression"
                    )
                    continue
                changes.append(f"{where}: {written.group(0)} -> {expected}")
                pieces.append((written.start(), match.start(), expected))
        if not pieces:
            continue
        out: list[str] = []
        cursor = 0
        for start, end, replacement in pieces:
            out.append(text[cursor:start])
            out.append(replacement)
            cursor = end
        out.append(text[cursor:])
        path.write_text("".join(out), encoding="utf-8")
    return changes


# ---------------------------------------------------------------------------
# The floors CI enforces
# ---------------------------------------------------------------------------

MIN_PASS = re.compile(r"--min-pass\s+(\d+)")
# Two floors are Makefile variables rather than `--min-pass` arguments.
FLOOR_VARIABLES = {
    "URL_FLOOR": "url.pass",
    "WPT_FLOOR": "wpt.pass",
}
FLOOR_VARIABLE = re.compile(r"^(%s)\s*\?=\s*(\d+)$" % "|".join(FLOOR_VARIABLES), re.MULTILINE)
# A floor is the score a corpus reaches today; the runner that measures it names
# the figure. The others (wpt, url, xml) take their floor from the Makefile.
FLOOR_RUNNERS = {
    "run_js_conformance.py": "js.pass",
    "run_dom_conformance.py": "dom.pass",
}


def floor_lines(text: str):
    """Every `--min-pass` in the workflow, as (offset, floor, figure name)."""
    for match in MIN_PASS.finditer(text):
        start = text.rfind("\n", 0, match.start()) + 1
        end = text.find("\n", match.start())
        line = text[start:end]
        for runner, name in FLOOR_RUNNERS.items():
            if runner in line:
                yield match, int(match.group(1)), name


def check_floors(problems: list[str], values: dict[str, int]) -> int:
    text = read(CI_WORKFLOW)
    checked = 0
    for _, floor, name in floor_lines(text):
        checked += 1
        if floor != values[name]:
            problems.append(
                f"ci.yml: `--min-pass {floor}` is not what the ledger says"
                f" {values[name]} ({name}) — run `make metrics`"
            )
    for match in FLOOR_VARIABLE.finditer(read(MAKEFILE)):
        variable = match.group(1)
        name = FLOOR_VARIABLES[variable]
        checked += 1
        if int(match.group(2)) != values[name]:
            problems.append(
                f"Makefile: {variable} is {match.group(2)}, not the ledger's"
                f" {values[name]} ({name}) — run `make metrics`"
            )
    return checked


def write_floors(values: dict[str, int], problems: list[str]) -> list[str]:
    text = read(CI_WORKFLOW)
    changes: list[str] = []
    out: list[str] = []
    cursor = 0
    for match, floor, name in floor_lines(text):
        want = values[name]
        if floor == want:
            continue
        if floor > want:
            # A floor is never lowered to let a tree pass: that is a
            # regression, and it is reported, not written.
            problems.append(
                f"ci.yml: `--min-pass {floor}` is above what the tree reaches"
                f" ({want}, {name}): a regression, and a floor is not lowered"
            )
            continue
        out.append(text[cursor : match.start(1)])
        out.append(str(want))
        cursor = match.end(1)
        changes.append(f"ci.yml: `--min-pass {floor}` -> {want} ({name})")
    out.append(text[cursor:])
    if changes:
        CI_WORKFLOW.write_text("".join(out), encoding="utf-8")

    makefile = read(MAKEFILE)
    out = []
    cursor = 0
    for match in FLOOR_VARIABLE.finditer(makefile):
        variable = match.group(1)
        name = FLOOR_VARIABLES[variable]
        floor, want = int(match.group(2)), values[name]
        if floor == want:
            continue
        if floor > want:
            problems.append(
                f"Makefile: {variable} is {floor}, above what the tree reaches"
                f" ({want}, {name}): a regression, and a floor is not lowered"
            )
            continue
        out.append(makefile[cursor : match.start(2)])
        out.append(str(want))
        cursor = match.end(2)
        changes.append(f"Makefile: {variable} {floor} -> {want} ({name})")
    out.append(makefile[cursor:])
    if changes and "".join(out) != makefile:
        MAKEFILE.write_text("".join(out), encoding="utf-8")
    return changes


def check_arithmetic(problems: list[str]) -> int:
    checked = 0
    for name in ("README.md", "ROADMAP.md"):
        text = visible(read(ROOT / name))
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
    text = visible(read(README))
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
    registered = suite_count()
    lines = visible(read(README)).split("\n")
    reported: set[int] = set()

    def report(number: int, text: str, digits: str) -> None:
        if number in reported:
            return
        reported.add(number)
        if int(digits.replace(",", "")) != registered:
            problems.append(
                f"README.md:{number}: {text.strip()};"
                f" tests/test_suite.cpp and tests/suites/ hold {registered}"
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


# ---------------------------------------------------------------------------
# The formats, and the cases they are trusted for
# ---------------------------------------------------------------------------

SELF_TEST_FIGURES = {
    "unit_suites": 260,
    "js.pass": 1137,
    "js.total": 1156,
    "dom.pass": 678,
    "dom.total": 678,
    "probe.pass": 90,
    "probe.total": 90,
    "t262.default.before": 12657,
    "t262.default.before_total": 15686,
    "t262.default.now": 13654,
    "t262.default.total": 15686,
    "t262.full.pass": 31161,
    "t262.full.total": 47967,
    "url.pass": 1188,
    "url.total": 1188,
    "wpt.pass": 9567,
    "wpt.total": 9824,
}

SELF_TEST_CASES = [
    # (figure, shape, what the claim must read)
    ("unit_suites", "int", "260"),
    ("unit_suites", "pair", "260/260"),
    ("js", "frac_pct", "1137 / 1156 (98.4%)"),
    ("js", "frac", "1137 / 1156"),
    ("dom", "frac", "678 / 678"),
    ("js", "pct", "98.4%"),
    ("js.pass", "num", "1,137"),
    ("probe.pass", "pair", "90/90"),
    ("t262.full.pass", "num", "31,161"),
    ("t262.full.pass", "of", "31,161 of 47,967"),
    ("t262.full", "pct", "65.0%"),
    ("t262.default", "pct", "87.0%"),
    ("t262.default.before", "num", "12,657"),
    ("t262.default.before_pct", "pct", "80.7%"),
    ("url", "frac_pct", "1188 / 1188 (100%)"),
    ("wpt", "frac", "9567 / 9824"),
    ("wpt.pass", "num", "9,567"),
]


def self_test_suite_count() -> int:
    """A suite count that is behind: whose it is to say so, and to rewrite."""
    import tempfile
    global DOCUMENTS
    failures = 0
    kept = DOCUMENTS
    with tempfile.TemporaryDirectory() as scratch:
        page = Path(scratch) / "README.md"
        text = "| Engine unit tests | 258<!--metrics:unit_suites/int--> suites |\n" \
               "| JS | **1137 / 1156 (98.4%)<!--metrics:js/frac_pct-->** |\n"
        page.write_text(text, encoding="utf-8")
        DOCUMENTS = (page,)
        try:
            problems: list[str] = []
            behind: list[str] = []
            check_markers(problems, SELF_TEST_FIGURES, behind)
            if problems or len(behind) != 1:
                print(f"  FAIL  a suite count that is behind: {len(problems)} failure(s) and"
                      f" {len(behind)} note(s), not 0 and 1")
                failures += 1
            problems = []
            check_markers(problems, SELF_TEST_FIGURES)
            if len(problems) != 1:
                print(f"  FAIL  --suites: a suite count that is behind is {len(problems)} failure(s), not 1")
                failures += 1
            write_markers(SELF_TEST_FIGURES, set(), [], False, suites=False)
            if page.read_text(encoding="utf-8") != text:
                print("  FAIL  --write without --suites rewrote the suite count")
                failures += 1
            write_markers(SELF_TEST_FIGURES, set(), [], False, suites=True)
            if "| 260<!--metrics:unit_suites/int-->" not in page.read_text(encoding="utf-8"):
                print("  FAIL  --write --suites did not rewrite the suite count")
                failures += 1
        finally:
            DOCUMENTS = kept
    return failures


def self_test_own_files() -> int:
    """A suite that is a file of its own is counted with the listed ones."""
    import tempfile
    with tempfile.TemporaryDirectory() as scratch:
        source = Path(scratch) / "test_suite.cpp"
        directory = Path(scratch) / "suites"
        directory.mkdir()
        source.write_text(
            "TEST(one) {\n}\nTEST(two) {\n}\nstatic void test_three() {}\n"
            "int main() {\n    RUN_TEST(one);\n    RUN_TEST(two);\n    RUN_TEST(three);\n"
            "    // RUN_TEST(not_this);\n}\n", encoding="utf-8")
        (directory / "218-a.cpp").write_text("// words\nTEST(four) {\n    assert(TEST(1));\n}\n", encoding="utf-8")
        (directory / "219-b.cpp").write_text("static int helper() { return 1; }\nTEST(five) { helper(); }\n",
                                             encoding="utf-8")
        (directory / "README.md").write_text("TEST(not_a_suite) {\n", encoding="utf-8")
        counted = suite_count(source, directory)
    if counted != 5:
        print(f"  FAIL  three listed suites and two files of their own are {counted} suites, not 5")
        return 1
    return 0


def self_test() -> int:
    failures = 0
    for name, shape, expected in SELF_TEST_CASES:
        written, _ = claim_text(name, shape, SELF_TEST_FIGURES)
        if written != expected:
            print(f"  FAIL  {name}/{shape}: {written!r}, not {expected!r}")
            failures += 1
    # The shapes that have to refuse: a figure with no total cannot be a
    # fraction, an unknown figure cannot be anything, and neither can an
    # unknown shape. A check that answers confidently to these is the check
    # that would rewrite a document wrongly and leave it looking right.
    for name, shape in (
        ("unit_suites", "frac"),      # no total
        ("js.total", "frac_pct"),     # a total is not a score
        ("no_such_figure", "int"),
        ("js", "percentage"),
    ):
        try:
            claim_text(name, shape, SELF_TEST_FIGURES)
        except UnknownFigure:
            continue
        print(f"  FAIL  {name}/{shape}: answered instead of refusing")
        failures += 1
    if first_number("1,137 / 1,156 (98.4%)") != 1137:
        print("  FAIL  first_number does not read a thousands separator")
        failures += 1
    failures += self_test_suite_count()
    failures += self_test_own_files()
    total = len(SELF_TEST_CASES) + 4 + 1 + 4 + 1
    if failures:
        print(f"\ncheck_numbers --self-test: {failures} failure(s)")
        return 1
    print(f"check_numbers --self-test: {total} case(s), 0 failure(s)")
    return 0


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()
    write = "--write" in argv
    allow_lowering = "--scores" in argv
    strict_suites = "--suites" in argv
    values, measured = figures()
    if write:
        print("check_numbers: refreshing the figures the documents publish")
        lowered: list[str] = []
        changes = write_floors(values, lowered) + write_markers(values, measured, lowered, allow_lowering,
                                                                suites=strict_suites)
        for change in changes:
            print(f"  {change}")
        for problem in lowered:
            print(f"  not written: {problem}")
        print()

    arithmetic: list[str] = []
    percentages = check_arithmetic(arithmetic)

    denominators: list[str] = []
    corpora = check_denominators(denominators)

    suites: list[str] = []
    registered = check_suite_count(suites)

    marked: list[str] = []
    behind: list[str] = []
    marks = check_markers(marked, values, None if strict_suites else behind)
    if not strict_suites:
        # Behind is a note: see "suite count" at the top of this file.
        behind = suites + behind
        suites = []

    floors: list[str] = []
    floor_count = check_floors(floors, values)

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
        print(f"  FAIL  tests/test_suite.cpp and tests/suites/ hold {registered} suites")
    elif behind:
        # No count of the places: the two checks above read the same lines, and
        # `len(behind)` said ten where README.md has six.
        print(f"  note  tests/test_suite.cpp and tests/suites/ hold {registered} suites and README.md says another"
              f" number: leave it, the landing session refreshes that count"
              f" (AGENTS.md, \"Working together\")")
    else:
        print(f"  ok    tests/test_suite.cpp and tests/suites/ hold {registered} suites, as README.md says")

    if marked:
        print(f"  FAIL  {len(marked)} marked claim(s) disagree with the tree or the ledger")
    else:
        print(f"  ok    {marks} marked claim(s) match docs/metrics.json and the tree")
    if floors:
        print(f"  FAIL  {len(floors)} floor(s) in ci.yml or the Makefile disagree with the ledger")
    else:
        print(f"  ok    ci.yml and the Makefile: {floor_count} floor(s) match the ledger")

    problems = arithmetic + denominators + suites + marked + floors
    if problems:
        print()
        for problem in problems:
            print(f"  {problem}")
        print(f"\ncheck_numbers: {len(problems)} failure(s)")
        return 1
    print("\ncheck_numbers: 0 failure(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
