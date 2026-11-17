#!/usr/bin/env python3
"""Say what failed in a CI step as annotations on the job.

A job's log is served by another host than the API, and not every place a
change is written in can reach it. The annotations of a job can always be
read:

    gh api repos/OWNER/REPO/check-runs/JOB_ID/annotations --jq '.[].message'

(JOB_ID from `gh run view RUN --json jobs`.) So a step that may fail keeps
its output in a file (`... 2>&1 | tee "$RUNNER_TEMP/NAME.log"`), and a step
with `if: failure()` runs this script on the files. It prints, for the first
file that shows a failure:

  1. the lines that led up to make's first `***` line (or, without one, the
     end of the file),
  2. the first report of a sanitizer, if there is one,
  3. every other line that looks like a failure, up to a limit,

as `::error` workflow commands, which the runner turns into annotations. A
step shows at most ten of a kind, so the text is cut into at most nine
pieces. The script always exits 0: it explains a failure, it is not a check.

    python3 tools/ci/annotate_failure.py LOG [LOG...]
    python3 tools/ci/annotate_failure.py --plain LOG     # the same text, as it is
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

LINE_WIDTH = 260          # characters kept of one line
BEFORE_MAKE_ERROR = 60    # lines kept before make's first `***`
REPORT_LINES = 45         # lines kept of the first sanitizer report
OTHER_LINES = 40          # other lines that look like a failure
PIECE = 3000              # characters in one annotation
PIECES = 9                # annotations (a step shows ten errors)

MAKE_ERROR = re.compile(r"^make(\[\d+\])?: \*\*\* ")
SANITIZER = re.compile(r"ERROR: (Address|Thread|Leak|Memory)Sanitizer|ERROR: libFuzzer|runtime error: ")
LOOKS_LIKE_FAILURE = re.compile(
    r"Assertion .* failed|\bFAIL(ED|URE)?\b|\bfailed\b|\bfinding\(s\)|\bcrash|\btimeout|timed out|"
    r"Traceback \(most recent call last\)|^\S*Error: |\berror: |below the floor|"
    r"Segmentation fault|Aborted|core dumped|SUMMARY: ",
    re.IGNORECASE)
# Lines that say a count of failures is zero are not failures.
NOTHING_FAILED = re.compile(r"\b0 (failed\b|failures?\b|finding\(s\)|crash(es)?\b|timeouts?\b|differ\b)", re.IGNORECASE)
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def read_lines(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8", errors="replace")
    return [ANSI.sub("", line).rstrip()[:LINE_WIDTH] for line in text.splitlines()]


def explain(lines: list[str]) -> list[str] | None:
    """The lines worth showing, or None when the file shows no failure."""
    first_make_error = next((i for i, line in enumerate(lines) if MAKE_ERROR.search(line)), None)
    first_report = next((i for i, line in enumerate(lines) if SANITIZER.search(line)), None)
    suspects = [i for i, line in enumerate(lines)
                if LOOKS_LIKE_FAILURE.search(line) and not NOTHING_FAILED.search(line)]
    if first_make_error is None and first_report is None and not suspects:
        return None

    shown: set[int] = set()
    out: list[str] = []

    def take(title: str, indexes: range | list[int]) -> None:
        chosen = [i for i in indexes if 0 <= i < len(lines) and i not in shown]
        if not chosen:
            return
        out.append(f"--- {title}")
        for i in chosen:
            shown.add(i)
            out.append(f"{i + 1}: {lines[i]}")

    if first_make_error is not None:
        take(f"before make's first error (line {first_make_error + 1} of {len(lines)})",
             range(first_make_error - BEFORE_MAKE_ERROR, first_make_error + 1))
        # Every later `***` line: which targets gave up, innermost first.
        take("make's other error lines",
             [i for i, line in enumerate(lines) if MAKE_ERROR.search(line)][:10])
    else:
        take(f"the end of the output ({len(lines)} lines)", range(len(lines) - BEFORE_MAKE_ERROR, len(lines)))
    if first_report is not None:
        take(f"the first sanitizer report (line {first_report + 1})",
             range(first_report - 3, first_report + REPORT_LINES))
    take("other lines that look like a failure", suspects[:OTHER_LINES])
    return out


def pieces(lines: list[str]) -> list[str]:
    result: list[str] = []
    current = ""
    for line in lines:
        if current and len(current) + len(line) + 1 > PIECE:
            result.append(current)
            current = ""
        current += ("\n" if current else "") + line
    if current:
        result.append(current)
    if len(result) > PIECES:
        result = result[:PIECES - 1] + [result[PIECES - 1] + "\n--- (more was cut here)"]
    return result


def escape_data(text: str) -> str:
    return text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def escape_property(text: str) -> str:
    return escape_data(text).replace(":", "%3A").replace(",", "%2C")


def main() -> int:
    arguments = sys.argv[1:]
    plain = "--plain" in arguments
    paths = [Path(a) for a in arguments if a != "--plain"]
    if not paths:
        print(__doc__, file=sys.stderr)
        return 0
    explained = False
    for path in paths:
        if not path.is_file():
            continue
        try:
            found = explain(read_lines(path))
        except OSError as error:
            print(f"::warning::annotate_failure: cannot read {path}: {error}")
            continue
        if found is None:
            continue
        explained = True
        parts = pieces(found)
        for number, part in enumerate(parts, 1):
            title = f"{path.name}, part {number} of {len(parts)}"
            if plain:
                print(f"===== {title}\n{part}")
            else:
                print(f"::error title={escape_property(title)}::{escape_data(part)}")
    if not explained and not plain:
        names = ", ".join(p.name for p in paths)
        print(f"::warning::annotate_failure: nothing that looks like a failure in {escape_data(names)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
