#!/usr/bin/env python3
"""What the open pull requests claim, and whether one is ready to be merged.

    python3 tools/ci/check_pr.py             # the claims, the numbers taken, the next free pair
    python3 tools/ci/check_pr.py claim N     # after opening #N: are its numbers and its files free
    python3 tools/ci/check_pr.py merge N     # when #N is finished: merge it, or leave it to the watch
    python3 tools/ci/check_pr.py landed N    # during a watch: wait for the landing session's answer
    python3 tools/ci/check_pr.py watch       # is a watch on, and what would it take now
    python3 tools/ci/check_pr.py --self-test # how a claim and the board are read, on fixed texts; no network

AGENTS.md says what a claim is and what has to be true before a merge. Each
of those was missed at least once by an agent that had read it, because each
is a thing to look up at a moment when the work feels finished. This looks
them up. It reads; it changes nothing on GitHub (but for the two commands of
the landing session, below). In the clone it fetches `main` and the pull
request it is asked about, and `merge N` may check a tree out into a scratch
directory and remove it again; no branch moves.

The watch
---------
While a watch is on (AGENTS.md, "Working together"), one session merges and
no other: it tests what is finished together, in one CI run, and merges each
pull request as it is. Whether one is on is said by one comment on the board
(the open pull request whose title begins with `BOARD:`), which the landing
session rewrites at every look:

    Lander: WATCH ON until 2026-11-23 12:00 UTC. Last look 2026-11-23 05:00 UTC.
    Lander: WATCH OFF since 2026-11-23 12:01 UTC. ...

A watch is on when the last such comment says ON, its end has not come, and
its last look is at most 30 minutes old. A landing session that has died
(its login ended, its turn was cut) stops looking, and half an hour later
every agent is told "no watch is on" without anybody having to say so.

`merge N`, while a watch is on, ends with "do not merge" (exit 3) where it
would have said "ready", and does not ask for `main` to be merged in: a
`main` that has moved is the landing session's to deal with. `--landing`
after it is the landing session asking, and gets the answer of no watch.

`landed N` waits, up to four minutes a call (`--wait SECONDS` for another
time), and says which of four things is true: #N has landed (exit 0); it is
yours again, with what the landing session wrote on the board, or with the
reason it is not something a watch takes (exit 1); no watch is on any more,
so merge it yourself (exit 2); nothing yet, run it again (exit 3).

`watch` lists the open pull requests in two groups: finished, green on the
head and mergeable, which is what the landing session takes, with whether
each can be merged at once (`main` is in its head) or goes into a lot; and
the rest, each with its reason. A pull request the landing session sent back
(`Lander: #N is yours again (head abc1234): why`, on the board) is left
alone until its head is another commit.

`watch --beat "YYYY-MM-DD HH:MM"` and `watch --off "what it did"` are the
landing session's: they write the status comment (`--name NAME` when the
landing session is not called Lander). They are the only two commands here
that write anything on GitHub.

`claim N` fails (exit 1) when
  - #N does not begin with the `Task:`, `Files:` and `Phase P, finding F`
    lines;
  - its phase or its finding is on `main` already, or an open pull request
    with a lower number has it (of two that took a number at the same
    moment, the later one moves);
  - a file it names or has changed belongs to an open pull request with a
    lower number. The files every pull request changes (AGENTS.md lists
    them) do not count;
  - (a note, not a failure) it has no `Agent:` line: AGENTS.md, "Working
    together", asks each agent for a name of its own since 22 November;
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
  - its phase or finding is on `main` already.
It no longer fails when README.md states another number of unit suites than
tests/test_suite.cpp and tests/suites/ hold: that count is the landing session's to
refresh (AGENTS.md, "Working together"; tools/ci/check_numbers.py says what
it cost while every pull request edited it), and `merge N` only notes it.

A pull request whose title begins with `BOARD:` or `WATCH:` claims nothing
and is left out of the claims: the first is where the landing session says
whether a watch is on and what it sent back, the second is the landing
session's own, which holds the heads of the pull requests it is testing
together and none of their files.
`merge N` says "not ready" for the board whatever else is true of it: it is
never merged.

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
import time
from datetime import datetime, timedelta, timezone
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
    ("check_suites.py", ("tools/ci/check_suites.py",)),
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
# The board and a watch (AGENTS.md, "Working together") are pull requests and
# claim nothing: no task, no numbers, no files.
NOT_A_CLAIM_RE = re.compile(r"^(?:BOARD|WATCH):")
# The board is never merged: it would be closed by it, and it is where the
# agents find each other.
NEVER_MERGED_RE = re.compile(r"^BOARD:")
# `Agent: Heron`: the name an agent gave itself, the same in all its pull requests.
# Not `Agent: <the name you gave yourself>`, which is what `next_ids.py --pr-body`
# prints for it to be written over.
AGENT_RE = re.compile(r"^Agent:\s*([^\s,;.<>]+)")

# The watch (AGENTS.md, "Working together"). One comment on the board says
# whether a watch is on: the landing session writes it when the watch begins
# and rewrites its "Last look" at every look. A watch whose last look is older
# than this many minutes is taken to have died. On 23 November the login to
# GitHub ended for every session in the middle of a watch, and the board went
# on saying "a watch is on until 06:30" for an hour after nobody was looking.
QUIET_AFTER_MINUTES = 30
STAMP = "%Y-%m-%d %H:%M"
WATCH_ON_RE = re.compile(r"^(\w+): WATCH ON until (\d{4}-\d\d-\d\d \d\d:\d\d) UTC\. "
                         r"Last look (\d{4}-\d\d-\d\d \d\d:\d\d) UTC\b")
WATCH_OFF_RE = re.compile(r"^(\w+): WATCH OFF\b")
# What the landing session writes on the board about a pull request it cannot
# land. The head commit is part of it: the pull request is left alone until
# its head is another one, and nothing is compared by its time.
YOURS_AGAIN_RE = re.compile(r"^\w+: #(\d+) is yours again \(head ([0-9a-f]{7,40})\)")
# The line that says which pull requests a lot holds, each with its head:
# "Lander: lot of 03:01 UTC: #305 (`52fe97d`), #310 (`0a0ffab`) with `main` …".
LOT_RE = re.compile(r"^\w+: (?:\*\*)?(?:the last )?lot of ")
# The label that gives a pull request the short run of CI (.github/workflows/ci.yml).
LOT_LABEL = "lot"
WATCH_ON_TEXT = (
    "While this line says ON and its last look is less than {quiet} minutes old, only the landing session "
    "merges. A pull request that is finished (no `WIP:` in its title), green on its head commit and "
    "mergeable is taken as it is: push nothing more to it, do not merge `main` into it, and wait with "
    "`python3 tools/ci/check_pr.py landed N`. What cannot be landed is said here, as "
    "`{name}: #N is yours again (head abc1234): why`. (AGENTS.md, \"Working together\".)"
)


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


def own_file_suites(ref: str) -> int:
    """The suites that are files of their own at a commit or on a branch.

    The `.cpp` files of tests/suites/, one suite each: tools/ci/check_suites.py
    holds a file to that, so the names are enough and no file is read. A commit
    from before 23 November 2026 has no such directory, and the answer is 0.
    """
    listed = gh("api", f"{REPO}/contents/tests/suites?ref={ref}", "--jq",
                '[.[] | select(.type == "file" and (.name | endswith(".cpp")))] | length', check=False)
    return int(listed) if listed.strip().isdigit() else 0


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
    agent = next((m.group(1) for m in (AGENT_RE.match(line) for line in head) if m), None)
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
        "agent": agent,
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


def open_pull_requests() -> list[dict]:
    return gh_json("pr", "list", "--state", "open", "--limit", "100",
                   "--json", "number,title,body,headRefOid,files")


def open_claims(prs: list[dict] | None = None) -> list[dict]:
    """The open pull requests that claim something: all but the board and a watch."""
    prs = open_pull_requests() if prs is None else prs
    return sorted((claim_of(pr) for pr in prs if not NOT_A_CLAIM_RE.match(pr["title"])),
                  key=lambda c: c["number"])


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
    if claim["has_claim_lines"] and not claim.get("agent"):
        problems.append((False, "no `Agent:` line: give yourself a name and put it under the three lines "
                                "(AGENTS.md, \"Working together\")"))
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
    prs = open_pull_requests()
    claims = open_claims(prs)
    main = on_main()
    print(f"main: phases up to {max(main.phases)}, findings up to {max(main.findings)}")
    for claim in claims:
        pushed = hours_since(commit(claim["head"])["commit"]["committer"]["date"])
        name = f"  [{claim['agent']}]" if claim.get("agent") else ""
        print(f"\n#{claim['number']}{name}  {numbers_of(claim)}  (last commit {pushed:.1f} h ago)")
        print(f"    {claim['title'][:110]}")
        own = sorted(claim["files"] - SHARED)
        print("    files: " + (", ".join(own) if own else "(none)"))
        for blocking, problem in claim_problems(claim, claims, main):
            print(f"    {'!!' if blocking else '  '} {problem}")
    for pr in sorted(prs, key=lambda pr: pr["number"]):
        if NOT_A_CLAIM_RE.match(pr["title"]):
            print(f"\n#{pr['number']}  not a claim: {pr['title'][:100]}")
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
    prs = open_pull_requests()
    claims = open_claims(prs)
    mine = next((c for c in claims if c["number"] == number), None)
    if mine is None:
        if any(pr["number"] == number for pr in prs):
            print(f"ok  #{number} is the board or a watch: it claims no task, no numbers and no files")
            return 0
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


def state_of(latest: list[tuple[str, str, str]]) -> tuple[str, list[str]]:
    """'passed', 'short', 'failed', 'running' or 'none' for the jobs of a commit,
    each (name, status, conclusion) in lower case, and the ones that are not green.

    'short' is the run a pull request with the label `lot` gets while a watch
    is on (.github/workflows/ci.yml): the build, the unit suites and the
    documents passed, and the other ten jobs were skipped, because the whole
    of CI runs on the lot the pull request lands in. It is enough to be taken
    into a lot. It is not enough for anybody to merge on.
    """
    if not latest:
        return "none", []
    bad = [f"{name}: {conclusion or status}" for name, status, conclusion in latest
           if conclusion not in ("success", "skipped", "neutral")]
    if not bad:
        return ("short" if any(conclusion == "skipped" for _n, _s, conclusion in latest) else "passed"), []
    if any(status != "completed" for _n, status, _c in latest):
        return "running", bad
    return "failed", bad


def check_runs(sha: str) -> tuple[str, list[str]]:
    """`state_of` for a commit."""
    runs = gh_json("api", f"{REPO}/commits/{sha}/check-runs?per_page=100").get("check_runs", [])
    # A job that was run again leaves both attempts: the last one of a name counts.
    latest: dict[str, dict] = {}
    for entry in sorted(runs, key=lambda r: r.get("started_at") or ""):
        latest[entry["name"]] = entry
    return state_of([(r["name"], (r["status"] or "").lower(), (r["conclusion"] or "").lower()) for r in latest.values()])


def short_run_note(head: str, number: int | None = None) -> str:
    """What to say of a short run to somebody who is about to merge on it."""
    return (f"only the short run of CI passed on {head[:7]} (build, unit suites, documents): the pull request has "
            "the label `lot`, which the landing session puts on while a watch is on, and the other ten jobs "
            "were skipped. Nobody merges on that"
            + ("" if number is None else
               f". The label is off now, which starts all twelve jobs on {head[:7]}: ask again when they have ended"))


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


def ci_verdict(e: Evidence, watch_on: bool = False) -> tuple[list[tuple[bool, str]], bool]:
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
    elif watch_on:
        # While a watch is on, a `main` that has moved is the landing session's
        # to deal with: it tests the pull request merged with `main` as it then
        # is, with whatever else is finished, in one run. An agent that merges
        # `main` itself starts a run of its own and moves the head under that.
        lines.append((True, f"main has moved by {e.behind} commit(s) that are not in {head}: while a watch is on "
                            "that is the landing session's to deal with. Do not merge main into the branch"))
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
    elif e.head_state == "short":
        if watch_on:
            lines.append((True, f"the short run of CI passed on the head commit {head} (build, unit suites, documents); "
                                "the whole of CI runs on the lot it lands in"))
        else:
            lines.append((False, short_run_note(e.head)))
    elif e.head_state == "running":
        lines.append((False, f"CI on {head}: " + "; ".join(e.head_bad)))
    elif e.ran_on is None:
        lines.append((False, f"no CI run on {head} or on the 30 commits before it"))
    elif e.ran_state == "short" and not watch_on:
        lines.append((False, f"no CI run on {head}, and " + short_run_note(e.ran_on)))
    elif e.ran_state not in ("passed", "short"):
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


class Watch(NamedTuple):
    """What the board says about a watch."""
    state: str                  # "on"; "off" and "none": no watch; "over": its time is up; "quiet": nobody is looking
    name: str                   # the landing session's name, "" when the board has no status comment
    until: datetime | None
    looked: datetime | None
    comment: dict | None        # the status comment, as `gh pr view --json comments` gives it

    @property
    def on(self) -> bool:
        return self.state == "on"

    def says(self) -> str:
        if self.state == "on":
            return (f"a watch is on until {self.until:%H:%M} UTC on {self.until.day} {self.until:%B} "
                    f"({self.name} last looked at {self.looked:%H:%M})")
        if self.state == "over":
            return (f"no watch is on: the one {self.name} began was to end at "
                    f"{self.until:%H:%M} UTC on {self.until.day} {self.until:%B}")
        if self.state == "quiet":
            return (f"no watch is on: the board says one is, but {self.name} has not looked since "
                    f"{self.looked:%H:%M} UTC on {self.looked.day} {self.looked:%B}, "
                    f"more than {QUIET_AFTER_MINUTES} minutes ago")
        return "no watch is on"


def first_line(comment: dict) -> str:
    return (comment.get("body") or "").replace("\r\n", "\n").lstrip().split("\n", 1)[0]


def watch_state(comments: list[dict], now: datetime) -> Watch:
    """Whether a watch is on at `now`: the last status comment on the board decides."""
    for comment in reversed(comments):
        line = first_line(comment)
        off = WATCH_OFF_RE.match(line)
        if off:
            return Watch("off", off.group(1), None, None, comment)
        on = WATCH_ON_RE.match(line)
        if on:
            until = datetime.strptime(on.group(2), STAMP).replace(tzinfo=timezone.utc)
            looked = datetime.strptime(on.group(3), STAMP).replace(tzinfo=timezone.utc)
            if now >= until:
                state = "over"
            elif now - looked > timedelta(minutes=QUIET_AFTER_MINUTES):
                state = "quiet"
            else:
                state = "on"
            return Watch(state, on.group(1), until, looked, comment)
    return Watch("none", "", None, None, None)


def sent_back(comments: list[dict], number: int) -> tuple[str, str] | None:
    """(the head commit #number was sent back at, what was written), from the last such comment."""
    for comment in reversed(comments):
        found = YOURS_AGAIN_RE.match(first_line(comment))
        if found and int(found.group(1)) == number:
            return found.group(2), (comment.get("body") or "").strip()
    return None


def taken_at(comments: list[dict], number: int) -> str | None:
    """The head commit at which the landing session last said it had taken
    #number into a lot, or None when it has not, or has sent it back since.

    The landing session takes some heads that have no green run of their own:
    one whose code is code that was green, merged with `main` by git alone
    and changed since in documents only (tools/watch/watch.sh,
    `code_was_green`). The run on the lot is the test of such a head. Its
    agent, asking `landed N`, was told "the landing session does not take #N
    as it is: no CI run on its head commit", and the natural answer to that
    is a push, which moves the head out from under the lot."""
    listed = re.compile(rf"#{number} \(`([0-9a-f]{{7,40}})`\)")
    for comment in reversed(comments):
        line = first_line(comment)
        back = YOURS_AGAIN_RE.match(line)
        if back and int(back.group(1)) == number:
            return None
        if LOT_RE.match(line):
            found = listed.search(line)
            if found:
                return found.group(1)
    return None


def rollup(checks: list[dict] | None) -> tuple[str, list[str]]:
    """'passed', 'short', 'failed', 'running' or 'none' for the checks GitHub lists
    for a pull request's head (`statusCheckRollup`), and the ones that are not green.

    The same answer as `check_runs`, from the other API: `gh pr view --json`
    asks GraphQL, whose limit is not the one `gh run view` and `gh api repos/…`
    share (AGENTS.md, \"The turn, and what a merge does to it\").
    """
    latest: dict[str, dict] = {}
    for entry in sorted(checks or [], key=lambda c: c.get("startedAt") or ""):
        latest[entry.get("name") or entry.get("context") or "?"] = entry    # a job run again: its last attempt

    def verdict(check: dict) -> str:
        return (check.get("conclusion") or check.get("state") or "").lower()

    return state_of([(name, (check.get("status") or "completed").lower(), verdict(check))
                     for name, check in latest.items()])


def why_not_taken(pr: dict, comments: list[dict]) -> str:
    """Why the landing session leaves a pull request alone during a watch; "" when it takes it.

    Taken is: finished, green on the head, not in conflict with `main`, and
    not sent back at this very head. Nobody has to ask: on the night of 22
    November three finished and green pull requests stood for up to an hour
    and three quarters while a watch was on, because their agents were to try
    twice themselves first and then write a line, and their turns had ended.
    """
    if NOT_A_CLAIM_RE.match(pr["title"]):
        return "it is the board or a watch"
    if pr.get("isDraft") or pr["title"].startswith("WIP"):
        return "its title still begins with WIP:"
    taken = taken_at(comments, pr["number"])
    if taken and pr["headRefOid"].startswith(taken):
        return ""       # the landing session has said so, of this very head
    state, bad = rollup(pr.get("statusCheckRollup"))
    if state == "none":
        files = [entry["path"] for entry in pr.get("files") or []]
        if not (files and all_of(DOCUMENT, files)):     # documents only: the lot's run is its run
            return "no CI run on its head commit"
    elif state == "running":
        # Taken all the same, since 24 November 2026: its own run and the
        # lot's go side by side, and it lands when the lot's has passed.
        # Waiting for its own first put a run's length in front of every
        # lot. If its own run fails it leaves the lot.
        pass
    elif state == "failed":
        return "CI failed on its head commit (" + "; ".join(bad[:3]) + ")"
    if pr.get("mergeable") == "CONFLICTING":
        return "it conflicts with main"
    back = sent_back(comments, pr["number"])
    if back and pr["headRefOid"].startswith(back[0]):
        return f"it was sent back at this head commit ({back[0][:7]}) and has not changed since"
    return ""


def board_number(prs: list[dict]) -> int | None:
    """The board: the open pull request whose title begins with `BOARD:`.

    `CHECK_PR_BOARD=N` in the environment names another pull request instead.
    That is for trying these commands out without telling every agent that a
    watch is on: the status comments are then read from #N and written there.
    """
    if os.environ.get("CHECK_PR_BOARD", "").isdigit():
        return int(os.environ["CHECK_PR_BOARD"])
    return next((pr["number"] for pr in sorted(prs, key=lambda pr: pr["number"])
                 if NEVER_MERGED_RE.match(pr["title"])), None)


def rewrites(last: Watch, until: datetime | None) -> bool:
    """Whether a status is written over the last status comment, not under it.

    A look of the same watch (the same end) rewrites its comment, and so does
    the end of a watch; a new watch gets a comment of its own, at the foot of
    the board where it is seen. `until` is None for the end of a watch.
    """
    if last.comment is None or last.state in ("off", "none"):
        return False
    return until is None or last.until == until


def board_comments(number: int | None) -> list[dict] | None:
    """The comments of the board, oldest first; None when they cannot be read."""
    if number is None:
        return None
    try:
        return json.loads(gh("pr", "view", str(number), "--json", "comments", check=False))["comments"]
    except (ValueError, KeyError):
        return None


def now_utc() -> datetime:
    return datetime.now(timezone.utc)


WATCH_PRS = "number,title,body,headRefOid,files,isDraft,mergeable,statusCheckRollup"


def show_watch() -> int:
    """Whether a watch is on, and what the landing session would take now."""
    prs = gh_json("pr", "list", "--state", "open", "--limit", "100", "--json", WATCH_PRS)
    board = board_number(prs)
    comments = board_comments(board)
    if comments is None:
        print("the board could not be read (an open pull request whose title begins with BOARD:)")
        return 2
    watch = watch_state(comments, now_utc())
    print(f"{watch.says()}  (the board is #{board})")
    main_sha = main_commit()
    taken, left = [], []
    for pr in sorted(prs, key=lambda pr: pr["number"]):
        if NOT_A_CLAIM_RE.match(pr["title"]):
            continue
        why = why_not_taken(pr, comments)
        how = ""
        if not why:
            # GitHub says UNKNOWN for most pull requests it has not been asked
            # about lately: whether this one still merges is asked of git.
            head = pr["headRefOid"]
            if fetched(f"refs/pull/{pr['number']}/head") != head:
                why = "its head commit could not be fetched"
            else:
                behind, conflict = staleness(head, main_sha)
                why = conflict
                how = ("main is in the head: it can be merged as it is, at once" if behind == 0
                       else f"{behind} commit(s) behind main, and it merges: into the lot")
        (left if why else taken).append((pr, why or how))
    print("\nfinished and mergeable, CI green or still going" + (": the landing session takes these" if watch.on else
                                                                ": theirs to merge, or the next watch's")
          + ("" if taken else "\n    (none)"))
    for pr, text in taken:
        print(f"    #{pr['number']} [{claim_of(pr).get('agent') or '-'}] {pr['headRefOid'][:7]}  {text}")
    print("\nnot now" + ("" if left else "\n    (none)"))
    for pr, text in left:
        print(f"    #{pr['number']} [{claim_of(pr).get('agent') or '-'}] {pr['headRefOid'][:7]}  {text}")
    return 0 if watch.on else 1


def write_status(first: str, rest: str, until: datetime | None) -> int:
    """Write the status comment of the board: rewrite the one of this watch, or add one."""
    prs = gh_json("pr", "list", "--state", "open", "--limit", "100", "--json", "number,title")
    board = board_number(prs)
    comments = board_comments(board)
    if comments is None:
        sys.exit("the board could not be read (an open pull request whose title begins with BOARD:)")
    body = first + ("\n\n" + rest if rest else "")
    last = watch_state(comments, now_utc())
    if rewrites(last, until):
        ident = (last.comment.get("url") or "").rsplit("issuecomment-", 1)[-1]
        if not ident.isdigit():
            sys.exit(f"the status comment has no number in its address: {last.comment.get('url')}")
        gh("api", "-X", "PATCH", f"{REPO}/issues/comments/{ident}", "-f", f"body={body}", "--silent")
    else:
        gh("pr", "comment", str(board), "--body", body)
    print(first)
    return 0


def beat(until: str, name: str) -> int:
    """The landing session's: say that a watch is on until `until`, and that it has just looked."""
    try:
        end = datetime.strptime(until, STAMP).replace(tzinfo=timezone.utc)
    except ValueError:
        sys.exit(f"--beat takes the end of the watch as \"YYYY-MM-DD HH:MM\" (UTC), not {until!r}")
    first = f"{name}: WATCH ON until {end:{STAMP}} UTC. Last look {now_utc():{STAMP}} UTC."
    return write_status(first, WATCH_ON_TEXT.format(quiet=QUIET_AFTER_MINUTES, name=name), end)


def watch_off(text: str, name: str) -> int:
    """The landing session's: say that the watch has ended, and what it did."""
    first = f"{name}: WATCH OFF since {now_utc():{STAMP}} UTC. {text}".rstrip()
    return write_status(first, "", None)


def check_landed(number: int, wait: int) -> int:
    """For the agent of #number, during a watch: wait for the landing session's answer.

    0: it has landed. 1: it is yours again (sent back, or not something the
    landing session takes), and why. 2: no watch is on, merge it yourself.
    3: still waiting when the time given was up; run it again.
    """
    deadline = time.monotonic() + wait
    while True:
        pr = gh_json("pr", "view", str(number), "--json", WATCH_PRS + ",state,mergedAt")
        if pr["state"] == "MERGED":
            print(f"#{number} has landed: merged at {pr['mergedAt'][11:16]} UTC. Its branch is `pr_merged`; "
                  "your next task begins with the person's Continue.")
            return 0
        if pr["state"] != "OPEN":
            print(f"#{number} is {pr['state'].lower()}")
            return 1
        prs = gh_json("pr", "list", "--state", "open", "--limit", "100", "--json", "number,title")
        comments = board_comments(board_number(prs))
        if comments is None:
            print("the board could not be read: no watch can be seen. Merge it yourself: "
                  f"python3 tools/ci/check_pr.py merge {number}")
            return 2
        back = sent_back(comments, number)
        if back and pr["headRefOid"].startswith(back[0]):
            print(f"#{number} is yours again. The landing session wrote:\n\n{back[1]}\n\n"
                  "Change it, push, and let CI pass on the new head commit: it is taken again then.")
            return 1
        watch = watch_state(comments, now_utc())
        if not watch.on:
            print(f"{watch.says()}. Merge #{number} yourself: python3 tools/ci/check_pr.py merge {number}")
            return 2
        why = why_not_taken(pr, comments)
        if why and not why.startswith("CI is still running"):
            print(f"{watch.says()}, and the landing session does not take #{number} as it is: {why}")
            return 1
        if time.monotonic() >= deadline:
            print(f"{watch.says()}. #{number} has not landed yet"
                  + (f" ({why})" if why else "") + ": run this again.")
            return 3
        time.sleep(min(60, max(1, deadline - time.monotonic())))


def check_merge(number: int, landing: bool = False) -> int:
    """`landing` is the landing session asking, about a pull request it is about
    to merge during its own watch: the answer is then the one given when no
    watch is on."""
    pr = gh_json("pr", "view", str(number), "--json", "number,title,body,state,mergeable,headRefOid,files,labels")
    report = Report()
    if pr["state"] != "OPEN":
        sys.exit(f"#{number} is {pr['state'].lower()}")
    if NEVER_MERGED_RE.match(pr["title"]):
        # Asked of #219 on 22 November, every question below answered "ok" and
        # the last line was "ready: gh pr merge 219 --merge".
        report.no(f"#{number} is the board: it is never merged, and never closed "
                  "(AGENTS.md, \"Working together\")")
        print("\nnot ready")
        return 1
    head = pr["headRefOid"]

    # Is a watch on? Then nobody merges but the landing session, and what is
    # asked here is whether #N is something it takes. Not asked of a watch's
    # own pull request, nor when the landing session is the one asking.
    open_prs = open_pull_requests()
    comments = board_comments(board_number(open_prs))
    watch = watch_state(comments or [], now_utc())
    watch_on = watch.on and not landing and not NOT_A_CLAIM_RE.match(pr["title"])

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
                                       ran_on, ran_state, since, same_tree), watch_on)
    for ok, text in lines:
        (report.ok if ok else report.no)(text)
    if "short" in (head_state, ran_state) and not watch_on and not landing:
        # No watch is on, so this agent merges, and all twelve jobs have not
        # run on what it would merge. Taking the label off starts them.
        if any(label.get("name") == LOT_LABEL for label in pr.get("labels") or []):
            gh("api", "-X", "DELETE", f"{REPO}/issues/{number}/labels/{LOT_LABEL}", check=False)
            report.note(f"the label `{LOT_LABEL}` is off #{number} now, which starts all twelve jobs on {head[:7]}: "
                        f"ask again when they have ended (`gh pr checks {number}`). If none has started in a minute, "
                        "push an empty commit")
        else:
            report.note(f"#{number} has no label `{LOT_LABEL}` any more: all twelve jobs run on its next push "
                        "(an empty commit will do), if they are not running already")
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
    for other in open_claims(open_prs):
        if other["number"] == number:
            continue
        for kind, key in (("Phase", "phase"), ("finding", "finding")):
            if claim[key] is not None and other[key] == claim[key]:
                who = "it moves" if other["number"] > number else "it was first: this one moves"
                (report.note if other["number"] > number else report.no)(f"#{other['number']} is open and says {kind} {claim[key]} too; {who}")
        both = same_files(claim["files"], other["files"])
        if both:
            report.note(f"#{other['number']} is open and names or changes {', '.join(both)}: whichever merges second merges the other")

    # The suites `main()` has a line for, and the files of tests/suites/.
    suites = len(re.findall(r"(?m)^    RUN_TEST\(", file_at("tests/test_suite.cpp", head))) + own_file_suites(head)
    # The figures a document publishes may carry a `<!--metrics:key/shape-->`
    # marker (tools/ci/check_numbers.py): an HTML comment naming the claim, which
    # a reader does not see and which must not read as a word of prose.
    readme = re.sub(r"<!--metrics:[^>]*-->", "", file_at("README.md", head))
    stated = {int(n) for n in re.findall(r"\| Engine unit tests \| (\d+) suites", readme)}
    stated |= {int(n) for n in re.findall(r"All (\d+) engine test suites passed", readme)}
    stated |= {int(n) for n in re.findall(r"# Run the (\d+) engine test suites", readme)}
    if stated == {suites}:
        report.ok(f"README.md says {suites} unit suites, and tests/ has {suites}")
    else:
        # Not a failure since 22 November: see the top of this file.
        report.note(f"tests/ has {suites} unit suites and README.md says {sorted(stated)}: "
                    "leave it, the landing session refreshes that count (AGENTS.md, \"Working together\")")

    if watch_on:
        back = sent_back(comments or [], number)
        if back and head.startswith(back[0]):
            report.no(f"the landing session sent it back at this head commit ({back[0][:7]}); what it wrote is on the "
                      f"board (`python3 tools/ci/check_pr.py landed {number}` prints it)")
    if report.failed:
        print("\nnot ready")
        return 1
    if watch_on:
        print(f"\ndo not merge: {watch.says()}.\n"
              f"#{number} is finished and green, and the landing session takes it as it is: push nothing more to it,\n"
              f"do not merge main into it, and wait for the answer:  python3 tools/ci/check_pr.py landed {number}")
        return 3
    if watch.state in ("over", "quiet"):
        report.note(watch.says())
    if comments is None:
        report.note("the board could not be read, so no watch can be seen: taken as none")
    print(f"\nready: gh pr merge {number} --merge, then watch `gh run list --branch main --limit 3` to the end "
          "(once a minute is enough)")
    return 0


def self_test() -> int:
    """The cases the reading of a claim is trusted for. Needs no network."""
    def claim(body: str, title: str = "WIP: a change", changed: tuple[str, ...] = ()) -> dict:
        return claim_of({"number": 1, "title": title, "body": body,
                         "files": [{"path": path} for path in changed]})

    named = claim("Task: a row\nFiles: `src/a.cpp`\nPhase 5, finding 7\nAgent: Heron\n\ntext")
    named_with_remark = claim("Task: a row\nFiles: `src/a.cpp`\nPhase 5, finding 7\nAgent: Heron, since 22 November.\n")
    numbered = claim("Task: a row\nFiles: `src/a.cpp` (new), `include/a.hpp`,\n`tests/test_suite.cpp`\nPhase 5, finding 7\nAgent: Heron\n\ntext, `not/a/file.cpp`")
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
        ("`Agent: Heron` is the agent's name", named["agent"], "Heron"),
        ("a name ends at the first space or stop", named_with_remark["agent"], "Heron"),
        ("the line `next_ids.py --pr-body` prints is not a name until it is written over",
         claim("Task: a row\nFiles: `src/a.cpp`\nPhase 5, finding 7\nAgent: <the name you gave yourself>\n")["agent"], None),
        ("no `Agent:` line is no name, and still a claim", (plain["agent"], plain["has_claim_lines"]), (None, True)),
        ("...and what `claim N` says about it is a note, which stops nothing",
         claim_problems(plain, [plain], Main({1}, {1}, {1}, {1}, 1, 1)),
         [(False, 'no `Agent:` line: give yourself a name and put it under the three lines (AGENTS.md, "Working together")')]),
        ("the board and a watch are not claims",
         [bool(NOT_A_CLAIM_RE.match(t)) for t in ("BOARD: hand-overs", "WATCH: 22 November", "WIP: a board", "Board games")],
         [True, True, False, False]),
        ("the board is never merged; a watch is, by the landing session, at its end",
         [bool(NEVER_MERGED_RE.match(t)) for t in ("BOARD: hand-overs", "WATCH: 22 November", "Land #140")],
         [True, False, False]),
        ("a claim with numbers is called by them", numbers_of(numbered), "Phase 5, finding 7"),
    ]

    # main has write-ups of phases 1-4 and 6 and of findings 1-6 and 8, and
    # floors of 6 and 8: Phase 5 and finding 7 are taken and have no write-up.
    main = Main(set(range(1, 7)), set(range(1, 9)), {1, 2, 3, 4, 6}, {1, 2, 3, 4, 5, 6, 8}, 6, 8)
    below = claim_problems(numbered, [numbered], main)
    written = claim_problems(claim("Task: a row\nFiles: `src/c.cpp`\nPhase 6, finding 8\nAgent: Heron\n"), [], main)
    free = claim_problems(claim("Task: a row\nFiles: `src/c.cpp`\nPhase 7, finding 9\nAgent: Heron\n"), [], main)
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
        watch_on = facts.pop("watch_on", False)
        lines, quick = ci_verdict(Evidence(**facts), watch_on)
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

    # The watch. What the board says, at a time: the last status comment decides.
    def at(stamp: str) -> datetime:
        return datetime.strptime(stamp, STAMP).replace(tzinfo=timezone.utc)

    def said(text: str) -> dict:
        return {"body": text, "url": "https://github.com/o/r/pull/9#issuecomment-1"}

    on = said("Lander: WATCH ON until 2026-11-23 12:00 UTC. Last look 2026-11-23 05:00 UTC.\n\n"
              + WATCH_ON_TEXT.format(quiet=QUIET_AFTER_MINUTES, name="Lander"))
    off = said("Lander: WATCH OFF since 2026-11-23 06:01 UTC. It landed #1 and #2.")
    back = said("Lander: #7 is yours again (head abc1234): it conflicts with main in `src/a.cpp`")
    board = [said("Heron: a question about #7"), on, back, said("Heron: thanks")]
    cases += [
        ("a board without a status comment: no watch",
         watch_state([said("Heron: hello")], at("2026-11-23 05:10")).state, "none"),
        ("ON, looked at ten minutes ago: a watch is on, whatever was written after the status comment",
         watch_state(board, at("2026-11-23 05:10")).state, "on"),
        ("...and it says until when, and who looked last",
         watch_state(board, at("2026-11-23 05:10")).says(),
         "a watch is on until 12:00 UTC on 23 November (Lander last looked at 05:00)"),
        (f"a last look {QUIET_AFTER_MINUTES} minutes old is still a watch",
         watch_state(board, at("2026-11-23 05:30")).state, "on"),
        ("one minute more and the watch has gone quiet: nobody waits for a landing session that is not there",
         (watch_state(board, at("2026-11-23 05:31")).state, watch_state(board, at("2026-11-23 05:31")).on),
         ("quiet", False)),
        ("a watch whose time is up is over, however lately it looked",
         watch_state([said("Lander: WATCH ON until 2026-11-23 12:00 UTC. Last look 2026-11-23 11:59 UTC.")],
                     at("2026-11-23 12:00")).state, "over"),
        ("OFF after ON: no watch", watch_state(board + [off], at("2026-11-23 06:05")).state, "off"),
        ("ON after OFF: the later one counts", watch_state([off, on], at("2026-11-23 05:10")).state, "on"),
        ("a line that only talks about a watch is not the status comment",
         watch_state([said("Heron: is a WATCH ON until tomorrow?"), said("WATCH ON until 2026-11-23 12:00 UTC.")],
                     at("2026-11-23 05:10")).state, "none"),
        ("the name in the status comment is the landing session's",
         watch_state([said("Osprey: WATCH OFF since 2026-11-23 06:01 UTC.")], at("2026-11-23 07:00")).name, "Osprey"),
        ("a look of the same watch rewrites its status comment; so does its end",
         [rewrites(watch_state(board, at("2026-11-23 05:10")), at("2026-11-23 12:00")),
          rewrites(watch_state(board, at("2026-11-23 05:10")), None),
          rewrites(watch_state(board, at("2026-11-23 13:00")), None)], [True, True, True]),
        ("a new watch (another end, or after an OFF, or on an empty board) gets a comment of its own",
         [rewrites(watch_state(board, at("2026-11-23 05:10")), at("2026-11-23 20:00")),
          rewrites(watch_state(board + [off], at("2026-11-23 07:00")), at("2026-11-23 12:00")),
          rewrites(watch_state([], at("2026-11-23 07:00")), at("2026-11-23 12:00")),
          rewrites(watch_state(board + [off], at("2026-11-23 07:00")), None)], [False] * 4),
        ("sent back: at which head, and the whole of what was written",
         sent_back(board, 7), ("abc1234", "Lander: #7 is yours again (head abc1234): it conflicts with main in `src/a.cpp`")),
        ("sent back: not a pull request the board says nothing of", sent_back(board, 8), None),
        ("sent back twice: the last one counts",
         sent_back(board + [said("Lander: #7 is yours again (head def5678): the lot fails with it")], 7)[0], "def5678"),
    ]

    def check(name: str, conclusion: str | None, status: str = "COMPLETED", started: str = "2026-11-23T05:00:00Z") -> dict:
        return {"__typename": "CheckRun", "name": name, "conclusion": conclusion, "status": status, "startedAt": started}

    green = [check(f"job {n}", "SUCCESS") for n in range(12)]
    cases += [
        ("no check on the head: none", rollup([]), ("none", [])),
        ("twelve green jobs: passed", rollup(green), ("passed", [])),
        ("a neutral job is green",
         rollup(green + [check("other", "NEUTRAL")]), ("passed", [])),
        ("two jobs passed and ten were skipped: the short run, not a whole one",
         rollup(green[:2] + [check(f"heavy {n}", "SKIPPED") for n in range(10)]), ("short", [])),
        ("a short run that is still going is running",
         rollup([check("job 0", "SUCCESS"), check("job 1", None, "IN_PROGRESS")] + [check(f"heavy {n}", "SKIPPED") for n in range(10)])[0],
         "running"),
        ("a short run whose build failed has failed",
         rollup([check("job 0", "FAILURE"), check("job 1", "SUCCESS")] + [check(f"heavy {n}", "SKIPPED") for n in range(10)])[0],
         "failed"),
        ("the same from the other API",
         [state_of(jobs)[0] for jobs in ([], [("a", "completed", "success")] * 12,
                                         [("a", "completed", "success"), ("b", "completed", "skipped")],
                                         [("a", "in_progress", ""), ("b", "completed", "skipped")],
                                         [("a", "completed", "failure"), ("b", "completed", "skipped")])],
         ["none", "passed", "short", "running", "failed"]),
        ("a short run on the head while a watch is on: that is what the landing session takes",
         verdict(head_state="short", ran_state="short", watch_on=True), ([True, True], False)),
        ("a short run on the head and no watch: nobody merges on two jobs",
         verdict(head_state="short", ran_state="short"), ([True, False], False)),
        ("no run on the head, the one before it was short, no watch: the same",
         verdict(head_state="none", ran_on="b" * 40, ran_state="short", since=["README.md"]), ([True, False], False)),
        ("one job still running: running, and it is named",
         rollup(green[:11] + [check("job 11", None, "IN_PROGRESS")]), ("running", ["job 11: in_progress"])),
        ("one job failed and all are done: failed",
         rollup(green[:11] + [check("job 11", "FAILURE")]), ("failed", ["job 11: failure"])),
        ("a job that was run again: its last attempt counts",
         rollup(green[:11] + [check("job 11", "FAILURE", started="2026-11-23T05:00:00Z"),
                              check("job 11", "SUCCESS", started="2026-11-23T05:30:00Z")]), ("passed", [])),
        ("a cancelled run is not a green one",
         rollup(green[:11] + [check("job 11", "CANCELLED")])[0], "failed"),
    ]

    def waiting(title: str = "A change (Phase 5, finding 7)", checks: list[dict] | None = None, head: str = "f" * 40,
                files: tuple[str, ...] = ("src/a.cpp", "README.md"), mergeable: str = "MERGEABLE", number: int = 7) -> dict:
        return {"number": number, "title": title, "headRefOid": head, "mergeable": mergeable, "isDraft": False,
                "statusCheckRollup": green if checks is None else checks, "files": [{"path": path} for path in files]}

    cases += [
        ("finished, green, mergeable: the landing session takes it, and nobody had to ask",
         why_not_taken(waiting(), board), ""),
        ("GitHub has not worked out yet whether it merges (UNKNOWN): taken, and git is asked",
         why_not_taken(waiting(mergeable="UNKNOWN"), board), ""),
        ("a title that begins with WIP: is not finished",
         why_not_taken(waiting(title="WIP: a change"), board), "its title still begins with WIP:"),
        ("CI still running on a finished pull request: taken, its run and the lot's go side by side",
         why_not_taken(waiting(checks=green[:11] + [check("job 11", None, "QUEUED")]), board), ""),
        ("CI failed: not taken, and the job is named",
         why_not_taken(waiting(checks=green[:11] + [check("job 11", "FAILURE")]), board),
         "CI failed on its head commit (job 11: failure)"),
        ("no run on the head of an engine change: not taken",
         why_not_taken(waiting(checks=[]), board), "no CI run on its head commit"),
        ("...unless the landing session has said it took that very head: its code was green, and the lot's run is its run",
         why_not_taken(waiting(checks=[]), board + [said("Lander: lot of 03:01 UTC: #5 (`aaaaaaa`), #7 (`fffffff`) with `main` `1234567`, one CI run.")]), ""),
        ("taken at another head than the one it has now: the rule as before",
         why_not_taken(waiting(checks=[]), board + [said("Lander: lot of 03:01 UTC: #7 (`eeeeeee`) with `main` `1234567`, one CI run.")]),
         "no CI run on its head commit"),
        ("taken, and sent back since: not taken",
         taken_at([said("Lander: lot of 03:01 UTC: #7 (`fffffff`) with `main` `1234567`."), said("Lander: #7 is yours again (head fffffff): a row")], 7), None),
        ("a lot line that names other pull requests says nothing of this one",
         taken_at([said("Lander: lot of 03:01 UTC: #17 (`fffffff`), #70 (`fffffff`) with `main` `1234567`.")], 7), None),
        ("the last lot of a watch is a lot",
         taken_at([said("Lander: **the last lot of this watch**, 23:41 UTC: #7 (`abcdef0`) as it is")], 7), None if not LOT_RE.match("Lander: **the last lot of this watch**, 23:41 UTC: #7 (`abcdef0`) as it is") else "abcdef0"),
        ("no run on the head of a change to documents only: taken, the lot's run is its run",
         why_not_taken(waiting(checks=[], files=("AGENTS.md", "docs/phases/5-a.md")), board), ""),
        ("a pull request with a short run is taken: the lot's run is the whole one",
         why_not_taken(waiting(checks=green[:2] + [check(f"heavy {n}", "SKIPPED") for n in range(10)]), board), ""),
        ("in conflict with main: not taken",
         why_not_taken(waiting(mergeable="CONFLICTING"), board), "it conflicts with main"),
        ("sent back at this very head: left alone until the head is another one",
         why_not_taken(waiting(head="abc1234" + "0" * 33), board),
         "it was sent back at this head commit (abc1234) and has not changed since"),
        ("sent back at a head before this one: taken again",
         why_not_taken(waiting(head="1234abc" + "0" * 33), board), ""),
        ("the board and a watch are never taken",
         [why_not_taken(waiting(title=t), board) for t in ("BOARD: hand-overs", "WATCH: 23 November")],
         ["it is the board or a watch"] * 2),
        ("while a watch is on, a main that moved by engine code is the landing session's: the agent is not told to merge it",
         verdict(**moved, main_files=["src/js/JsEval.cpp"], watch_on=True), ([True, True], False)),
        ("...and with no watch on it is still the agent's, as it was",
         verdict(**moved, main_files=["src/js/JsEval.cpp"]), ([False, True], False)),
        ("...a watch does not make a failed run good",
         verdict(**moved, main_files=["src/js/JsEval.cpp"], head_state="failed", head_bad=["Build: failure"],
                 watch_on=True), ([True, False], False)),
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
    landing = "--landing" in arguments
    arguments = [argument for argument in arguments if argument != "--landing"]
    if len(arguments) == 2 and arguments[0] in ("claim", "merge") and arguments[1].lstrip("#").isdigit():
        number = int(arguments[1].lstrip("#"))
        return check_claim(number) if arguments[0] == "claim" else check_merge(number, landing)
    if arguments[:1] == ["landed"] and len(arguments) in (2, 4) and arguments[1].lstrip("#").isdigit():
        wait = 240
        if len(arguments) == 4:
            if arguments[2] != "--wait" or not arguments[3].isdigit():
                print(__doc__.strip(), file=sys.stderr)
                return 2
            wait = int(arguments[3])
        return check_landed(int(arguments[1].lstrip("#")), wait)
    if arguments == ["watch"]:
        return show_watch()
    if arguments[:1] == ["watch"] and len(arguments) in (3, 5) and arguments[1] in ("--beat", "--off"):
        name = "Lander"
        if len(arguments) == 5:
            if arguments[3] != "--name" or not re.fullmatch(r"\w+", arguments[4]):
                print(__doc__.strip(), file=sys.stderr)
                return 2
            name = arguments[4]
        return beat(arguments[2], name) if arguments[1] == "--beat" else watch_off(arguments[2], name)
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
