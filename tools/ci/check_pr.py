#!/usr/bin/env python3
"""What the open pull requests claim, and whether one is ready to be merged.

    python3 tools/ci/check_pr.py             # the claims, the numbers taken, the next free pair
    python3 tools/ci/check_pr.py claim N     # after opening #N: are its numbers and its files free
    python3 tools/ci/check_pr.py merge N     # before `gh pr merge N --merge`
    python3 tools/ci/check_pr.py --self-test # how a claim is read, on fixed texts; no network

AGENTS.md says what a claim is and what has to be true before a merge. Each
of those was missed at least once by an agent that had read it, because each
is a thing to look up at a moment when the work feels finished. This looks
them up. It reads; it changes nothing on GitHub. In the clone it fetches
`main` and the pull request it is asked about, and `merge N` may check a
tree out into a scratch directory and remove it again; no branch moves.

`claim N` fails (exit 1) when
  - #N does not begin with the `Task:`, `Files:` and `Phase P, finding F`
    lines;
  - its phase or its finding is on `main` already, or an open pull request
    with a lower number has it (of two that took a number at the same
    moment, the later one moves);
  - a file it names or has changed belongs to an open pull request with a
    lower number. The files every pull request changes (AGENTS.md lists
    them) do not count;
  - it is behind `main` and no longer merges into it. This was not asked
    until a "Land #N" was opened from a base 39 commits old: every question
    above answered "correct claim", and GitHub was the first thing to say
    CONFLICTING. A branch that is behind but still merges is a note, not a
    refusal, because most pull requests are behind by the time they land.

`merge N` fails when
  - the title still begins with `WIP:`;
  - GitHub does not report the pull request as mergeable;
  - `main` has moved since it was merged in, by more than documents and the
    checks that read them: whatever CI said, it said about another tree;
  - CI has not passed on the head commit. A head commit that was pushed with
    `[skip ci]` is accepted when CI passed on a commit before it and nothing
    but documents changed since, and a pull request that changes documents
    only needs no CI run of its own;
    (neither of the two is asked when the merge would put on `main`, file
    for file, the tree of a commit CI passed on: see "The same tree" below)
  - the checks that read documents fail on what would be merged (see below);
  - its phase or finding is on `main` already;
  - the number of unit suites README.md states is not the number in
    tests/test_suite.cpp.

Documents, and the 28 minutes they used to cost
------------------------------------------------
Until 18 November every commit on `main` sent every finished pull request
back for a whole CI run, a line of README.md like any other. On that morning
`main` moved eight times in three hours and twenty minutes; #86 merged
`main` six times after its work was done and #73 thirteen times in all, each
time for 28 minutes of CI, and the agents that did not wait merged behind
`main` instead (#76, #77, #84, #91; the last left `main` failing for 25
minutes).

Nothing in CI reads a document but four quick steps: `make check-numbers`,
and the three of the job "Documentation shape" (`tools/ci/check_docs.py` and
the self-tests of `tools/next_ids.py` and of this file). So when the only
difference between the tree CI passed on and the tree that would be merged
is in documents (`*.md` at the top of the tree, `docs/`, the `README.md` of
a test directory) or, on the side of `main`, in those checks themselves
(`tools/ci/`, `tools/next_ids.py`), every other step of CI would say again
what it said, and those four are run here instead: on the head commit, or
on the commit GitHub would make by merging, in a scratch worktree that is
removed afterwards. They take seconds.

The same tree
-------------
The plainest case of that rule: merging would put on `main` the very tree a
run passed on, file for file. It was met on 19 November by #118, which
carried the head commits of four finished pull requests while the agent of
one of them came back and was about to merge its own. Whichever of the two
is merged first, `main` moves, by commits the other has in it already, and
the tree of the other, merged, is what it was. Asking it for a second run
would be asking CI to read the same files again. So when `git merge-tree`
of `main` and the head gives the tree of a commit CI passed on (the head, or
one before it along the branch), that is said, and the four quick checks
are run against the new `main` as above. Two trees are the same when git
gives them the same name; nothing is compared by eye.

Everything else is asked of GitHub through `gh` (which has to be logged in),
and nothing of the local clone: a sandbox's clone is shallow, and a shallow
clone answers "is `main` in this commit" wrongly. The other exception is
the numbers `main` has: `tools/next_ids.py` reads them out of git, from the
commit that GitHub says `main` is at. Run it inside the repository, so that
`gh` knows which one is meant.

The one exception is the set of numbers taken, which comes from
`tools/next_ids.py --taken --ref main` — that tool reads `main` out of git
rather than off the disk, and it is the allocator the rest of the repository
uses, so the two cannot drift apart.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import NamedTuple

ROOT = Path(__file__).resolve().parent.parent.parent
NEXT_IDS = ROOT / "tools" / "next_ids.py"      # the one allocator

# AGENTS.md, "The files every pull request changes", and the two it leaves out.
SHARED = {
    "README.md", "ARCHITECTURE.md", "AUDIT.md", "AGENTS.md", "ROADMAP.md",
    "tests/test_suite.cpp", "Makefile", ".github/workflows/ci.yml",
}
# What no step of CI reads but the four checks named below: the documents at
# the top of the tree, everything under docs/, and the README.md of a test
# directory. Not every `.md`: tools/conformance/run_wpt.py fails for any file
# under tests/wpt/ that its manifest does not list, whatever it is called.
DOCUMENT = re.compile(r"^(?:[^/]+\.md|docs/.+|tests/[^/]+/README\.md)$")
# The documents, and the checks that read them.
QUICK = re.compile(r"^(?:[^/]+\.md|docs/.+|tests/[^/]+/README\.md|tools/ci/.+|tools/next_ids\.py)$")
# Those checks: the first step of CI's "Build & unit tests" and the three of
# "Documentation shape". `{main}` is the commit of `main` to compare with.
QUICK_CHECKS = (
    ("check_numbers.py", ("tools/ci/check_numbers.py",)),
    ("check_docs.py", ("tools/ci/check_docs.py", "--base", "{main}")),
    ("next_ids.py --self-test", ("tools/next_ids.py", "--self-test")),
    ("check_pr.py --self-test", ("tools/ci/check_pr.py", "--self-test")),
)
# "No phase, no finding:", "takes no phase or finding number", "is not a phase".
# A pull request that claims no numbers has to say that it claims none, rather
# than merely omit the line: silence is how a number gets forgotten, not how a
# numberless change announces itself.
NO_NUMBERS_RE = re.compile(
    r"no phase,? (?:no|or) finding|takes no phase|is not a phase|not a phase and takes no",
    re.IGNORECASE,
)
# What a file name in a `Files:` line can be followed by: the closing backtick,
# and the full stop, semicolon or colon of the sentence around it.
FILE_NAME_END_RE = re.compile(r"[`.;:\s]+$")
REPO = "repos/{owner}/{repo}"      # `gh api` fills the two in from the clone


def gh(*arguments: str, check: bool = True) -> str:
    result = subprocess.run(("gh",) + arguments, capture_output=True, text=True)
    if check and result.returncode != 0:
        sys.exit(f"gh {' '.join(arguments)}\n{result.stderr.strip()}")
    return result.stdout


def gh_json(*arguments: str):
    return json.loads(gh(*arguments))


def file_at(path: str, ref: str) -> str:
    """A file of the repository as it is at a commit or on a branch."""
    return gh("api", f"{REPO}/contents/{path}?ref={ref}", "-H", "Accept: application/vnd.github.raw")


def without_remarks(line: str) -> str:
    """A line of a `Files:` list without what is said in brackets.

    Brackets inside the brackets are part of the remark. They used not to be:
    of "`src/a.cpp` (the `f()` helper)" the first closing bracket ended the
    remark, "` helper)" stayed, and the name that was read was no file's. On
    20 November that hid a clash: #163 named `src/js/BuiltinsArray.cpp` that
    way thirty-one seconds after #161 had claimed it, and was told nothing.
    """
    kept, depth = [], 0
    for character in line:
        if character == "(":
            depth += 1
        elif character == ")":
            if depth == 0:
                break               # a bracket that closes nothing: read the line the old way
            depth -= 1
        elif depth == 0:
            kept.append(character)
    else:
        if depth == 0:
            return "".join(kept)
    return re.sub(r"\([^)]*\)", "", line)   # brackets that do not pair up


def claim_of(pr: dict) -> dict:
    """The claim a pull request makes: the three lines AGENTS.md asks for."""
    body = (pr.get("body") or "").replace("\r\n", "\n")
    head = body.split("\n")[:16]
    task = next((line for line in head if line.startswith("Task:")), None)
    # A change that takes no numbers says so, in place of the `Phase P, finding F`
    # line. AGENTS.md: "A change like this one -- documents, tooling, CI, nothing
    # that moves a scoreboard -- is not a phase and takes no phase or finding
    # number." #76 was merged saying "No phase, no finding:", and this used to
    # call it a missing claim, which is a wrong answer rather than an error: it
    # blocks work that announced itself correctly.
    no_numbers = NO_NUMBERS_RE.search(body) is not None
    numbers = next((m for m in (re.match(r"Phase (\d+), finding (\d+)\s*$", line) for line in head) if m), None)
    phase = int(numbers.group(1)) if numbers else None
    finding = int(numbers.group(2)) if numbers else None
    if numbers is None:
        # No claim line: what the title says is still a number somebody is using.
        in_title = re.search(r"\bPhase (\d+)\b", pr["title"])
        phase = int(in_title.group(1)) if in_title else None
        in_title = re.search(r"\b[Ff]inding (\d+)\b", pr["title"])
        finding = int(in_title.group(1)) if in_title else None

    # `Files:` and the lines that continue it, up to an empty line or the next of the three.
    named: set[str] = set()
    has_files_line = False
    collecting = False
    for line in head:
        if line.startswith("Files:"):
            has_files_line = collecting = True
            line = line[len("Files:"):]
        elif collecting and (not line.strip() or line.startswith("Task:") or re.match(r"Phase \d+", line)
                             or NO_NUMBERS_RE.search(line)):
            # The third line of a claim ends the list whichever of its two forms
            # it has. `No phase, no finding: ...` used to be read on as more
            # files, so two pull requests without numbers shared one called
            # "No phase", and the later was told to stay out of it.
            collecting = False
        if collecting:
            for part in re.split(r"[,;]", without_remarks(line)):
                # A name in backticks is the name, whatever is said around it
                # ("new `src/a.cpp`", "`src/a.cpp` -- the helper").
                quoted = re.search(r"`([^`]+)`", part)
                if quoted:
                    part = quoted.group(1)
                # "`docs/x.md`." at the end of a sentence is docs/x.md; a name
                # may begin with a full stop (.github/...), so only the end.
                part = FILE_NAME_END_RE.sub("", part.strip().lstrip("`").strip())
                if part:
                    named.add(part)
    changed = {entry["path"] for entry in pr.get("files") or []}
    return {
        "number": pr["number"],
        "title": pr["title"],
        "task": task[len("Task:"):].strip() if task else None,
        "files": named | changed,       # what it says it will change, and what it has changed
        "has_claim_lines": task is not None and has_files_line and (numbers is not None or no_numbers),
        "phase": phase,
        "finding": finding,
        "head": pr.get("headRefOid", ""),
    }


def same_files(mine: set[str], theirs: set[str]) -> list[str]:
    """Files two claims share. A name that ends in `/` is every file under it."""
    shared = set()
    for a in mine - SHARED:
        for b in theirs - SHARED:
            if a == b or (a.endswith("/") and b.startswith(a)) or (b.endswith("/") and a.startswith(b)):
                shared.add(a if len(a) >= len(b) else b)
    return sorted(shared)


def main_commit() -> str:
    """The commit `main` is at on GitHub, made sure to be in this clone.

    `tools/next_ids.py --ref` reads a ref out of the local clone, and the
    local `main` is where the clone was made. On 18 November that was one
    merge behind: asked for `main`, the allocator answered with floors of 66
    and 72 and no Phase 68, twenty minutes after #83 had put 68 and 74 on
    `main`.
    Whether a number has landed is the one question that must not be answered
    from before it landed, so the commit is asked of GitHub and fetched when
    the clone does not have it.
    """
    sha = commit("main")["sha"]

    def present() -> bool:
        return subprocess.run(("git", "cat-file", "-e", f"{sha}^{{commit}}"),
                              cwd=ROOT, capture_output=True).returncode == 0

    if not present():
        subprocess.run(("git", "fetch", "--quiet", "origin", "main"), cwd=ROOT, capture_output=True)
    if not present():
        sys.exit(f"main is at {sha[:7]} on GitHub, and this clone does not have that commit "
                 "and could not fetch it (git fetch origin main)")
    return sha


class Main(NamedTuple):
    """What `main` says about the two counters."""
    phases: set[int]            # taken: mentioned in a file, or at or below a floor
    findings: set[int]
    written_phases: set[int]    # has a write-up (tools/next_ids.py, `written`)
    written_findings: set[int]
    phase_floor: int
    finding_floor: int


def on_main() -> Main:
    """The phase and finding numbers `main` has taken, and those it has written.

    Asked of `tools/next_ids.py`, which is the one allocator in the
    repository. This used to parse ARCHITECTURE.md and AUDIT.md here with
    `^## Phase (\\d+)` and `^## (\\d+)\\.`, and quietly went blind when the
    phase and finding write-ups moved out of those two documents into
    `docs/phases/` and `docs/findings/`: every heading it looked for was
    gone, so it reported nothing taken and would have handed out numbers
    that were already somebody else's. One allocator, asked from both
    places, cannot disagree with itself like that.

    `main` and not the working tree, because the question is what `main`
    has and a sandbox's clone may be behind it; and `main` as GitHub has it,
    not the clone's branch of that name, for the same reason
    (`main_commit`). `--no-prs`, because the
    open claims are folded in separately below: without it, a pull request
    is told that the number it has just claimed is already taken — by
    itself.

    Taken and written are kept apart. Every number up to a floor of
    `docs/state.json` is taken, and every number a document mentions, so that
    the allocator never hands one out again, whether or not a write-up of it
    exists. This file used to call all of them "on main already": #63 (Phase
    62, finding 68) and #73 (Phase 58, finding 64) were open, with numbers
    that had no write-up, when the floors passed them, and were told "not
    ready" for a reason that was not true.
    """
    result = subprocess.run(
        [sys.executable, str(NEXT_IDS), "--taken", "--ref", main_commit(), "--no-prs"],
        cwd=ROOT, capture_output=True, text=True,
    )
    if result.returncode not in (0, 1) or not result.stdout.strip():
        # A warning about `gh` exits 1 with the numbers still printed, so 1 is
        # not failure here. Anything else is, and there is no honest answer to
        # give: saying "nothing is taken" would let a duplicate number through.
        sys.exit(f"could not ask {NEXT_IDS.name} what is taken:\n"
                 f"{result.stderr.strip() or result.stdout.strip()}")
    try:
        taken = json.loads(result.stdout)
    except ValueError:
        sys.exit(f"{NEXT_IDS.name} answered with something that is not JSON:\n{result.stdout[:400]}")
    if result.stderr.strip():
        print(f"note: {result.stderr.strip()}", file=sys.stderr)
    phases, findings = set(taken["phases"]), set(taken["findings"])
    return Main(phases, findings,
                set(taken.get("written_phases", phases)), set(taken.get("written_findings", findings)),
                int(taken.get("phase_floor", 0)), int(taken.get("finding_floor", 0)))


def open_claims() -> list[dict]:
    prs = gh_json("pr", "list", "--state", "open", "--limit", "100",
                  "--json", "number,title,body,headRefOid,files")
    return sorted((claim_of(pr) for pr in prs), key=lambda c: c["number"])


def commit(sha: str) -> dict:
    return gh_json("api", f"{REPO}/commits/{sha}")


def hours_since(stamp: str) -> float:
    then = datetime.strptime(stamp, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
    return (datetime.now(timezone.utc) - then).total_seconds() / 3600


def next_pair() -> str:
    """'Phase P, finding F': the next free pair, as the allocator gives it.

    This file used to work the pair out from the claims it had read, and a
    claim is three lines that may not be there. #98 had `Phase 79` in its
    title, no claim lines, and finding 85 in its text only: this file said
    the next free finding was 85, and `tools/next_ids.py`, which reads the
    whole text of every open pull request, said 86. One allocator, asked.
    """
    result = subprocess.run([sys.executable, str(NEXT_IDS), "--json"], cwd=ROOT, capture_output=True, text=True)
    try:
        answer = json.loads(result.stdout)
        return f"Phase {answer['next_phase']}, finding {answer['next_finding']}"
    except (ValueError, KeyError):
        return "ask `python3 tools/next_ids.py` (it gave no answer here)"


def numbers_of(claim: dict) -> str:
    """'Phase 5, finding 7', or what a claim without numbers is called."""
    return ", ".join(f"{kind} {claim[key]}" for kind, key in (("Phase", "phase"), ("finding", "finding"))
                     if claim[key] is not None) or "no phase, no finding"


def number_on_main(kind: str, number: int, main: Main) -> tuple[bool, str | None]:
    """(is a write-up of this number on main, what else there is to say about it)."""
    written, floor = ((main.written_phases, main.phase_floor) if kind == "Phase"
                      else (main.written_findings, main.finding_floor))
    if number in written:
        return True, None
    if number <= floor:
        return False, (f"{kind} {number} has no write-up on main. It is at or below the floor of docs/state.json "
                       f"({floor}), so tools/next_ids.py hands it out no more: it is this pull request's "
                       "if it was claimed while it was free")
    return False, None


class Report:
    def __init__(self) -> None:
        self.failed = False

    def ok(self, text: str) -> None:
        print(f"ok  {text}")

    def no(self, text: str) -> None:
        self.failed = True
        print(f"NO  {text}")

    def note(self, text: str) -> None:
        print(f"    {text}")


def claim_problems(claim: dict, claims: list[dict], main: Main) -> list[tuple[bool, str]]:
    """What is wrong with a claim: (does this pull request have to change, what)."""
    problems: list[tuple[bool, str]] = []
    if not claim["has_claim_lines"]:
        problems.append((True, "its text does not begin with the Task:, Files: and `Phase P, finding F` lines"))
    others = [c for c in claims if c["number"] != claim["number"]]
    for kind, key in (("Phase", "phase"), ("finding", "finding")):
        number = claim[key]
        if number is None:
            continue
        written, remark = number_on_main(kind, number, main)
        if written:
            problems.append((True, f"{kind} {number} is on main already"))
        elif remark:
            problems.append((False, remark))
        for other in others:
            if other[key] != number:
                continue
            if other["number"] > claim["number"]:
                problems.append((False, f"{kind} {number}: #{other['number']} says it too. #{other['number']} is the later one, and moves"))
            else:
                problems.append((True, f"{kind} {number}: #{other['number']} had it first. This one moves"))
    for other in others:
        both = same_files(claim["files"], other["files"])
        if not both:
            continue
        if other["number"] > claim["number"]:
            problems.append((False, f"#{other['number']}, opened later, names or changes {', '.join(both)} too: "
                                    "it is the one that has to stay out"))
        else:
            problems.append((True, f"#{other['number']} had {', '.join(both)} first: stay out of "
                                   f"{'it' if len(both) == 1 else 'them'}, or tell the person you work for"))
    return problems


def show_claims() -> int:
    claims = open_claims()
    main = on_main()
    print(f"main: phases up to {max(main.phases)}, findings up to {max(main.findings)}")
    for claim in claims:
        pushed = hours_since(commit(claim["head"])["commit"]["committer"]["date"])
        print(f"\n#{claim['number']}  {numbers_of(claim)}  (last commit {pushed:.1f} h ago)")
        print(f"    {claim['title'][:110]}")
        own = sorted(claim["files"] - SHARED)
        print("    files: " + (", ".join(own) if own else "(none)"))
        for blocking, problem in claim_problems(claim, claims, main):
            print(f"    {'!!' if blocking else '  '} {problem}")
    print(f"\nnext free: {next_pair()}")
    return 0


def staleness(head: str, main_sha: str) -> tuple[int, str]:
    """(how many commits of `main` the head does not have, why it cannot merge, or "").

    A pull request opened from a `main` that has since moved is not wrong by
    itself -- most are, and merging it is the point. What went wrong once is a
    "Land #N" opened from a base 39 commits old that no longer merged at all:
    it read as a correct claim from every angle this file looked at, because
    `claim N` only ever asked about numbers and file overlap. GitHub was the
    first thing to say CONFLICTING, and by then the session had spent the work.

    So `claim N` now asks the one question that would have caught it: would
    this merge? `git merge-tree` answers the way GitHub does, in the clone,
    with no checkout and no branch moved.
    """
    behind = 0
    counted = git("rev-list", "--count", f"{head}..{main_sha}")
    if counted.returncode == 0:
        try:
            behind = int(counted.stdout.strip() or "0")
        except ValueError:
            behind = 0
    if behind == 0:
        return 0, ""
    for _ in range(4):      # a sandbox's clone is shallow: go back until the two share an ancestor
        if git("merge-base", main_sha, head).returncode == 0:
            break
        if git("rev-parse", "--is-shallow-repository").stdout.strip() != "true":
            break
        git("fetch", "--quiet", "--deepen=500", "origin", "main")
    merged = git("merge-tree", "--write-tree", main_sha, head)
    if merged.returncode == 1:
        conflicting = sorted({line.split("\t")[-1] for line in merged.stdout.split("\n") if "\t" in line})
        named = ", ".join(conflicting[:4]) + (" and more" if len(conflicting) > 4 else "")
        return behind, (f"it does not merge with main any more ({named})" if named
                        else "it does not merge with main any more")
    return behind, ""


def staleness_problems(claim: dict, main_sha: str) -> list[tuple[bool, str]]:
    """Whether the branch still merges into `main`: (blocking, what).

    Kept apart from `claim_problems`, which is about numbers and files and is
    tested on claims that have no head and no `main`: this one asks git.
    """
    if not claim.get("head"):
        return []
    behind, why = staleness(claim["head"], main_sha)
    if behind == 0:
        return [(False, f"main is in the head commit {claim['head'][:7]}")]
    if why:
        return [(True, f"{why}: it is {behind} commit(s) behind main. Merge main in, "
                       f"or close it if what it was for has landed")]
    return [(False, f"{behind} commit(s) behind main, and it still merges")]


def check_claim(number: int) -> int:
    claims = open_claims()
    mine = next((c for c in claims if c["number"] == number), None)
    if mine is None:
        sys.exit(f"#{number} is not an open pull request")
    main = on_main()
    report = Report()
    for blocking, problem in claim_problems(mine, claims, main):
        (report.no if blocking else report.note)(problem)
    for blocking, problem in staleness_problems(mine, main_commit()):
        (report.no if blocking else report.ok)(problem)
    if not report.failed:
        report.ok(f"#{number}: {numbers_of(mine)}; no earlier open pull request has its numbers or its files")
    else:
        report.note(f"next free: {next_pair()}")
    return 1 if report.failed else 0


def check_runs(sha: str) -> tuple[str, list[str]]:
    """'passed', 'failed', 'running' or 'none' for a commit, and the checks that are not green."""
    runs = gh_json("api", f"{REPO}/commits/{sha}/check-runs?per_page=100").get("check_runs", [])
    if not runs:
        return "none", []
    # A job that was run again leaves both attempts: the last one of a name counts.
    latest: dict[str, dict] = {}
    for entry in sorted(runs, key=lambda r: r.get("started_at") or ""):
        latest[entry["name"]] = entry
    bad = [f"{r['name']}: {r['conclusion'] or r['status']}" for r in latest.values()
           if r["conclusion"] not in ("success", "skipped", "neutral")]
    if not bad:
        return "passed", []
    if any(r["status"] != "completed" for r in latest.values()):
        return "running", bad
    return "failed", bad


def delta(base: str, tip: str) -> tuple[int, list[str] | None]:
    """How many commits `tip` has that `base` has not, and the files they
    change. None for the files when GitHub lists so many that some may be
    missing: a caller that cannot see every file must not call them harmless.
    """
    comparison = gh_json("api", f"{REPO}/compare/{base}...{tip}")
    files = comparison.get("files") or []
    names = sorted({name for entry in files
                    for name in (entry["filename"], entry.get("previous_filename")) if name})
    return comparison["ahead_by"], (None if len(files) >= 300 else names)


def all_of(kind: re.Pattern[str], files: list[str] | None) -> bool:
    """Every file is of this kind. Not knowing the files is not knowing that."""
    return files is not None and all(kind.search(name) for name in files)


def some(files: list[str] | None, limit: int = 6) -> str:
    if files is None:
        return "more files than GitHub lists"
    if not files:
        return "nothing"
    return ", ".join(files[:limit]) + (f" and {len(files) - limit} more" if len(files) > limit else "")


class Evidence(NamedTuple):
    """What `merge N` found out about CI and `main`, before it decides."""
    head: str
    behind: int                     # commits of main that are not in the head
    main_files: list[str] | None    # the files those commits change
    pr_files: list[str] | None      # the files the pull request changes
    head_state: str                 # CI on the head: passed, failed, running, none
    head_bad: list[str]             # ...and the jobs that are not green
    ran_on: str | None              # the nearest commit of the branch with a CI run: the head, or one before it
    ran_state: str                  # ...and what that run says
    since: list[str] | None         # the files changed from that commit to the head
    same_tree: bool = False         # merged with main, the tree is the tree of `ran_on`, file for file


def ci_verdict(e: Evidence) -> tuple[list[tuple[bool, str]], bool]:
    """(the lines to report, whether the quick checks decide the rest).

    One rule, in three places: a step of CI says again what it said when
    nothing it reads has changed. So a new run is needed only when both sides
    of the merge changed something other than documents and the checks that
    read them. The quick checks are then run on the tree that would be merged,
    because that tree is not the one CI saw.
    """
    head = e.head[:7]
    lines: list[tuple[bool, str]] = []
    quick = False
    documents_only = all_of(DOCUMENT, e.pr_files)
    quick_only = all_of(QUICK, e.pr_files)

    # The same tree. The rule at its plainest: the merge would put on main the
    # very tree a run passed on, so no step has anything new to read. It is
    # what a pull request that carries other pull requests' heads meets when
    # one of them is merged by its own agent first (main has moved, by commits
    # the branch had already), and what a head meets that is a merge which
    # changed no file. One line answers both questions below.
    if e.same_tree and e.ran_on is not None and e.ran_state == "passed" and e.head_state != "failed":
        lines.append((True, f"CI passed on {e.ran_on[:7]}, and merged with main this is the tree of that commit, "
                            "file for file: whatever main has since, the branch had already. No new CI run for that"))
        return lines, True

    # main
    if e.behind == 0:
        lines.append((True, f"main is in the head commit {head}"))
    elif all_of(QUICK, e.main_files):
        quick = True
        lines.append((True, f"main has moved by {e.behind} commit(s) that change only documents and the checks "
                            f"that read them ({some(e.main_files)}): no new CI run for that"))
    elif quick_only:
        quick = True
        lines.append((True, f"main has moved by {e.behind} commit(s), and this pull request changes only documents "
                            f"and the checks that read them ({some(e.pr_files)}): no new CI run for that"))
    else:
        lines.append((False, f"main has moved: {e.behind} of its commits are not in {head}, and they change "
                             f"{some([f for f in e.main_files if not QUICK.search(f)] if e.main_files is not None else None)}. "
                             "Merge it, build, run the checks, push, and let CI pass on that commit"))

    # CI
    if e.head_state == "failed":
        lines.append((False, f"CI on {head}: " + "; ".join(e.head_bad)))
    elif documents_only:
        quick = True
        lines.append((True, f"it changes documents only ({some(e.pr_files)}): it needs no CI run of its own"))
    elif e.head_state == "passed":
        lines.append((True, f"CI passed on the head commit {head}"))
    elif e.head_state == "running":
        lines.append((False, f"CI on {head}: " + "; ".join(e.head_bad)))
    elif e.ran_on is None:
        lines.append((False, f"no CI run on {head} or on the 30 commits before it"))
    elif e.ran_state != "passed":
        lines.append((False, f"no CI run on {head}; the last one, on {e.ran_on[:7]}, {e.ran_state}"))
    elif all_of(DOCUMENT, e.since):
        quick = True
        lines.append((True, f"CI passed on {e.ran_on[:7]}, and since then only documents changed "
                            f"({some(e.since)}). Say so in the pull request"))
    elif quick_only and e.since is not None and not set(e.since) & {f for f in e.pr_files if not DOCUMENT.search(f)}:
        # `main` was merged into the branch since the run, which brought in
        # whatever `main` had. What had to pass on a runner is this pull
        # request's own copy of the checks, and that is as it was then.
        quick = True
        own = [f for f in e.pr_files if not DOCUMENT.search(f)]
        lines.append((True, f"CI passed on {e.ran_on[:7]}; since then main was merged in, and the checks this pull "
                            f"request changes ({some(own)}) are as they were. Say so in the pull request"))
    else:
        code = None if e.since is None else [f for f in e.since if not DOCUMENT.search(f)]
        lines.append((False, f"no CI run on {head}; since the last one, on {e.ran_on[:7]}, these changed: {some(code)}"))

    return lines, quick and all(ok for ok, _text in lines)


def fetched(ref: str) -> str | None:
    """A ref of GitHub's, fetched into the clone: its commit, or None."""
    done = subprocess.run(("git", "fetch", "--quiet", "origin", ref), cwd=ROOT, capture_output=True, text=True)
    if done.returncode != 0:
        return None
    found = subprocess.run(("git", "rev-parse", "FETCH_HEAD"), cwd=ROOT, capture_output=True, text=True)
    return found.stdout.strip() or None


def git(*arguments: str) -> subprocess.CompletedProcess[str]:
    identity = {"GIT_AUTHOR_NAME": "check_pr", "GIT_AUTHOR_EMAIL": "check_pr@localhost",
                "GIT_COMMITTER_NAME": "check_pr", "GIT_COMMITTER_EMAIL": "check_pr@localhost"}
    return subprocess.run(("git", *arguments), cwd=ROOT, capture_output=True, text=True,
                          env={**os.environ, **identity})


def tree_to_merge(number: int, head: str, main_sha: str, behind: int) -> tuple[str | None, str, str]:
    """(a commit with the tree `gh pr merge` would put on main, what to call it, why there is none).

    When `main` is in the head that is the head. Otherwise the two are merged
    here, with `git merge-tree`, which is what GitHub merges with; nothing is
    checked out and no branch moves. GitHub's own `refs/pull/N/merge` is not
    used: twenty minutes after `main` had moved it was still the merge with
    the `main` of before.
    """
    if fetched(f"refs/pull/{number}/head") != head:
        return None, "", f"could not fetch the head commit {head[:7]} to run the checks on"
    if behind == 0:
        return head, f"the head commit {head[:7]}", ""
    for _ in range(4):      # a sandbox's clone is shallow: go back until the two have an ancestor in common
        if git("merge-base", main_sha, head).returncode == 0:
            break
        if git("rev-parse", "--is-shallow-repository").stdout.strip() != "true":
            break
        git("fetch", "--quiet", "--deepen=500", "origin", "main", f"refs/pull/{number}/head")
    merged = git("merge-tree", "--write-tree", main_sha, head)
    if merged.returncode == 1:
        return None, "", "it does not merge with main without conflicts"
    if merged.returncode != 0 or not merged.stdout.split():
        return None, "", ("git could not merge it with main here to run the checks on "
                          "(it takes git 2.38 or later, and a clone that has their common ancestor): "
                          "merge main into the branch instead")
    made = git("commit-tree", merged.stdout.split()[0], "-p", main_sha, "-p", head,
               "-m", f"check_pr: what merging #{number} would put on main")
    if made.returncode != 0 or not made.stdout.strip():
        return None, "", "git could not record the merge with main to run the checks on"
    return made.stdout.strip(), f"the merge with main ({main_sha[:7]})", ""


def merges_to_the_tree_of(number: int, head: str, main_sha: str, behind: int, ran_on: str) -> bool:
    """Whether `gh pr merge` would put on main, file for file, the tree of `ran_on`.

    Two trees are the same when git gives them the same name, so this is one
    comparison and cannot be nearly right. Anything it cannot find out (a
    conflict, a clone too shallow to hold `ran_on`) is a no.
    """
    made, _called, _why_not = tree_to_merge(number, head, main_sha, behind)
    if made is None:
        return False
    merged, ran = git("rev-parse", f"{made}^{{tree}}"), git("rev-parse", f"{ran_on}^{{tree}}")
    return merged.returncode == 0 and ran.returncode == 0 and merged.stdout.strip() == ran.stdout.strip() != ""


def quick_checks(tree: str, main_sha: str) -> list[str]:
    """The four steps of CI that read documents, run on a commit: what failed.

    In a scratch worktree, with that tree's own copies of the scripts, which
    is what CI will run once it is merged. The worktree is removed afterwards.
    """
    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="check_pr-") as scratch:
        where = Path(scratch) / "tree"
        made = subprocess.run(("git", "worktree", "add", "--detach", "--quiet", str(where), tree),
                              cwd=ROOT, capture_output=True, text=True)
        if made.returncode != 0:
            return [f"could not check out {tree[:7]}: {(made.stderr.strip().splitlines() or ['git worktree add failed'])[-1]}"]
        try:
            for name, arguments in QUICK_CHECKS:
                if not (where / arguments[0]).is_file():
                    failures.append(f"{arguments[0]} is not in that tree")
                    continue
                run = subprocess.run((sys.executable, *(argument.format(main=main_sha) for argument in arguments)),
                                     cwd=where, capture_output=True, text=True)
                if run.returncode != 0:
                    output = (run.stdout + run.stderr).strip().splitlines() or ["no output"]
                    telling = [line.strip() for line in output if "FAIL" in line] or [output[-1].strip()]
                    failures.append(f"{name} fails: {telling[0][:200]}")
        finally:
            subprocess.run(("git", "worktree", "remove", "--force", str(where)), cwd=ROOT, capture_output=True)
            subprocess.run(("git", "worktree", "prune"), cwd=ROOT, capture_output=True)
    return failures


def check_merge(number: int) -> int:
    pr = gh_json("pr", "view", str(number), "--json", "number,title,body,state,mergeable,headRefOid,files")
    report = Report()
    if pr["state"] != "OPEN":
        sys.exit(f"#{number} is {pr['state'].lower()}")
    head = pr["headRefOid"]

    if pr["title"].startswith("WIP"):
        report.no("the title still begins with WIP:")
    else:
        report.ok("the title has no WIP:")

    if pr["mergeable"] == "MERGEABLE":
        report.ok("GitHub reports it mergeable")
    else:
        report.no(f"GitHub reports {pr['mergeable']} (UNKNOWN right after a push: ask again in a few seconds)")

    main_sha = main_commit()
    behind, main_files = delta(head, main_sha)
    _ahead, pr_files = delta(main_sha, head)
    head_state, head_bad = check_runs(head)
    ran_on, ran_state, since = (None, "none", None) if head_state == "none" else (head, head_state, [])
    if head_state == "none":
        # No run on the head: it was pushed with [skip ci], or the pull request
        # conflicted with main when it was pushed. Walk back along the branch
        # to the last commit that has one.
        sha = head
        for _ in range(30):
            parents = commit(sha)["parents"]
            if not parents:
                break
            sha = parents[0]["sha"]
            ancestor_state, _bad = check_runs(sha)
            if ancestor_state != "none":
                ran_on, ran_state, since = sha, ancestor_state, delta(sha, head)[1]
                break
    # Asked only where it can change the answer: main has moved, or the run
    # was on a commit before the head.
    same_tree = (ran_on is not None and ran_state == "passed" and (behind > 0 or ran_on != head)
                 and merges_to_the_tree_of(number, head, main_sha, behind, ran_on))
    lines, quick = ci_verdict(Evidence(head, behind, main_files, pr_files, head_state, head_bad,
                                       ran_on, ran_state, since, same_tree))
    for ok, text in lines:
        (report.ok if ok else report.no)(text)
    if quick:
        tree, called, why_not = tree_to_merge(number, head, main_sha, behind)
        if tree is None:
            report.no(why_not)
        else:
            failures = quick_checks(tree, main_sha)
            for failure in failures:
                report.no(f"on {called}: {failure}")
            if not failures:
                report.ok(f"on {called}, the checks that read documents pass: "
                          + ", ".join(name for name, _arguments in QUICK_CHECKS))

    claim = claim_of(pr)
    main = on_main()
    if not claim["has_claim_lines"]:
        report.no("the text does not begin with the Task:, Files: and `Phase P, finding F` lines")
    for kind, mine in (("Phase", claim["phase"]), ("finding", claim["finding"])):
        if mine is None:
            continue
        written, remark = number_on_main(kind, mine, main)
        if written:
            report.no(f"{kind} {mine} is on main already")
        else:
            report.ok(f"{kind} {mine} is not on main")
            if remark:
                report.note(remark)
    for other in open_claims():
        if other["number"] == number:
            continue
        for kind, key in (("Phase", "phase"), ("finding", "finding")):
            if claim[key] is not None and other[key] == claim[key]:
                who = "it moves" if other["number"] > number else "it was first: this one moves"
                (report.note if other["number"] > number else report.no)(f"#{other['number']} is open and says {kind} {claim[key]} too; {who}")
        both = same_files(claim["files"], other["files"])
        if both:
            report.note(f"#{other['number']} is open and names or changes {', '.join(both)}: whichever merges second merges the other")

    suites = len(re.findall(r"(?m)^    RUN_TEST\(", file_at("tests/test_suite.cpp", head)))
    # The figures a document publishes may carry a `<!--metrics:key/shape-->`
    # marker (tools/ci/check_numbers.py): an HTML comment naming the claim, which
    # a reader does not see and which must not read as a word of prose.
    readme = re.sub(r"<!--metrics:[^>]*-->", "", file_at("README.md", head))
    stated = {int(n) for n in re.findall(r"\| Engine unit tests \| (\d+) suites", readme)}
    stated |= {int(n) for n in re.findall(r"All (\d+) engine test suites passed", readme)}
    stated |= {int(n) for n in re.findall(r"# Run the (\d+) engine test suites", readme)}
    if stated == {suites}:
        report.ok(f"README.md says {suites} unit suites, and tests/test_suite.cpp has {suites}")
    else:
        report.no(f"tests/test_suite.cpp has {suites} unit suites; README.md says {sorted(stated)}")

    print("\nnot ready" if report.failed
          else f"\nready: gh pr merge {number} --merge, then watch `gh run list --branch main --limit 3` to the end")
    return 1 if report.failed else 0


def self_test() -> int:
    """The cases the reading of a claim is trusted for. Needs no network."""
    def claim(body: str, title: str = "WIP: a change", changed: tuple[str, ...] = ()) -> dict:
        return claim_of({"number": 1, "title": title, "body": body,
                         "files": [{"path": path} for path in changed]})

    numbered = claim("Task: a row\nFiles: `src/a.cpp` (new), `include/a.hpp`,\n`tests/test_suite.cpp`\nPhase 5, finding 7\n\ntext, `not/a/file.cpp`")
    plain = claim("Task: not a row of the table.\nFiles: `ROADMAP.md`\nNo phase, no finding: documents only; no C++ changed.\n\ntext")
    other = claim("Task: something else.\nFiles: `docs/README.md`\nNo phase, no finding: tooling, nothing that moves a scoreboard.\n")
    sentence = claim("Task: a row\nFiles: `src/a.cpp`, `.github/workflows/ci.yml`, `docs/findings/64-lists.md`.\nPhase 5, finding 7\n")
    silent = claim("## What\n\nA change, described.", title="Phase 56: a title that says a number")
    folder = claim("Task: a row\nFiles: `tests/dom-conformance/`\nPhase 5, finding 7\n")
    inside = claim("Task: a row\nFiles: `src/b.cpp`\nPhase 6, finding 8\n", changed=("tests/dom-conformance/everyday.dat",))
    remark = claim("Task: a row\nFiles: `src/a.cpp` (the `f()` helper), `tests/test_suite.cpp`\nPhase 5, finding 7\n")
    around = claim("Task: a row\nFiles: new `src/a.cpp`, `src/b.cpp` -- the helper; `src/c.cpp` (half (the `g()` half, not `h()`))\nPhase 5, finding 7\n")
    unpaired = claim("Task: a row\nFiles: `src/a.cpp` (new, `src/b.cpp`\nPhase 5, finding 7\n")

    # `staleness` asks git, and this self-test needs no network. What is pinned
    # here is what a claim says about each of the three answers it can give,
    # which is the thing the session that opened a 39-commit-old landing never saw.
    stale = [(0, ""), (39, "it does not merge with main any more (tests/test_suite.cpp)"),
             (39, "")]
    wanted_stale = [
        [(False, "main is in the head commit abc1234")],
        [(True, "it does not merge with main any more (tests/test_suite.cpp): it is 39 commit(s) "
                "behind main. Merge main in, or close it if what it was for has landed")],
        [(False, "39 commit(s) behind main, and it still merges")],
    ]

    cases = [
        ("the three lines: the numbers", (numbered["phase"], numbered["finding"]), (5, 7)),
        ("the three lines: the files, over two lines, without what is in brackets",
         numbered["files"], {"src/a.cpp", "include/a.hpp", "tests/test_suite.cpp"}),
        ("a remark in brackets that has brackets in it is a remark to its end",
         remark["files"], {"src/a.cpp", "tests/test_suite.cpp"}),
        ("a name in backticks is the name, whatever is said around it, and a semicolon parts two as a comma does",
         around["files"], {"src/a.cpp", "src/b.cpp", "src/c.cpp"}),
        ("brackets that do not pair up are read as they were", unpaired["files"], {"src/a.cpp", "src/b.cpp"}),
        ("the three lines are a claim", numbered["has_claim_lines"], True),
        ("`No phase, no finding` is the third line of a claim, and a claim", plain["has_claim_lines"], True),
        ("`No phase, no finding` takes no number", (plain["phase"], plain["finding"]), (None, None)),
        ("`No phase, no finding` ends the list of files: its words are not files", plain["files"], {"ROADMAP.md"}),
        ("two pull requests without numbers share no file", same_files(plain["files"], other["files"]), []),
        ("a name at the end of a sentence loses the full stop, one that begins with a full stop keeps it",
         sentence["files"], {"src/a.cpp", ".github/workflows/ci.yml", "docs/findings/64-lists.md"}),
        ("no claim lines is no claim", silent["has_claim_lines"], False),
        ("no claim lines: the number in the title is still a number in use", silent["phase"], 56),
        ("a name that ends in / is every file under it",
         same_files(folder["files"], inside["files"]), ["tests/dom-conformance/everyday.dat"]),
        ("the files every pull request changes are shared by none",
         same_files({"AGENTS.md", "README.md"}, {"AGENTS.md", "README.md"}), []),
        ("a claim without numbers is called that, not `Phase None`", numbers_of(plain), "no phase, no finding"),
        ("a claim with numbers is called by them", numbers_of(numbered), "Phase 5, finding 7"),
    ]

    # main has write-ups of phases 1-4 and 6 and of findings 1-6 and 8, and
    # floors of 6 and 8: Phase 5 and finding 7 are taken and have no write-up.
    main = Main(set(range(1, 7)), set(range(1, 9)), {1, 2, 3, 4, 6}, {1, 2, 3, 4, 5, 6, 8}, 6, 8)
    below = claim_problems(numbered, [numbered], main)
    written = claim_problems(claim("Task: a row\nFiles: `src/c.cpp`\nPhase 6, finding 8\n"), [], main)
    free = claim_problems(claim("Task: a row\nFiles: `src/c.cpp`\nPhase 7, finding 9\n"), [], main)
    cases += [
        ("a number at or below a floor that has no write-up does not stop its pull request",
         [blocking for blocking, _text in below], [False, False]),
        ("and the remark says where it stands",
         ["has no write-up on main" in text and "floor" in text for _blocking, text in below], [True, True]),
        ("a number that has a write-up on main stops it",
         written, [(True, "Phase 6 is on main already"), (True, "finding 8 is on main already")]),
        ("a number above the floors that has no write-up is free", free, []),
    ]
    # When a merge needs no new CI run: what is a document, what is a check
    # that reads documents, and what the two sides of a merge may have changed.
    def kinds(name: str) -> tuple[bool, bool]:
        return bool(DOCUMENT.search(name)), bool(QUICK.search(name))

    def verdict(**found) -> tuple[list[bool], bool]:
        facts = dict(head="a" * 40, behind=0, main_files=[], pr_files=["src/a.cpp", "README.md"],
                     head_state="passed", head_bad=[], ran_on="a" * 40, ran_state="passed", since=[])
        facts.update(found)
        lines, quick = ci_verdict(Evidence(**facts))
        return [ok for ok, _text in lines], quick

    moved = dict(behind=2)
    no_run = dict(head_state="none", ran_on="b" * 40)
    cases += [
        ("documents: at the top of the tree, under docs/, the README.md of a test directory",
         [kinds(n) for n in ("README.md", "AGENTS.md", "docs/phases/63-a.md", "docs/state.json", "tests/wpt/README.md")],
         [(True, True)] * 5),
        ("the checks that read documents are quick and are not documents",
         [kinds(n) for n in ("tools/ci/check_docs.py", "tools/ci/check_numbers.py", "tools/next_ids.py")],
         [(False, True)] * 3),
        ("neither: a file run_wpt.py would not find in its manifest, another tool, the build, the workflow, a test",
         [kinds(n) for n in ("tests/wpt/dom/nodes/notes.md", "src/notes.md", "tools/conformance/run_wpt.py",
                             "Makefile", ".github/workflows/ci.yml", "tests/test_suite.cpp")],
         [(False, False)] * 6),
        ("CI passed on a head that has main in it: ready, and nothing more to run",
         verdict(), ([True, True], False)),
        ("main moved by a line of README.md: no new CI run, the quick checks decide",
         verdict(**moved, main_files=["README.md"]), ([True, True], True)),
        ("main moved by documents and a check that reads them: the same",
         verdict(**moved, main_files=["README.md", "ROADMAP.md", "tools/ci/check_numbers.py"]), ([True, True], True)),
        ("main moved by those and by the Makefile and the workflow (#92, which #91 was merged without): a new run",
         verdict(**moved, main_files=[".github/workflows/ci.yml", "Makefile", "README.md", "tools/ci/check_numbers.py"]),
         ([False, True], False)),
        ("main moved by engine code: a new run",
         verdict(**moved, main_files=["src/js/JsEval.cpp"]), ([False, True], False)),
        ("main moved by more files than GitHub lists: a new run",
         verdict(**moved, main_files=None), ([False, True], False)),
        ("a pull request of documents only needs no CI run of its own, wherever main is (#93, #95)",
         verdict(**moved, main_files=["src/js/JsEval.cpp"], pr_files=["README.md"], head_state="none", ran_on=None),
         ([True, True], True)),
        ("...nor has it to wait for the one that is running",
         verdict(pr_files=["README.md"], head_state="running", head_bad=["Build: in_progress"]), ([True, True], True)),
        ("...but a run that failed on its head stops it",
         verdict(pr_files=["README.md"], head_state="failed", head_bad=["Documentation shape: failure"]),
         ([True, False], False)),
        ("a pull request that changes a check needs CI on its head once, and no second run when engine code lands",
         verdict(**moved, main_files=["src/js/JsEval.cpp"], pr_files=["AGENTS.md", "tools/ci/check_pr.py"]),
         ([True, True], True)),
        ("...the once is not waived",
         verdict(**moved, main_files=["src/js/JsEval.cpp"], pr_files=["AGENTS.md", "tools/ci/check_pr.py"],
                 head_state="none", ran_on=None), ([True, False], False)),
        ("a head pushed with [skip ci] after a run that passed, documents changed since: the quick checks decide",
         verdict(**no_run, since=["AGENTS.md"]), ([True, True], True)),
        ("...a check changed since: a new run",
         verdict(**no_run, since=["AGENTS.md", "tools/ci/check_pr.py"]), ([True, False], False)),
        ("...the run before it failed: not ready",
         verdict(**no_run, ran_state="failed", since=["AGENTS.md"]), ([True, False], False)),
        ("a pull request that changes a check, after merging main with [skip ci] to settle a conflict in a document: "
         "its own checks are as CI saw them",
         verdict(**no_run, pr_files=["AGENTS.md", "tools/ci/check_pr.py"],
                 since=["AGENTS.md", "src/js/JsEval.cpp", "tests/test_suite.cpp"]), ([True, True], True)),
        ("...but not when its checks changed since that run",
         verdict(**no_run, pr_files=["AGENTS.md", "tools/ci/check_pr.py"],
                 since=["AGENTS.md", "src/js/JsEval.cpp", "tools/ci/check_pr.py"]), ([True, False], False)),
        ("...and an engine change that merged main with [skip ci] needs its run",
         verdict(**no_run, since=["AGENTS.md", "src/js/JsEval.cpp"]), ([True, False], False)),
        ("CI still running on an engine change: not ready",
         verdict(head_state="running", head_bad=["Build: in_progress"]), ([True, False], False)),
        ("main moved by engine code the branch had already (a head it carries was merged first): "
         "merged, it is the tree CI passed on, and the quick checks decide",
         verdict(**moved, main_files=["src/js/JsEval.cpp", "tests/test_suite.cpp"], same_tree=True), ([True], True)),
        ("...the same when the head is a merge that changed no file, pushed with [skip ci] after the run",
         verdict(**moved, **no_run, main_files=["src/js/JsEval.cpp"], since=["src/js/JsEval.cpp"], same_tree=True),
         ([True], True)),
        ("...but a run that failed on the head is not outvoted by an older one on the same tree",
         verdict(**moved, main_files=["src/js/JsEval.cpp"], head_state="failed", head_bad=["Build: failure"],
                 same_tree=True), ([False, False], False)),
        ("...nor is a run that failed made good by the tree being the same",
         verdict(**moved, **no_run, ran_state="failed", main_files=["src/js/JsEval.cpp"], since=[], same_tree=True),
         ([False, False], False)),
    ]

    names_stale = ["a head with main in it: quiet",
                   "39 commits behind and it does not merge: blocking, and it names the file",
                   "39 commits behind and it still merges: a note, not a refusal"]
    for name, (behind, why), wanted in zip(names_stale, stale, wanted_stale):
        real = globals()["staleness"]
        globals()["staleness"] = lambda head, sha, _b=behind, _w=why: (_b, _w)
        try:
            got = staleness_problems({"head": "abc1234"}, "deadbee")
        finally:
            globals()["staleness"] = real
        cases.append((name, got, wanted))

    failed = 0
    for name, got, wanted in cases:
        ok = got == wanted
        failed += not ok
        print(f"  [{'ok  ' if ok else 'FAIL'}] {name}")
        if not ok:
            print(f"         wanted {wanted!r}")
            print(f"         got    {got!r}")
    print(f"\n{len(cases) - failed}/{len(cases)} pass")
    return 1 if failed else 0


def main() -> int:
    arguments = sys.argv[1:]
    if not arguments:
        return show_claims()
    if arguments == ["--self-test"]:
        return self_test()
    if len(arguments) == 2 and arguments[0] in ("claim", "merge") and arguments[1].lstrip("#").isdigit():
        number = int(arguments[1].lstrip("#"))
        return check_claim(number) if arguments[0] == "claim" else check_merge(number)
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
