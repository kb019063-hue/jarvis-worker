#!/usr/bin/env python3
"""The next free phase number and the next free finding number.

Why this exists
---------------
`AGENTS.md` used to tell an agent to work the next phase number out "from the
phase table in ARCHITECTURE.md" and the next finding number from `AUDIT.md`.
That asked an agent to read about 660 KB of markdown in order to extract two
integers, and it was still racy: two agents that looked at the same moment
took the same number, because neither could see the other.

This reads the same sources an agent would — the documents in the tree, the
`docs/phases/` write-ups, `docs/state.json`, and the open pull requests, which
are claims no file in the tree knows about yet — and prints the two numbers.

    python3 tools/next_ids.py             # what to claim
    python3 tools/next_ids.py --sources   # and where each number came from
    python3 tools/next_ids.py --json      # for scripts and CI
    python3 tools/next_ids.py --pr-body   # the block a WIP pull request opens with
    python3 tools/next_ids.py --self-test # the cases the parsing is trusted for

It never writes, and it reserves nothing. A number is allocated by claiming a
pull request, not by running this; two agents that run it in the same second
and both open their pull request afterwards will still collide. What it does
is make the collision visible in `gh pr list` instead of hidden in 660 KB.

Sources, and why each is read
-----------------------------
`ARCHITECTURE.md`, `AUDIT.md`, `README.md`, `ROADMAP.md`, `AGENTS.md`
    Phase and finding numbers mentioned anywhere in the prose.
`docs/phases/*.md`
    Phase write-ups that no longer live in `ARCHITECTURE.md`.
`docs/state.json`
    A durable floor, so a number claimed by a pull request that was later
    closed without merging is never handed out again. Bumped by hand in the
    commit that retires numbers; there is no automatic bump.
`gh pr list --state open`
    The live claims. Skipped with a warning if `gh` is missing or fails, in
    which case the answer is a lower bound and the pull requests must be
    checked by hand.

The answer is `max(every number found, the floor) + 1` for each of the two.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATE = ROOT / "docs" / "state.json"

# "Phase 61", "Phase 61, finding 67", "phases 42 and 43"
PHASE_RE = re.compile(r"\bphases?\s+((?:\d+\s*(?:,|and|&)?\s*)+)", re.IGNORECASE)
# "finding 67", "AUDIT.md, finding 57", "findings 43, 44 & 60"
FINDING_RE = re.compile(r"\bfindings?\s+((?:\d+\s*(?:,|and|&)?\s*)+)", re.IGNORECASE)
# "## 56. ✅ CSS Color 4/5 ..." — a finding that states no number in its prose
AUDIT_HEADING_RE = re.compile(r"^##\s+(\d+)\.", re.MULTILINE)
# "## Phase 55 Architecture: ..."
ARCH_HEADING_RE = re.compile(r"^##\s+Phase\s+(\d+)\b", re.MULTILINE | re.IGNORECASE)
# "docs/phases/55-regexp-flags.md"
PHASE_FILE_RE = re.compile(r"^(\d+)[-_]")

# A phase or finding number is small. Anything larger, in a list that starts
# with one, is a count that wandered in from the prose.
MAX_ID = 999

# A comma between digits with exactly three digits after it and no digit beyond
# that is a thousands separator ("10,835"), not a list separator ("43, 44").
THOUSANDS_SEP_RE = re.compile(r"(?<=\d),(?=\d{3}(?!\d))")

PROSE_DOCS = ("AGENTS.md", "README.md", "ROADMAP.md", "ARCHITECTURE.md", "AUDIT.md")


def _numbers(match_text: str) -> list[int]:
    """The numbers of a matched list, with counts that wandered in dropped.

    Two things in the prose are not identifiers and must not be read as one:

    * a thousands separator — README.md has "before Phase 59, 10,835 / 69.1%",
      where 10,835 is one count and not the two numbers 10 and 835; and
    * a count that runs on after a comma list — the same file has "1,127 before
      Phase 28, 5421 before Phase 47", where 5421 is a WPT subtest count.

    A list whose first element is too large to be an identifier is dropped
    whole; otherwise only the elements that could be identifiers are kept.
    """
    text = THOUSANDS_SEP_RE.sub("", match_text)
    ns = [int(n) for n in re.findall(r"\d+", text)]
    if not ns or ns[0] > MAX_ID:
        return []
    return [n for n in ns if n <= MAX_ID]


def _dedupe(ns: list[int]) -> list[int]:
    """Order-preserving dedupe.

    A heading can be matched twice, once as prose and once by the heading
    pattern. Every caller only ever wants the maximum, so the duplicate is
    harmless — it is dropped to keep `--json` and the self-test readable.
    """
    return list(dict.fromkeys(ns))


def scan_text(text: str) -> tuple[list[int], list[int]]:
    """Every phase and finding number mentioned in a piece of text."""
    phases: list[int] = []
    findings: list[int] = []
    for m in PHASE_RE.finditer(text):
        phases.extend(_numbers(m.group(1)))
    for m in FINDING_RE.finditer(text):
        findings.extend(_numbers(m.group(1)))
    for m in AUDIT_HEADING_RE.finditer(text):
        findings.append(int(m.group(1)))
    for m in ARCH_HEADING_RE.finditer(text):
        phases.append(int(m.group(1)))
    return _dedupe(phases), _dedupe(findings)


def scan_files() -> dict[str, tuple[list[int], list[int]]]:
    found: dict[str, tuple[list[int], list[int]]] = {}
    for name in PROSE_DOCS:
        path = ROOT / name
        if path.is_file():
            found[name] = scan_text(path.read_text(encoding="utf-8", errors="replace"))
    docs_dir = ROOT / "docs"
    if docs_dir.is_dir():
        phases: list[int] = []
        findings: list[int] = []
        for p in sorted(docs_dir.rglob("*.md")):
            # "docs/phases/56-cross-origin-window-reflection.md" names a phase;
            # the same two digits in a findings file do not.
            if p.parent.name == "phases":
                m = PHASE_FILE_RE.match(p.name)
                if m:
                    phases.append(int(m.group(1)))
            file_phases, file_findings = scan_text(
                p.read_text(encoding="utf-8", errors="replace")
            )
            phases.extend(file_phases)
            findings.extend(file_findings)
        if phases or findings:
            found["docs/"] = (phases, findings)
    return found


def scan_state() -> tuple[int, int, str]:
    """The durable floors. Missing or broken state is a floor of zero, not an error."""
    if not STATE.is_file():
        return 0, 0, "docs/state.json (absent — floor 0)"
    try:
        raw = json.loads(STATE.read_text(encoding="utf-8"))
    except (ValueError, OSError) as e:
        print(f"warning: {STATE.relative_to(ROOT)} unreadable ({e}); floor 0",
              file=sys.stderr)
        return 0, 0, "docs/state.json (unreadable — floor 0)"
    return (
        int(raw.get("phase_floor", 0)),
        int(raw.get("finding_floor", 0)),
        "docs/state.json (floor)",
    )


def scan_open_prs() -> tuple[dict[str, tuple[list[int], list[int]]], str | None]:
    """The live claims. Returns ({}, warning) if gh is unavailable."""
    try:
        out = subprocess.run(
            ["gh", "pr", "list", "--state", "open",
             "--limit", "200", "--json", "number,title,body"],
            cwd=ROOT, capture_output=True, text=True, timeout=60,
        )
    except (OSError, subprocess.SubprocessError) as e:
        return {}, f"gh did not run ({e})"
    if out.returncode != 0:
        detail = (out.stderr or "").strip().splitlines()
        return {}, f"gh failed: {detail[0] if detail else 'no message'}"
    try:
        prs = json.loads(out.stdout)
    except ValueError:
        return {}, "gh returned something that is not JSON"
    found: dict[str, tuple[list[int], list[int]]] = {}
    for pr in prs:
        text = f"{pr.get('title', '')}\n{pr.get('body', '') or ''}"
        phases, findings = scan_text(text)
        if phases or findings:
            found[f"PR #{pr.get('number', '?')}"] = (phases, findings)
    return found, None


def collect() -> tuple[int, int, dict[str, tuple[int, int]], str | None]:
    """Returns (next_phase, next_finding, per-source maxima, gh warning)."""
    sources: dict[str, tuple[int, int]] = {}

    for name, (phases, findings) in scan_files().items():
        sources[name] = (max(phases, default=0), max(findings, default=0))

    floor_phase, floor_finding, floor_name = scan_state()
    sources[floor_name] = (floor_phase, floor_finding)

    pr_sources, warning = scan_open_prs()
    for name, (phases, findings) in pr_sources.items():
        sources[name] = (max(phases, default=0), max(findings, default=0))

    phase = max((p for p, _ in sources.values()), default=0)
    finding = max((f for _, f in sources.values()), default=0)
    return phase + 1, finding + 1, sources, warning


# (text, expected phases, expected findings, why this case is here)
SELF_TEST_CASES: list[tuple[str, list[int], list[int], str]] = [
    ("1,127 before Phase 28, 5421 before Phase 47", [28, 47], [],
     "a scoreboard count in a comma list is dropped; the phase after it is kept"),
    ("before Phase 59, 10,835 / 69.1% before Phase 44", [59, 44], [],
     "a thousands-separated count is one number, not two"),
    ("Phase 61, finding 67", [61], [67],
     "the block a WIP pull request opens with"),
    ("findings 43, 44 & 60", [], [43, 44, 60],
     "a comma and ampersand list"),
    ("AUDIT.md, findings 48, 62", [], [48, 62],
     "how the task table cites findings"),
    ("## 56. ✅ CSS Color 4/5 spaces", [], [56],
     "a finding whose prose states no number"),
    ("## Phase 55 Architecture: Complete RegExp Flags", [55], [],
     "an ARCHITECTURE.md heading"),
    ("landed in Phase 50", [50], [], "a plain prose mention"),
    ("before Phase 47 read the last 16 encodings", [47], [],
     "a count standing after the mention"),
    ("phases 42 and 43", [42, 43], [], "an 'and' list"),
    ("no identifiers here at all", [], [], "nothing is found in nothing"),
]


def self_test() -> int:
    print(f"next_ids self-test: {len(SELF_TEST_CASES)} cases")
    failed = 0
    for text, want_phases, want_findings, why in SELF_TEST_CASES:
        got_phases, got_findings = scan_text(text)
        ok = got_phases == want_phases and got_findings == want_findings
        if not ok:
            failed += 1
        print(f"  [{'ok  ' if ok else 'FAIL'}] {text!r}")
        if not ok:
            print(f"         wanted phases={want_phases} findings={want_findings}")
            print(f"         got    phases={got_phases} findings={got_findings}")
        print(f"         {why}")
    print(f"\n{len(SELF_TEST_CASES) - failed}/{len(SELF_TEST_CASES)} pass")
    return 1 if failed else 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Print the next free phase and finding numbers."
    )
    ap.add_argument("--sources", action="store_true",
                    help="show the highest number each source contributes")
    ap.add_argument("--json", action="store_true", help="emit JSON")
    ap.add_argument("--pr-body", action="store_true",
                    help="emit the block a WIP pull request opens with")
    ap.add_argument("--self-test", action="store_true",
                    help="check the parsing against the cases it is trusted for")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    phase, finding, sources, warning = collect()

    if args.json:
        print(json.dumps({
            "next_phase": phase,
            "next_finding": finding,
            "sources": {k: {"phase": p, "finding": f} for k, (p, f) in sources.items()},
            "warning": warning,
        }, indent=2))
    elif args.pr_body:
        print("Task: <the row of the table>")
        print("Files: <what it will change>")
        print(f"Phase {phase}, finding {finding}")
    else:
        print(f"next phase:   {phase}")
        print(f"next finding: {finding}")
        if args.sources:
            print()
            width = max(len(k) for k in sources)
            for name, (p, f) in sorted(sources.items(), key=lambda kv: -kv[1][0]):
                print(f"  {name:<{width}}  phase {p:>4}   finding {f:>4}")
    if warning:
        print(f"warning: {warning}", file=sys.stderr)
        print("warning: the open pull requests were NOT checked, so the numbers "
              "above are a lower bound.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
