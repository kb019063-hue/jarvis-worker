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
import tempfile
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


def git_show(ref: str, path: str) -> str | None:
    """The content of `path` as of `ref`, or None when it is not there."""
    result = subprocess.run(("git", "show", f"{ref}:{path}"), cwd=ROOT,
                            capture_output=True, text=True)
    return result.stdout if result.returncode == 0 else None


def git_ls(ref: str, directory: str) -> list[str]:
    """The markdown files under `directory` as of `ref`."""
    result = subprocess.run(("git", "ls-tree", "-r", "--name-only", ref, "--", directory),
                            cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        return []
    return [line for line in result.stdout.splitlines() if line.endswith(".md")]


def scan_files(ref: str | None = None) -> dict[str, tuple[list[int], list[int]]]:
    """What the repository says, either as it lies on disk or as of a ref.

    With no ref the working tree is read, which is what an agent about to
    write needs: it includes the file it has just added and not committed.
    `tools/ci/check_pr.py` passes `main`, because the question it answers is
    whether a number is taken on `main`, and a sandbox's clone can be behind
    it. The two used to be answered by two parsers — one here and one in
    that file — which agreed until they did not.
    """
    found: dict[str, tuple[list[int], list[int]]] = {}
    read = (lambda path: git_show(ref, path)) if ref else (
        lambda path: (ROOT / path).read_text(encoding="utf-8", errors="replace")
        if (ROOT / path).is_file() else None
    )
    for name in PROSE_DOCS:
        text = read(name)
        if text is not None:
            found[name] = scan_text(text)
    docs_dir = ROOT / "docs"
    listed = git_ls(ref, "docs/") if ref else (
        sorted(str(p.relative_to(ROOT)) for p in docs_dir.rglob("*.md"))
        if docs_dir.is_dir() else []
    )
    if listed:
        phases: list[int] = []
        findings: list[int] = []
        for rel in listed:
            name = rel.rsplit("/", 1)[-1]
            parent = rel.rsplit("/", 2)[-2] if "/" in rel else ""
            # "docs/phases/56-cross-origin-window-reflection.md" names a phase
            # and "docs/findings/69-....md" names a finding. The number is in
            # the name and not only in the prose, because the write-up is an
            # "# NN. ..." heading after the move and nothing else in it has to
            # say its own number.
            m = PHASE_FILE_RE.match(name)
            if m:
                if parent == "phases":
                    phases.append(int(m.group(1)))
                elif parent == "findings":
                    findings.append(int(m.group(1)))
            text = read(rel)
            if text is None:
                continue
            file_phases, file_findings = scan_text(text)
            phases.extend(file_phases)
            findings.extend(file_findings)
        if phases or findings:
            found["docs/"] = (phases, findings)
    return found


def scan_state(ref: str | None = None) -> tuple[int, int, str]:
    """The durable floors. Missing or broken state is a floor of zero, not an error."""
    raw_text = git_show(ref, "docs/state.json") if ref else (
        STATE.read_text(encoding="utf-8") if STATE.is_file() else None
    )
    if raw_text is None:
        return 0, 0, "docs/state.json (absent — floor 0)"
    try:
        raw = json.loads(raw_text)
    except ValueError as e:
        print(f"warning: docs/state.json unreadable ({e}); floor 0", file=sys.stderr)
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


def collect_sets(ref: str | None = None, with_prs: bool = True) -> tuple[set[int], set[int], dict[str, tuple[int, int]], str | None]:
    """Every phase and finding number taken, and where the highest of each came from.

    Returns (phases, findings, per-source maxima, gh warning). `tools/ci/check_pr.py`
    asks this for the *set*, not only the next free number, because it has to say
    whether a number a pull request has claimed is already somebody else's — and
    that is the same question, so it is answered here rather than a second time
    in that file.

    `ref` is a git ref to read instead of the working tree: that caller passes
    `main`. With a ref the open pull requests are still counted, since a claim
    is a claim whether or not it has landed — unless `with_prs` is false, which
    `tools/ci/check_pr.py` asks for when it wants what `main` alone has, so
    that it does not tell a pull request that its own number is taken.
    """
    sources: dict[str, tuple[int, int]] = {}
    phases: set[int] = set()
    findings: set[int] = set()

    for name, (file_phases, file_findings) in scan_files(ref).items():
        phases.update(file_phases)
        findings.update(file_findings)
        sources[name] = (max(file_phases, default=0), max(file_findings, default=0))

    floor_phase, floor_finding, floor_name = scan_state(ref)
    sources[floor_name] = (floor_phase, floor_finding)
    # Everything up to a floor counts as taken: that is what a floor means.
    phases.update(range(1, floor_phase + 1))
    findings.update(range(1, floor_finding + 1))

    warning = None
    if with_prs:
        pr_sources, warning = scan_open_prs()
        for name, (pr_phases, pr_findings) in pr_sources.items():
            phases.update(pr_phases)
            findings.update(pr_findings)
            sources[name] = (max(pr_phases, default=0), max(pr_findings, default=0))

    return phases, findings, sources, warning


def written(ref: str | None = None) -> tuple[set[int], set[int]]:
    """The phase and finding numbers some file says: no floor, no pull request.

    "Taken" and "written" are two questions. A floor makes every number up to
    it taken, so that none of them is handed out again; it does not put a
    write-up of that number on `main`. `tools/ci/check_pr.py` asked the first
    question and reported the answer as the second: after the floors rose to
    66 and 72 it told #63 (Phase 62, finding 68) and #73 (Phase 58, finding
    64), both open and both holding numbers no file has, that their numbers
    were "on main already", and that they were not ready to merge.
    """
    phases: set[int] = set()
    findings: set[int] = set()
    for file_phases, file_findings in scan_files(ref).values():
        phases.update(file_phases)
        findings.update(file_findings)
    return phases, findings


def collect(ref: str | None = None) -> tuple[int, int, dict[str, tuple[int, int]], str | None]:
    """Returns (next_phase, next_finding, per-source maxima, gh warning)."""
    phases, findings, sources, warning = collect_sets(ref)
    return max(phases, default=0) + 1, max(findings, default=0) + 1, sources, warning


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


def ref_cases() -> list[tuple[bool, str, str]]:
    """Does reading a ref work, and does it find the numbers in it.

    The bug these are here for: `tools/ci/check_pr.py` read `main` for
    `^## Phase (\\d+)` headings in ARCHITECTURE.md and `^## (\\d+)\\.` in
    AUDIT.md. When the write-ups moved to `docs/phases/` and `docs/findings/`
    those headings left the two documents, the reading found nothing, and the
    check went on reporting an empty set as "what main has taken". It was not
    an error — it was a silent wrong answer, which is worse. The numbers are
    in the filenames now; this asks whether the filenames are read.
    """
    cases: list[tuple[bool, str, str]] = []

    listed = git_ls("HEAD", "docs/")
    if not listed:
        cases.append((True, "HEAD has no docs/*.md", "nothing to compare against"))
        return cases

    cases.append((git_show("HEAD", "README.md") is not None,
                  "a ref is read out of git and not off the disk",
                  "" if git_show("HEAD", "README.md") is not None
                  else "git show HEAD:README.md returned nothing"))

    want_phases, want_findings = set(), set()
    for rel in listed:
        name = rel.rsplit("/", 1)[-1]
        parent = rel.rsplit("/", 2)[-2] if "/" in rel else ""
        m = PHASE_FILE_RE.match(name)
        if not m:
            continue
        if parent == "phases":
            want_phases.add(int(m.group(1)))
        elif parent == "findings":
            want_findings.add(int(m.group(1)))

    got_phases, got_findings = scan_files("HEAD").get("docs/", ([], []))
    got_phases, got_findings = set(got_phases), set(got_findings)
    missing_p = sorted(want_phases - got_phases)
    missing_f = sorted(want_findings - got_findings)
    cases.append((not missing_p,
                  f"every phase write-up on HEAD is counted ({len(want_phases)} of them)",
                  f"missing: {missing_p}" if missing_p else ""))
    cases.append((not missing_f,
                  f"every finding write-up on HEAD is counted ({len(want_findings)} of them)",
                  f"missing: {missing_f}" if missing_f else ""))

    no_prs = collect_sets("HEAD", with_prs=False)
    with_prs = collect_sets("HEAD", with_prs=True)
    cases.append((set(no_prs[0]) <= set(with_prs[0]) and set(no_prs[1]) <= set(with_prs[1]),
                  "--no-prs only ever takes numbers away",
                  "the open pull requests added nothing, which is fine, or took some away"))

    written_phases, written_findings = written("HEAD")
    floor_phase, floor_finding, _ = scan_state("HEAD")
    beyond_p = sorted(n for n in set(no_prs[0]) - written_phases if n > floor_phase)
    beyond_f = sorted(n for n in set(no_prs[1]) - written_findings if n > floor_finding)
    cases.append((written_phases <= set(no_prs[0]) and written_findings <= set(no_prs[1])
                  and not beyond_p and not beyond_f,
                  "what a file says is taken, and what is taken beyond that is at or below a floor",
                  f"taken, in no file and above the floor: phases {beyond_p}, findings {beyond_f}"
                  if beyond_p or beyond_f else ""))
    return cases


def floor_cases() -> list[tuple[bool, str, str]]:
    """A floor makes a number taken. It does not make it written.

    Asked of a tree made for the question: a floor of 10 and 12, one phase
    write-up numbered 3 that cites finding 4, and nothing else.
    """
    global ROOT, STATE
    kept = ROOT, STATE
    with tempfile.TemporaryDirectory() as made:
        ROOT = Path(made)
        STATE = ROOT / "docs" / "state.json"
        (ROOT / "docs" / "phases").mkdir(parents=True)
        STATE.write_text('{"phase_floor": 10, "finding_floor": 12}', encoding="utf-8")
        (ROOT / "docs" / "phases" / "03-a-write-up.md").write_text(
            "# 3. A write-up\n\nIt closes finding 4.\n", encoding="utf-8")
        try:
            taken_phases, taken_findings, _sources, _warning = collect_sets(None, with_prs=False)
            written_phases, written_findings = written(None)
        finally:
            ROOT, STATE = kept
    return [
        (taken_phases == set(range(1, 11)) and taken_findings == set(range(1, 13)),
         "every number up to a floor is taken",
         f"taken: phases {sorted(taken_phases)}, findings {sorted(taken_findings)}"),
        (written_phases == {3} and written_findings == {4},
         "only what a file says is written",
         f"written: phases {sorted(written_phases)}, findings {sorted(written_findings)}"),
    ]


def self_test() -> int:
    failed = 0

    print(f"parsing: {len(SELF_TEST_CASES)} cases")
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

    ref_results = ref_cases()
    print(f"\nreading a ref: {len(ref_results)} cases")
    for ok, name, detail in ref_results:
        if not ok:
            failed += 1
        print(f"  [{'ok  ' if ok else 'FAIL'}] {name}")
        if detail:
            print(f"         {detail}")

    floor_results = floor_cases()
    print(f"\na floor: {len(floor_results)} cases")
    for ok, name, detail in floor_results:
        if not ok:
            failed += 1
        print(f"  [{'ok  ' if ok else 'FAIL'}] {name}")
        print(f"         {detail}")

    total = len(SELF_TEST_CASES) + len(ref_results) + len(floor_results)
    print(f"\n{total - failed}/{total} pass")
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
    ap.add_argument("--taken", action="store_true",
                    help="every number taken, not only the next free one "
                         "(what tools/ci/check_pr.py asks for)")
    ap.add_argument("--ref", metavar="REF",
                    help="read the repository as of REF (git show / ls-tree) "
                         "instead of the working tree; the open pull requests "
                         "are counted either way")
    ap.add_argument("--no-prs", action="store_true",
                    help="leave the open pull requests out: what the "
                         "repository has landed, and nothing claimed")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    if args.taken:
        phases, findings, sources, warning = collect_sets(args.ref, not args.no_prs)
        written_phases, written_findings = written(args.ref)
        phase_floor, finding_floor, _ = scan_state(args.ref)
        print(json.dumps({
            "phases": sorted(phases),
            "findings": sorted(findings),
            # What a file says, apart from what a floor or a pull request
            # adds: "is this number on main" is asked of these two.
            "written_phases": sorted(written_phases),
            "written_findings": sorted(written_findings),
            "phase_floor": phase_floor,
            "finding_floor": finding_floor,
            "next_phase": max(phases, default=0) + 1,
            "next_finding": max(findings, default=0) + 1,
            "warning": warning,
        }, indent=2))
        if warning:
            print(f"warning: {warning}", file=sys.stderr)
            return 1
        return 0

    phase, finding, sources, warning = collect(args.ref)

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
