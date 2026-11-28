#!/usr/bin/env python3
"""What the open pull requests claim, and whether one is ready to be merged.

    python3 tools/ci/check_pr.py             # the claims, the numbers taken, the next free pair
    python3 tools/ci/check_pr.py claim N     # after opening #N: are its numbers and its files free
    python3 tools/ci/check_pr.py merge N     # when #N is finished: merge it, or leave it to the watch
    python3 tools/ci/check_pr.py landed N    # during a watch: wait for the landing session's answer
    python3 tools/ci/check_pr.py watch       # is a watch on, and what would it take now
    python3 tools/ci/check_pr.py numbers     # the open pull requests that must change a number first
    python3 tools/ci/check_pr.py --self-test # how a claim and the board are read, on fixed texts; no network

With no argument, with `claim N`, `merge N` and `watch` a line begins `!!`
when a job of `main`'s own newest CI run has failed: the tree your branch was
made from may be broken, and by whom is in that run (`run_report.py --run`).

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
  - a file it names or has changed, and that `main` does not have, belongs
    to an open pull request with a lower number (a file `main` has can be
    changed by both: the one that lands second merges `main`, and `claim N`
    says so without refusing). The files every pull request changes (AGENTS.md lists
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
# The directories every pull request adds a file of its own to, named by its
# own number. A `Files:` line that names one of them bare ("`docs/phases/`")
# would claim every other pull request's file there, since a name that ends
# in `/` is every file under it: at 02:53 UTC on 25 November #390 and #395
# said so of all three, and `claim N` told each of the six pull requests
# opened after #390 that #390 "had" its phase, its finding and its suite
# first. Named bare they claim nothing; a file in them is claimed by name.
SHARED_DIRECTORIES = ("docs/phases", "docs/findings", "tests/suites")
# A file there that is named before its number is known: `docs/phases/N-*.md`.
# The `Files:` line is written when the pull request is opened, and the number
# is what opening it gives. At 06:09 UTC on 25 November #415 and #421 had both
# written `tests/suites/N-*.cpp, docs/phases/N-*.md, docs/findings/N-*.md`,
# the words of this file's own advice ("name your own files"), and `claim 421`
# said that #415 "had" the three first and that two pull requests cannot both
# make them; #421's agent wrote that it would add no phase, finding or suite
# while they were held. They are no file's name. A name there that begins
# with a number is a file's (`419-*.md` too: only #419 may make it).
NOT_YET_NUMBERED = re.compile(
    r"^(?:docs/phases|docs/findings|tests/suites)/"
    r"(?:(?:N+|n+|X+|x+|#+|PR)[-_.…*]|[<{\[]|\*|…|\.\.\.)")
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
# The third line of a claim since 24 November 2026: the pull request's own
# number is its phase and its finding (AGENTS.md, "Before starting", point
# 2). GitHub gives every pull request another number, so two agents who
# start in the same minute cannot take the same one, which the next free
# pair of `tools/next_ids.py` let them do: in one hour of that morning four
# pull requests of twelve had to be renumbered after they were finished.
OWN_NUMBER_RE = re.compile(r"^Phase and finding:\s*(?:this|the) pull request'?s?(?: own)? number\b", re.IGNORECASE)
# The label that starts every job of CI on a pull request when it is taken
# off (.github/workflows/ci.yml). Until 25 November 2026 it was also what
# made a pull request's runs short while it was on; every run a push starts
# is short now, with or without it.
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
    own_number = numbers is None and any(OWN_NUMBER_RE.match(line) for line in head)
    if own_number:
        phase = finding = pr["number"]
    elif numbers is None:
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
                             or OWN_NUMBER_RE.match(line) or NO_NUMBERS_RE.search(line)):
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
        "has_claim_lines": task is not None and has_files_line and (numbers is not None or own_number or no_numbers),
        "phase": phase,
        "finding": finding,
        "head": pr.get("headRefOid", ""),
    }


def claims_nothing(name: str) -> bool:
    """A file every pull request changes, a directory every one adds its own
    file to, or a file there whose number is not written in yet."""
    return name in SHARED or name.rstrip("/") in SHARED_DIRECTORIES or NOT_YET_NUMBERED.match(name) is not None


def same_files(mine: set[str], theirs: set[str]) -> list[str]:
    """Files two claims share. A name that ends in `/` is every file under it."""
    shared = set()
    for a in mine:
        if claims_nothing(a):
            continue
        for b in theirs:
            if claims_nothing(b):
                continue
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
                   "--json", "number,title,body,headRefOid,files,isDraft")


def draft_note(number: int, title: str, is_draft: bool) -> str | None:
    """What `claim N` says of work in progress that is no draft.

    GitHub merges a pull request with `WIP:` in its title for whoever asks,
    and twice somebody asked: #449 on 25 November 2026, 59 seconds after it
    was opened and with no CI run, and #491 on 28 November, during a watch.
    A draft it merges for nobody ("Pull Request is still a draft"), so a
    claim is opened as one (AGENTS.md, "Before starting", point 2).
    """
    if not title.startswith("WIP") or is_draft:
        return None
    return (f"#{number} is work in progress and no draft. Make it one now, `gh pr ready {number} --undo`, and ask "
            "again. A claim is a draft (AGENTS.md, \"Before starting\", point 2): GitHub merges a draft for "
            f"nobody, and work under `WIP:` for whoever asks. `gh pr ready {number}` again with your last push, "
            "when `WIP:` comes off")


def claim_end(number: int, numbers: str, claim_failed: bool, not_a_draft: str | None, next_numbers) -> list[tuple[str, str]]:
    """The last lines of `claim N`, each (`ok`, `no` or `note`, the text).

    A claim that is no draft is refused, with what to do (`draft_note`).
    It was a note at first, from 28 November 2026, 08:22 UTC, under lines
    that began "ok", and `claim N` ended with status 0. Of the claims opened
    in the six hours after that, five were opened ready all the same, four
    of them by one agent (#515, #521, #532, #539). Each was made a draft by
    the landing session, which its agent did not see: two of them then
    took their `WIP:` off and waited, finished drafts, for somebody to
    make them ready. An agent in a long chat knows the routine as it was
    when the chat began; what it reads every time is what the tools answer.

    `next_numbers()` is asked only when the claim itself fails (its
    numbers or its files are somebody else's): a claim that is right and no
    draft is not told to take other numbers.
    """
    lines = [("note", next_numbers())] if claim_failed else [
        ("ok", f"#{number}: {numbers}; no earlier open pull request has its numbers or its files")]
    if not_a_draft:
        lines.append(("no", not_a_draft))
    return lines


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


# Of the 22 pull requests that landed in the watch of the night of 25
# November 2026, 21 went from opened to merged in 9 to 76 minutes. A claim
# with no push and no word from its agent for this long is worth asking about.
QUIET_HOURS = 2.0

# Agents sign a comment in its first word: "Vireo: the claim check reports...",
# "**Merlin — claim: PR #414".
FIRST_WORD_RE = re.compile(r"^[\s*_>#`]*([A-Za-z][A-Za-z0-9]*)")


def last_words(comments: list[dict]) -> dict[str, tuple[str, int]]:
    """For each name that a comment begins with: (when its newest was made, on which pull request)."""
    newest: dict[str, tuple[str, int]] = {}
    for comment in comments:
        match = FIRST_WORD_RE.match(comment.get("body") or "")
        if match and (match.group(1) not in newest or comment["created_at"] > newest[match.group(1)][0]):
            newest[match.group(1)] = (comment["created_at"], int(comment["issue_url"].rsplit("/", 1)[1]))
    return newest


def recent_comments(hours: float) -> list[dict]:
    """The comments made on any pull request in the last `hours` hours: three calls at most.

    An answer that cannot be read is no comments: the list of claims is then
    shown without them, and says so, rather than not at all.
    """
    since = (datetime.now(timezone.utc) - timedelta(hours=hours)).strftime("%Y-%m-%dT%H:%M:%SZ")
    found: list[dict] = []
    for page in (1, 2, 3):
        answer = gh("api", f"{REPO}/issues/comments?since={since}&sort=created&direction=desc&per_page=100&page={page}",
                    check=False)
        try:
            comments = json.loads(answer)
        except ValueError:
            return found
        if not isinstance(comments, list):
            return found
        found += [comment for comment in comments if comment["created_at"] >= since]   # `since` goes by the last edit
        if len(comments) < 100:
            break
    return found


def quiet_note(title: str, changed: int, hours: float, agent: str | None, wrote: bool) -> str | None:
    """What to say of a pull request with no push for a long time and no word from its agent.

    `changed` is how many files it changes (none: a claim and nothing else);
    `wrote` is whether a comment in its agent's name was made anywhere in the
    last QUIET_HOURS. Both are needed. On 25 November the landing session
    closed #389 as given up, 2 h 40 min after its only push, from what #389's
    own page showed: its agent had written on #416 thirteen minutes before.
    It was reopened a quarter of an hour later. #367, 21 hours after its only
    push, had no word from its agent anywhere: that chat could not go on.
    The person retired that agent and gave its task to another, which landed
    it as #432 and left #367 open, as the note then told it to: it was closed
    ten hours later, when the person asked. The note now says who closes it.
    """
    if hours < QUIET_HOURS or wrote or not title.startswith("WIP"):
        return None
    what = "nothing in it but its claim" if changed == 0 else "work in progress"
    whose = f"no comment in {agent}'s name anywhere" if agent else "it names no agent whose comments could be looked for"
    return (f"QUIET: no push for {hours:.1f} h ({what}), and {whose} in the last {QUIET_HOURS:g} h. It is still a claim, "
            "and not yours to take or to close: ask in a comment on it, and tell the person you work for. "
            "(Only when the person has told you that its agent is retired and its task is yours: say so on it, "
            + ("" if changed == 0 else "take its commits, ") + "and close it. AGENTS.md, \"Before starting\", point 1)")


def next_pair() -> str:
    """What a new pull request takes: its own number, for both. (Until 24
    November 2026 this was 'Phase P, finding F', the next free pair, as the
    allocator gave it; `old_next_pair` still asks for that.)"""
    return ("a new pull request's phase and finding are its own number: open it, and use the number GitHub gives it "
            "(AGENTS.md, \"Before starting\", point 2)")


def old_next_pair() -> str:
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


def file_on_main(path: str) -> bool:
    """Is this file on `main`, as the clone has it from origin? (A name that ends in `/`: this directory.)"""
    return git("cat-file", "-e", f"origin/main:{path.rstrip('/')}").returncode == 0


def claim_problems(claim: dict, claims: list[dict], main: Main, on_main_file=None) -> list[tuple[bool, str]]:
    """What is wrong with a claim: (does this pull request have to change, what).

    `on_main_file(path)` says whether a file is on `main`; the clone is asked
    when it is not given.
    """
    on_main_file = on_main_file or file_on_main
    problems: list[tuple[bool, str]] = []
    if not claim["has_claim_lines"]:
        problems.append((True, "its text does not begin with the Task: and Files: lines and the line of its numbers "
                               "(`Phase and finding: this pull request's number`)"))
    if claim["has_claim_lines"] and not claim.get("agent"):
        problems.append((False, "no `Agent:` line: give yourself a name and put it under the three lines "
                                "(AGENTS.md, \"Working together\")"))
    bare = sorted(name for name in claim["files"] if name.rstrip("/") in SHARED_DIRECTORIES)
    if bare:
        problems.append((False, f"its `Files:` line names {', '.join(f'`{name}`' for name in bare)}: every pull request adds a "
                                f"file of its own there, so that claims nothing. Name the file (`{bare[0].rstrip('/')}/"
                                f"{claim['number']}-...`)"))
    # A file it adds where every pull request adds its own is called by its own
    # number. #393 was written as Phase 387 while #387 to #392 were being
    # opened, and was taken into a lot with `docs/phases/387-...`; #387 would
    # have found a second Phase 387 on `main`.
    for name in sorted(claim["files"]):
        placed = re.match(r"(docs/phases|docs/findings|tests/suites)/(\d+)-", name)
        if not placed:
            continue
        mine = claim["finding"] if placed.group(1) == "docs/findings" else claim["phase"]
        written = main.written_findings if placed.group(1) == "docs/findings" else main.written_phases
        number = int(placed.group(2))
        if mine is None or number == mine or number in written:
            continue        # its own, or a write-up or suite `main` has, changed here
        problems.append((True, f"`{name}` is called by {number}, and this pull request's number is {mine}: "
                               f"rename it (`{placed.group(1)}/{mine}-...`)"))
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
                                    "it is the one that gives way"))
            continue
        # A file both would make is the first one's: there is nothing to merge
        # it with. A file `main` has is another matter. Until 25 November 2026
        # that was a refusal too, and it read "stay out of it, or tell the
        # person you work for": #390 was finished and measured in its agent's
        # sandbox, needed one function of src/js/JsInterpreter.cpp, a file
        # #387 had named eight minutes before #390 was opened, and its agent
        # closed it unpushed. #387 landed 45 minutes after that. Whoever
        # lands second merges `main`, which every pull request that `main`
        # has overtaken does anyway.
        made = [name for name in both if not on_main_file(name)]
        shared = [name for name in both if name not in made]
        if made:
            problems.append((True, f"#{other['number']} had {', '.join(made)} first, and `main` has no such file: "
                                   f"two pull requests cannot both make {'it' if len(made) == 1 else 'them'}. "
                                   "Take another name, or tell the person you work for"))
        if shared:
            problems.append((False, f"#{other['number']}, opened first, changes {', '.join(shared)} too. Go on, and change "
                                    f"no more of {'it' if len(shared) == 1 else 'them'} than the task needs: if "
                                    f"#{other['number']} lands first and the two no longer merge, merge `main` into this one"))
    return problems


def number_clashes(claims: list[dict], main: Main) -> list[tuple[int, str]]:
    """(pull request, why) for every open claim that has to change a number
    before it can land: the number is on `main`, or an earlier pull request
    says it.

    The landing session asks this before it takes a pull request. On 24
    November #336 was taken as Phase 269, which #333 had said seven minutes
    before it; its agent found that out from `merge N`, renumbered, and
    pushed under the lot, twice in a quarter of an hour (the second number
    was #343's). One push cost the lot a run that was nine minutes old, the
    other left #336's last commit out of what landed.
    """
    found: list[tuple[int, str]] = []
    for claim in claims:
        for blocking, text in claim_problems(claim, claims, main):
            if blocking and re.match(r"(?:Phase|finding) \d+", text):
                found.append((claim["number"], text))
    return found


def show_number_clashes() -> int:
    for number, why in number_clashes(open_claims(), on_main()):
        print(f"#{number}: {why}")
    return 0


def main_note(run: dict | None, jobs: list[dict]) -> str | None:
    """What an agent is told about the newest CI run on `main`: nothing until a
    job of it has failed. `run` is an entry of `gh run list --json
    databaseId,headSha,status,conclusion`, `jobs` the jobs of that run.

    #449 was merged on 25 November at 21:32:51 with no CI run and broke
    `main` for nine minutes. The first job of `main`'s own run had failed 49
    seconds after the merge, 7 of 18 in the end, and nothing read that run:
    a pull request begun at 21:35 (#451) was branched from the broken tree,
    and its agent was told by hand half an hour later."""
    if not run:
        return None
    bad = sorted(job["name"] for job in jobs if (job.get("conclusion") or "") in ("failure", "timed_out"))
    if not bad:
        return None
    so_far = "" if run.get("status") == "completed" else " so far"
    names = ", ".join(bad[:3]) + (f" and {len(bad) - 3} more" if len(bad) > 3 else "")
    return (f"`main` is broken at {run['headSha'][:7]}: {len(bad)} job(s) of its own CI run failed{so_far} ({names}). "
            f"`python3 tools/ci/run_report.py --run {run['databaseId']}` says why. If a merge did it, a branch made "
            "from this `main` has it too: AGENTS.md, \"Working together\", point 6, says what to do; "
            "measure nothing on that tree, and `git fetch origin && git merge origin/main` again when `main` is mended")


def newest_run_on_main(answer: dict | None) -> dict | None:
    """The newest run a push to `main` started, as `main_note` takes it, from
    what `gh api repos/.../actions/runs?branch=main&event=push&per_page=1`
    answers; None when it lists none."""
    runs = (answer or {}).get("workflow_runs") or []
    if not runs or not isinstance(runs[0], dict):
        return None
    run = runs[0]
    return {"databaseId": run.get("id"), "headSha": run.get("head_sha") or "",
            "status": run.get("status"), "conclusion": run.get("conclusion")}


def unread_main_note(why: str) -> str:
    """The line for a lookup that failed: it says so, and how to look."""
    said = " ".join((why or "no answer").split())[:140]
    return (f"whether `main` is broken could not be read here ({said}). Look yourself before you branch from it or "
            "measure on it: `gh run list --branch main --limit 3`")


def main_run_note() -> str | None:
    """`main_note` for the newest run that a push to `main` started. A lookup
    that fails says that it failed: this is a warning, never a reason to stop.

    Asked of the API. Until 28 November it was asked with `gh run list
    --event push`, and a lookup that failed said nothing. In the sandbox of
    that day `gh` is 2.23.0, which has no `--event`: the command ended with
    "unknown flag", and this line, which is how an agent learns that `main`
    is broken (AGENTS.md, "Working together", point 6), told nobody anything.
    Since when is not known. It was found by typing the same flag by hand.
    """
    try:
        listed = subprocess.run(("gh", "api", f"{REPO}/actions/runs?branch=main&event=push&per_page=1"),
                                capture_output=True, text=True, timeout=30)
        if listed.returncode != 0:
            return unread_main_note(listed.stderr or listed.stdout)
        run = newest_run_on_main(json.loads(listed.stdout or "{}"))
        if not run or (run.get("status") == "completed" and run.get("conclusion") == "success"):
            return None
        jobs = subprocess.run(("gh", "api", f"{REPO}/actions/runs/{run['databaseId']}/jobs?per_page=100"),
                              capture_output=True, text=True, timeout=30)
        if jobs.returncode != 0:
            return unread_main_note(jobs.stderr or jobs.stdout)
        return main_note(run, json.loads(jobs.stdout or "{}").get("jobs", []))
    except (OSError, ValueError, KeyError, TypeError, AttributeError, subprocess.SubprocessError) as error:
        return unread_main_note(f"{type(error).__name__}: {error}")


# What `gh pr merge` offers when a rule on `main` refuses a merge, and no agent takes.
NEVER = "Never `gh pr merge --admin`, and not `--auto` either: `gh` offers both when it refuses"

# The marks at which GitHub starts no run (GitHub Docs, "Skipping workflow
# runs"); this repository writes the first.
SKIP_MARKS = ("[skip ci]", "[ci skip]", "[no ci]", "[skip actions]", "[actions skip]")


def carries_skip_mark(message: str) -> bool:
    """The message of a commit on which GitHub starts no run: not for the
    push, and not for anything that happens to a pull request whose head it
    is. Taking the label `lot` off starts every job on any other head; on
    28 November at 11:34 it was put on #520 and taken off, whose head commit
    carried the mark, and no run started, while this file had answered
    "every job of CI has been started"."""
    return any(mark in message for mark in SKIP_MARKS) or bool(re.search(r"(?mi)^skip-checks: *true *$", message))


def required_from(branch: dict | None, rules: list | None) -> list[str]:
    """The checks a rule of GitHub's on `main` wants passed on a head commit,
    from what a session may read: `gh api repos/.../branches/main` (a rule
    set under Settings, Branches) and `gh api repos/.../rules/branches/main`
    (a ruleset). None of either: no names."""
    names: list[str] = []
    protection = (branch or {}).get("protection") or {}
    names += [str(name) for name in (protection.get("required_status_checks") or {}).get("contexts") or []]
    for rule in rules or []:
        if isinstance(rule, dict) and rule.get("type") == "required_status_checks":
            wanted = (rule.get("parameters") or {}).get("required_status_checks") or []
            names += [str(check.get("context")) for check in wanted if isinstance(check, dict) and check.get("context")]
    return sorted(set(names))


def required_checks() -> list[str]:
    """`required_from` for this repository. A lookup that fails gives no
    names: the names are for the line that is printed, and what decides is
    GitHub's verdict on the pull request (`rule_note`)."""
    try:
        branch = subprocess.run(("gh", "api", f"{REPO}/branches/main"), capture_output=True, text=True, timeout=30)
        rules = subprocess.run(("gh", "api", f"{REPO}/rules/branches/main"), capture_output=True, text=True, timeout=30)
        got_branch = json.loads(branch.stdout or "{}") if branch.returncode == 0 else {}
        got_rules = json.loads(rules.stdout or "[]") if rules.returncode == 0 else []
        return required_from(got_branch if isinstance(got_branch, dict) else {},
                             got_rules if isinstance(got_rules, list) else [])
    except (OSError, ValueError, KeyError, TypeError, AttributeError, subprocess.SubprocessError):
        return []


def taken_while_it_runs(state: str | None, head_state: str, watch_on: bool) -> bool:
    """Under the rule on `main` a head whose own run is going reports BLOCKED.
    While a watch is on that is no refusal of its own: the landing session
    takes a finished pull request while its run goes ("Working together",
    point 4), and merges it when that run and the lot's have both passed.
    The answer is "not ready" all the same, for the run that is going (the
    line `CI on ...: in_progress`), as it was before the rule. This keeps
    the rule's line from saying it a second time, as "GitHub will not merge
    #N as it is", of a pull request that is in a lot by then (#528, 28
    November, 12:04)."""
    return bool(watch_on) and state == "BLOCKED" and head_state == "running"


def rule_note(state: str | None, number: int, head: str, head_state: str, required: list[str],
              unfinished: bool = False) -> str | None:
    """What a rule of GitHub's on `main` has against merging #N as it is, as a
    line for the report; None when GitHub has nothing against it. `state` is
    the pull request's `mergeStateStatus`, `head_state` what `check_runs`
    says of the head commit, `required` the checks the rule names (none when
    they could not be read).

    Nothing in this repository can stop a merge: #449 was merged on 25
    November with no CI run, 59 seconds after it was opened. A rule on `main`
    that wants a passed run of named checks on the head commit can, and only
    the person can set it: a session can neither set it nor read it whole
    (403). What a session can read is GitHub's verdict on one pull request,
    and this goes by that: BLOCKED is such a rule saying no, BEHIND is a rule
    that wants `main` in the branch.

    It matters because several answers below are "ready" for a head commit
    with no run of its own (pushed with the mark that skips CI: "since then
    only documents changed", "it changes documents only", the same tree).
    Under such a rule GitHub refuses those, and the message `gh` then prints
    offers `--admin`, which is the one thing never to do: it is the rule
    being switched off for one merge.

    Written on 28 November 2026 in the morning, against made-up answers. The
    person set the rule that day: a ruleset on `main`, in force from 11:28
    UTC, which wants `Build & unit tests` and `Documentation shape`. What
    the first minutes under it showed:

    - at 11:34 `gh pr merge 520 --merge --match-head-commit ...` on a head
      with no run was refused: "Pull request #520 is not mergeable: the base
      branch policy prohibits the merge", status 1, and two more lines that
      offer `--auto` and `--admin`. Neither is for an agent: `--admin` is
      the rule switched off for one merge, and `--auto` asks GitHub to
      merge by itself when the two checks have passed, at a time nobody
      chose and on the short run alone (the repository has it switched
      off: `allow_auto_merge` is false);
    - every claim that was open and not in conflict reported BLOCKED from
      the minute the rule was on: no run on its head, since a claim's
      pushes carry the mark. That is `unfinished` here: nothing is said
      then, because the pull request is not finished and the lines above
      this one say so. Asked of #487, a draft under `WIP:` in the middle
      of its work, this said "push a commit without the mark (an empty one
      will do)", which would have been a run for nothing. (BLOCKED says
      nothing of a draft as such: #520, a draft, reported UNSTABLE at
      11:51 with the two checks passed and one job going, and CLEAN when
      all had passed. GitHub merges no draft all the same);
    - `gh pr checks N --required` says "no checks reported" of a head with
      no run: it shows what the rule got only once something has run.
    """
    if unfinished:
        return None
    if state == "BLOCKED":
        names = ", ".join(f"`{name}`" for name in required) or "the checks it names"
        start = (f"GitHub will not merge #{number} as it is (it reports BLOCKED): the rule the person set on `main` "
                 f"wants a passed run of {names} on the head commit {head[:7]}")
        never = " " + NEVER
        if head_state == "none":
            return start + (", and that commit has no run: it was pushed with the mark that skips CI. Push a commit "
                            "without the mark (an empty one will do: `git commit --allow-empty -m \"Start CI on "
                            "the head\"`) and ask again when its run has passed." + never)
        if head_state == "running":
            return start + ", and its run is still going: ask again when it has ended." + never
        if head_state == "failed":
            return start + ", and its run failed (the line above)." + never
        return start + (f", and the run on that commit did not give it that: `gh pr checks {number} --required` shows "
                        "what the rule wants and what it got. Tell the person." + never)
    if state == "BEHIND":
        return (f"GitHub will not merge #{number} as it is (it reports BEHIND): a rule on `main` wants `main` in the "
                f"branch. Merge it (`git fetch origin && git merge origin/main`), push without the mark that skips "
                "CI and let that run pass. And tell the person: with \"Require branches to be up to date before "
                "merging\" on, a landing session cannot land a lot of several pull requests. "
                + NEVER)
    return None


def show_claims() -> int:
    prs = open_pull_requests()
    claims = open_claims(prs)
    main = on_main()
    print(f"main: phases up to {max(main.phases)}, findings up to {max(main.findings)}")
    changed = {pr["number"]: len(pr.get("files") or []) for pr in prs}
    words = last_words(recent_comments(QUIET_HOURS))
    for claim in claims:
        pushed = hours_since(commit(claim["head"])["commit"]["committer"]["date"])
        name = f"  [{claim['agent']}]" if claim.get("agent") else ""
        wrote = words.get(claim.get("agent") or "")
        said = f"; {claim['agent']} wrote {hours_since(wrote[0]):.1f} h ago in #{wrote[1]}" if wrote else ""
        print(f"\n#{claim['number']}{name}  {numbers_of(claim)}  (last commit {pushed:.1f} h ago{said})")
        print(f"    {claim['title'][:110]}")
        own = sorted(name for name in claim["files"] if not claims_nothing(name))
        print("    files: " + (", ".join(own) if own else "(none)"))
        quiet = quiet_note(claim["title"], changed.get(claim["number"], 0), pushed, claim.get("agent"), bool(wrote))
        if quiet:
            print(f"    !! {quiet}")
        for blocking, problem in claim_problems(claim, claims, main):
            print(f"    {'!!' if blocking else '  '} {problem}")
    for pr in sorted(prs, key=lambda pr: pr["number"]):
        if NOT_A_CLAIM_RE.match(pr["title"]):
            print(f"\n#{pr['number']}  not a claim: {pr['title'][:100]}")
    print(f"\nnumbers: {next_pair()}")
    broken = main_run_note()
    if broken:
        print(f"\n!! {broken}")
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
    if counted.returncode != 0:
        # The clone does not have one of the two commits, so git counted
        # nothing. Until 25 November 2026 that was read as nought: asked
        # about #415 from a clone that had not fetched its head, `claim 415`
        # said "main is in the head commit" of a pull request that was 45
        # commits behind `main` and in conflict with it.
        return -1, (f"this clone does not have the commit {head[:7]}, so nobody can say here whether it merges "
                    f"with main: fetch it (`git fetch origin refs/pull/N/head`) and ask again")
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
    if claim.get("number") and git("cat-file", "-e", claim["head"] + "^{commit}").returncode != 0:
        fetched(f"refs/pull/{claim['number']}/head")     # another agent's pull request: its head is not here yet
    behind, why = staleness(claim["head"], main_sha)
    if behind < 0:
        return [(True, why)]
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
    its = next(pr for pr in prs if pr["number"] == number)
    not_a_draft = draft_note(number, its["title"], bool(its.get("isDraft")))
    for kind, text in claim_end(number, numbers_of(mine), report.failed, not_a_draft, next_pair):
        getattr(report, kind)(text)
    broken = main_run_note()
    if broken:
        report.note(f"!! {broken}")
    return 1 if report.failed else 0


def state_of(latest: list[tuple[str, str, str]]) -> tuple[str, list[str]]:
    """'passed', 'short', 'failed', 'running' or 'none' for the jobs of a commit,
    each (name, status, conclusion) in lower case, and the ones that are not green.

    'short' is the run every push to a pull request gets
    (.github/workflows/ci.yml): the build, the unit suites, the documents
    and the corpora passed, and every other job was skipped, because the
    whole of CI runs on the lot the pull request lands in, or when `merge
    N` asks for it. It is enough to be taken into a lot. It is not enough
    for anybody to merge on.
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
    return (f"only the short run of CI passed on {head[:7]} (build, unit suites, documents, the corpora): that is "
            "what every push to a pull request gets, and the sanitizer jobs and the deep pages were skipped. "
            "Nobody merges on that"
            + ("" if number is None else
               f". The label is off now, which starts every job on {head[:7]}: ask again when they have ended"))


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


UNMARKED_END = "Push a commit without the mark (an empty one will do), and ask again"


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
    merged_alone: bool = False      # no run on the head, which is `ran_on` with main merged in by git alone, documents apart


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
    # What to do about a head that is a green commit with main merged in and
    # has no run, when no watch is on and somebody is to merge on it.
    unmarked = "" if not (e.merged_alone and e.ran_on) else (
        f". {head} is {e.ran_on[:7]} with main merged in by git alone, documents apart, and no run started on it: "
        "its message carries the mark that skips CI, or it was in conflict with main when it was pushed. "
        + UNMARKED_END)
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
        lines.append((True, f"it changes documents only ({some(e.pr_files)}): the whole of CI need not run on it"))
    elif e.head_state == "passed":
        lines.append((True, f"CI passed on the head commit {head}"))
    elif e.head_state == "short":
        if watch_on:
            lines.append((True, f"the short run of CI passed on the head commit {head} (build, unit suites, documents, "
                                "the corpora); the whole of CI runs on the lot it lands in"))
        else:
            lines.append((False, short_run_note(e.head)))
    elif e.head_state == "running":
        lines.append((False, f"CI on {head}: " + "; ".join(e.head_bad)))
    elif e.ran_on is None:
        lines.append((False, f"no CI run on {head} or on the 30 commits before it"))
    elif e.ran_state == "short" and not watch_on:
        lines.append((False, f"no CI run on {head}, and " + short_run_note(e.ran_on) + unmarked))
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
    elif e.merged_alone and watch_on:
        # AGENTS.md, "Working together", point 3: a pull request that went
        # back for a conflict in documents comes back as `main` merged into
        # the head that was green, under the mark that skips CI, and is taken
        # with no run of its own. The landing session's tool has taken such
        # heads since 24 November (tools/watch/watch.sh, `code_was_green`);
        # until 28 November this one said "not ready" of them.
        lines.append((True, f"CI passed on {e.ran_on[:7]}, and {head} is that commit with main merged in by git alone, "
                            "documents apart: while a watch is on it needs no run of its own. The landing session "
                            "takes it as it is, and every job of CI runs on the lot it lands in"))
    else:
        code = None if e.since is None else [f for f in e.since if not DOCUMENT.search(f)]
        lines.append((False, f"no CI run on {head}; since the last one, on {e.ran_on[:7]}, these changed: {some(code)}"
                             + unmarked))

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


def run_says(runs: list[dict]) -> str:
    """'green', 'failed' or '' for the check runs of a commit, as the landing
    session's tool reads them (tools/watch/watch.sh, `green`): green when
    every one has ended and passed, ten or more, or two or more passed and
    the rest were skipped, which is the short run of a push. Failed when
    one failed or ran out of time. A run the next push cancelled, one that
    is still going and no run at all say nothing.
    """
    ended = [(run.get("conclusion") or "").lower() for run in runs if (run.get("status") or "").lower() == "completed"]
    if any(conclusion in ("failure", "timed_out") for conclusion in ended):
        return "failed"
    if len(ended) != len(runs) or any(conclusion not in ("success", "skipped") for conclusion in ended):
        return ""
    passed = ended.count("success")
    return "green" if passed >= (2 if "skipped" in ended else 10) else ""


def code_was_green(head: str, main_sha: str, says, run_git=None) -> str | None:
    """For a head commit with no CI run of its own: the commit before it on
    its branch whose run passed and whose code the head's code is, or None.

    The head's code is that commit's when the head is that commit with
    `main` merged in by git alone, where `main` was merged since, and with
    documents only changed on top: a conflict in ROADMAP.md put right, a
    write-up finished, an empty commit. The landing session asks this before
    it takes such a head (tools/watch/watch.sh, `code_was_green`, the same
    steps), and the run on the lot is the head's test.

    Asked here since 28 November 2026, so that an agent is told what the
    landing session does. That morning #492 was in conflict with `main` in
    one row of ROADMAP.md; its agent merged `main` and put the row right
    under the mark that skips CI, as "Working together" says to, and `landed
    492` answered "the landing session does not take #492 as it is: no CI
    run on its head commit". The agent pushed an empty commit for a run. The
    lot was built half a minute after that push; built before it, the push
    would have moved the head under the lot. `merge N` said "not ready" of
    such a head as well.

    `says(commit)` is `run_says` of a commit's check runs. Only the clone is
    asked otherwise: the caller has fetched the head and `main`. Whatever
    git cannot tell here (a clone too shallow to hold the commits) is a no.
    """
    run_git = run_git or git
    chain = run_git("rev-list", "--first-parent", "-8", head)
    if chain.returncode != 0:
        return None
    green = None
    for sha in chain.stdout.split()[1:]:
        if run_git("merge-base", "--is-ancestor", sha, main_sha).returncode == 0:
            return None                 # a commit of main: nothing of the branch's own was ever tested
        said = says(sha)
        if said == "failed":
            return None
        if said == "green":
            green = sha
            break
    if green is None:
        return None
    base = run_git("merge-base", head, main_sha)
    if base.returncode != 0 or not base.stdout.strip():
        return None
    if run_git("merge-base", "--is-ancestor", base.stdout.strip(), green).returncode == 0:
        made = run_git("rev-parse", f"{green}^{{tree}}")        # main was not merged in since the run
    else:
        # What git alone makes of the two. With a conflict it ends in 1 and
        # still names a tree, the conflict's marks in it: the head may differ
        # from that in the document the conflict was in.
        made = run_git("merge-tree", "--write-tree", green, base.stdout.strip())
    if made.returncode not in (0, 1) or not made.stdout.split():
        return None
    differ = run_git("diff", "--name-only", made.stdout.split()[0], f"{head}^{{tree}}")
    if differ.returncode != 0 or not all_of(DOCUMENT, [name for name in differ.stdout.splitlines() if name]):
        return None
    return green


LEANS: dict[tuple[str, str], str | None] = {}


def leans_on(number: int, head: str, main_sha: str) -> str | None:
    """`code_was_green` of #number's head commit, which has no run: the head
    and `main` are fetched first, and the answer is kept, because `landed N`
    asks once a minute."""
    key = (head, main_sha)
    if key in LEANS:
        return LEANS[key]
    found = None
    if fetched(f"refs/pull/{number}/head") == head:
        if git("cat-file", "-e", f"{main_sha}^{{commit}}").returncode != 0:
            fetched("main")
        for _ in range(4):      # a sandbox's clone is shallow: go back until the two have an ancestor in common
            if git("merge-base", main_sha, head).returncode == 0:
                break
            if git("rev-parse", "--is-shallow-repository").stdout.strip() != "true":
                break
            git("fetch", "--quiet", "--deepen=500", "origin", "main", f"refs/pull/{number}/head")
        found = code_was_green(head, main_sha, lambda sha: run_says(
            (gh_json("api", f"{REPO}/commits/{sha}/check-runs?per_page=100") or {}).get("check_runs", [])))
    LEANS[key] = found
    return found


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


def why_not_taken(pr: dict, comments: list[dict], leans=None) -> str:
    """Why the landing session leaves a pull request alone during a watch; "" when it takes it.

    Taken is: finished, green on the head, not in conflict with `main`, and
    not sent back at this very head. Nobody has to ask: on the night of 22
    November three finished and green pull requests stood for up to an hour
    and three quarters while a watch was on, because their agents were to try
    twice themselves first and then write a line, and their turns had ended.

    `leans(pr)` says whether a head with no run is a green commit with
    `main` merged in by git alone (`leans_on`): the landing session takes
    such a head, and since 28 November 2026 this says so too.
    """
    if NOT_A_CLAIM_RE.match(pr["title"]):
        return "it is the board or a watch"
    if pr["title"].startswith("WIP"):
        return "its title still begins with WIP:"
    if pr.get("isDraft"):
        # Said by itself: on 24 November #385 was a draft whose title had
        # lost its WIP:, its agent was told "its title still begins with
        # WIP:", and the landing session's own tool, which read titles
        # only, took it into a lot. GitHub merges no draft.
        return f"it is a draft: `gh pr ready {pr['number']}` when it is finished"
    if pr.get("files") == []:
        # A claim whose title says nothing of work in progress. #423 was
        # opened at 06:19 UTC on 25 November as "Claim: full <html> root
        # layout box ...", one empty commit, and four minutes later stood
        # in the landing session's list as "main is in the head: it can be
        # merged as it is, at once". Merged, it would have ended its
        # agent's branch before the work began.
        return ("it changes no file: there is nothing in it to land. If it is a claim, its title begins with "
                "WIP: until the work is finished and pushed")
    taken = taken_at(comments, pr["number"])
    if taken and pr["headRefOid"].startswith(taken):
        return ""       # the landing session has said so, of this very head
    state, bad = rollup(pr.get("statusCheckRollup"))
    if state == "none":
        if pr.get("mergeStateStatus") == "BLOCKED":
            # The rule on `main` (28 November 2026, 11:28 UTC): GitHub merges
            # no head without a passed run, a change to documents like any
            # other. Until then such a head was taken, the lot's run being
            # its run, and this went on saying so. #548, three documents,
            # every push under the mark, stood here as "the landing session
            # takes these" at 14:39 that day while the landing session's
            # own tool left it out without a word.
            return ("no CI run on its head commit, and since the rule on main GitHub merges none without: every push "
                    "to it carried the mark that skips CI. Push a commit without the mark (an empty one will do)")
        files = [entry["path"] for entry in pr.get("files") or []]
        if not (files and all_of(DOCUMENT, files)):     # documents only: the lot's run is its run
            if leans is None or not leans(pr):
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


WATCH_PRS = "number,title,body,headRefOid,files,isDraft,mergeable,mergeStateStatus,statusCheckRollup"


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
        why = why_not_taken(pr, comments, lambda pr: leans_on(pr["number"], pr["headRefOid"], main_sha))
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
    broken = main_run_note()
    if broken:
        # The landing session's `watch.sh look` shows the lines of this that are new.
        print(f"\n!! {broken}")
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
        why = why_not_taken(pr, comments, lambda pr: leans_on(pr["number"], pr["headRefOid"], main_commit()))
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
    pr = gh_json("pr", "view", str(number), "--json",
                 "number,title,body,state,mergeable,mergeStateStatus,headRefOid,files,labels,isDraft")
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
    elif pr.get("isDraft"):
        report.no(f"the title has no WIP:, and #{number} is a draft: GitHub merges no draft, and the landing session "
                  f"takes none. `gh pr ready {number}` when it is finished")
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
    leaned = leans_on(number, head, main_sha) if head_state == "none" else None
    if leaned:
        # No run on the head, and its code is the code of a commit before it
        # whose run passed: `main` was merged in by git alone since, and
        # documents changed. (Some of that commit's jobs may have been
        # cancelled by a later push and passed in another run: "short" then.)
        there, _bad = check_runs(leaned)
        ran_on, ran_state, since = leaned, (there if there in ("passed", "short") else "short"), delta(leaned, head)[1]
    elif head_state == "none":
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
                                       ran_on, ran_state, since, same_tree, bool(leaned)), watch_on)
    for ok, text in lines:
        (report.ok if ok else report.no)(text)
    # A rule of GitHub's on `main`, if the person has set one: asked of GitHub,
    # and the rule's own names only when it says no.
    github = pr.get("mergeStateStatus")
    unfinished = pr["title"].startswith("WIP") or bool(pr.get("isDraft"))
    refusal = rule_note(github, number, head, head_state,
                        required_checks() if github in ("BLOCKED", "BEHIND") and not unfinished else [], unfinished)
    if refusal and taken_while_it_runs(github, head_state, watch_on):
        report.note(f"GitHub reports BLOCKED until the run on {head[:7]} has passed: the rule on `main`. The landing "
                    f"session takes #{number} while that run goes, and merges it when it and the lot's have passed")
    elif refusal:
        report.no(refusal)
    if "short" in (head_state, ran_state) and not watch_on and not landing:
        # No watch is on, so this agent merges, and the whole of CI has not
        # run on what it would merge. Every run of a pull request is short
        # (.github/workflows/ci.yml) but the one that taking a label off
        # starts, when the title has no WIP:.
        labelled = any(label.get("name") == LOT_LABEL for label in pr.get("labels") or [])
        if pr["title"].startswith("WIP"):
            # Any run started now would be short again.
            report.note(f"every job of CI runs on #{number} once its title has no WIP: take the WIP: off, then ask again")
        elif head_state == "none" and carries_skip_mark(str((commit(head).get("commit") or {}).get("message") or "")):
            # Nothing starts a run on this head, the label neither.
            if not refusal:
                report.note(f"the head commit {head[:7]} carries the mark that skips CI, and nothing starts a run on such "
                            f"a commit: taking the label `{LOT_LABEL}` off does not either. Push a commit without the "
                            "mark (an empty one will do: `git commit --allow-empty -m \"Start CI on the head\"`), "
                            "and ask again when its run has passed: that starts every job")
        elif labelled:
            # Taking the label off starts every job.
            gh("api", "-X", "DELETE", f"{REPO}/issues/{number}/labels/{LOT_LABEL}", check=False)
            report.note(f"the label `{LOT_LABEL}` is off #{number} now, which starts every job of CI on {head[:7]}: "
                        f"ask again when they have ended (`gh pr checks {number}`). If none has started in a minute, "
                        "ask again, which starts them again: a push would start the short run and no more")
        else:
            # No label to take off. Putting the label on starts nothing;
            # taking it off starts every job.
            gh("api", "-X", "POST", f"{REPO}/issues/{number}/labels", "-f", f"labels[]={LOT_LABEL}", check=False)
            gh("api", "-X", "DELETE", f"{REPO}/issues/{number}/labels/{LOT_LABEL}", check=False)
            report.note(f"the run on {head[:7]} was the short one, as every run a push starts is. "
                        f"Every job of CI has been started on {head[:7]} now (the label `{LOT_LABEL}` was put on #{number} "
                        f"and taken off, which does it): ask again when they have ended (`gh pr checks {number}`). "
                        "If none has started in a minute, ask again, which starts them again: a push would start "
                        "the short run and no more")
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
        report.no("the text does not begin with the Task: and Files: lines and the line of its numbers "
                  "(`Phase and finding: this pull request's number`)")
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
    broken = main_run_note()
    if broken:
        report.note(f"!! {broken}")
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


LEANING_HEADS = [
    ("main merged in by git alone under the mark", True),
    ("...and an empty commit on top, for a run that has not come", True),
    ("...and a write-up finished on top", True),
    ("a conflict in ROADMAP.md put right in the merge", True),
    ("documents changed on the green commit, main not merged in", True),
    ("the merge changed a source file as well", False),
    ("a source file changed after the merge", False),
    ("a source file changed on the green commit, no run since", False),
    ("the run on the commit before it failed", False),
    ("no commit of the branch ever had a run", False),
    ("the run was two commits back and the one between was cancelled", True),
]


def leaning_heads() -> list[tuple[str, bool]]:
    """`code_was_green` asked of heads made for it, in a repository of its
    own: `main` with one commit after the fork, and a branch whose commit
    `green` has a run that passed."""
    with tempfile.TemporaryDirectory(prefix="check_pr-") as where:
        def run(*arguments: str) -> subprocess.CompletedProcess[str]:
            identity = {"GIT_AUTHOR_NAME": "check_pr", "GIT_AUTHOR_EMAIL": "check_pr@localhost",
                        "GIT_COMMITTER_NAME": "check_pr", "GIT_COMMITTER_EMAIL": "check_pr@localhost",
                        "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_SYSTEM": os.devnull}
            return subprocess.run(("git", *arguments), cwd=where, capture_output=True, text=True,
                                  env={**os.environ, **identity})

        def write(name: str, text: str) -> None:
            path = Path(where) / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)

        def commit(message: str, **files: str) -> str:
            for name, text in files.items():
                write(name.replace("__", "/").replace("_md", ".md").replace("_cpp", ".cpp"), text)
            run("add", "-A")
            run("commit", "-q", "--allow-empty", "-m", message)
            return run("rev-parse", "HEAD").stdout.strip()

        def branch(name: str, at: str) -> None:
            run("checkout", "-q", "-B", name, at)

        def merge_main(message: str = "Merge main [skip ci]") -> bool:
            return run("merge", "-q", "--no-ff", "-m", message, "main").returncode == 0

        run("init", "-q", "-b", "main")
        fork = commit("the fork", src__a_cpp="a\n", ROADMAP_md="| 23 | open: bodies, proxies |\n", README_md="readme\n")
        branch("work", fork)
        green = commit("the change", src__b_cpp="b\n")
        branch("main", fork)
        main = commit("another pull request lands", src__c_cpp="c\n", ROADMAP_md="| 23 | open: proxies |\n")
        says = {green: "green"}

        def asked(head: str, said: dict[str, str] | None = None) -> bool:
            found = code_was_green(head, main, lambda sha: (says if said is None else said).get(sha, ""), run)
            return found == green

        heads: list[bool] = []
        branch("merged", green)
        merge_main()
        merged = run("rev-parse", "HEAD").stdout.strip()
        heads.append(asked(merged))
        heads.append(asked(commit("Run CI over merged main")))
        heads.append(asked(commit("The write-up", docs__phases__5_md="done\n")))
        # both sides rewrote the row: git alone leaves the conflict's marks, the agent wrote the row
        branch("row", green)
        row = commit("The row [skip ci]", ROADMAP_md="| 23 | open: bodies |\n")
        conflict = not merge_main()
        heads.append(conflict and asked(commit("Merge main, the row put right [skip ci]", ROADMAP_md="| 23 | open: nothing |\n")))
        heads.append(asked(row))
        branch("evil", green)
        run("merge", "-q", "--no-ff", "--no-commit", "main")
        heads.append(asked(commit("Merge main [skip ci]", src__b_cpp="b, and more\n")))
        branch("later", merged)
        heads.append(asked(commit("One more thing [skip ci]", src__b_cpp="b, and more\n")))
        branch("more", green)
        heads.append(asked(commit("More [skip ci]", src__b_cpp="b, and more\n")))
        heads.append(asked(merged, {green: "failed"}))
        heads.append(asked(merged, {}))
        # a push that cancelled the run on the commit under it, then main merged in
        branch("cancelled", green)
        between = commit("A line of the write-up", docs__phases__5_md="begun\n")
        merge_main()
        heads.append(asked(run("rev-parse", "HEAD").stdout.strip(), {green: "green", between: ""}))
        return list(zip([name for name, _wanted in LEANING_HEADS], heads))


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
             (39, ""), (-1, "this clone does not have the commit abc1234")]
    wanted_stale = [
        [(False, "main is in the head commit abc1234")],
        [(True, "it does not merge with main any more (tests/test_suite.cpp): it is 39 commit(s) "
                "behind main. Merge main in, or close it if what it was for has landed")],
        [(False, "39 commit(s) behind main, and it still merges")],
        [(True, "this clone does not have the commit abc1234")],
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
        ("a directory every pull request adds its own file to, named bare, claims nobody's file in it",
         same_files({"src/js/JsOps.cpp", "tests/suites/", "docs/phases/", "docs/findings"},
                    {"tests/suites/393-json-raw-json.cpp", "docs/phases/393-a-number.md", "docs/findings/393-the-text.md"}), []),
        ("a file added there under another number than its own is a refusal",
         [text for blocking, text in claim_problems(
             dict(claim("Task: a row\nFiles: `docs/phases/387-a-number.md`, `docs/findings/393-the-text.md`, `tests/suites/1-old.cpp`\n"
                        "Phase and finding: this pull request's number\nAgent: Merlin\n"), number=393, phase=393, finding=393),
             [], Main({1}, {1}, {1}, {1}, 1, 1)) if blocking],
         ["`docs/phases/387-a-number.md` is called by 387, and this pull request's number is 393: rename it (`docs/phases/393-...`)"]),
        ("a claim with no push and no word from its agent for two hours is called quiet",
         (quiet_note("WIP: a row", 0, 21.3, "Rowan", False) or "")[:112],
         "QUIET: no push for 21.3 h (nothing in it but its claim), and no comment in Rowan's name anywhere in the last 2 h"),
        ("...and it says who may close it: the agent the person has given its task to, a retired agent's successor (#367)",
         [(quiet_note("WIP: a row", changed, 21.3, "Rowan", False) or "").split("(Only when ")[-1] for changed in (0, 3)],
         ["the person has told you that its agent is retired and its task is yours: say so on it, and close it. "
          "AGENTS.md, \"Before starting\", point 1)",
          "the person has told you that its agent is retired and its task is yours: say so on it, take its commits, and close it. "
          "AGENTS.md, \"Before starting\", point 1)"]),
        ("...not one whose agent wrote somewhere in that time: #389, which was closed for its silence and was not silent",
         quiet_note("WIP: a row", 0, 2.6, "Vireo", True), None),
        ("...not one pushed to an hour ago, and not one that is finished and waits to land",
         (quiet_note("WIP: a row", 0, 1.9, "Vireo", False), quiet_note("Phase 5: a row", 4, 30.0, "Vireo", False)),
         (None, None)),
        ("...one with files in it is work in progress, and one that names no agent says so",
         (quiet_note("WIP: a row", 3, 5.0, None, False) or "")[:103],
         "QUIET: no push for 5.0 h (work in progress), and it names no agent whose comments could be looked for i"),
        ("whose a comment is, is its first word, and the newest of each name is kept",
         last_words([
             {"body": "Vireo: The #389 claim check reports overlap", "created_at": "2026-11-25T05:02:57Z",
              "issue_url": "https://api.github.com/repos/o/r/issues/416"},
             {"body": "**Merlin — claim: PR #414", "created_at": "2026-11-25T05:00:02Z",
              "issue_url": "https://api.github.com/repos/o/r/issues/219"},
             {"body": "Vireo: claiming the row", "created_at": "2026-11-25T02:40:00Z",
              "issue_url": "https://api.github.com/repos/o/r/issues/389"},
             {"body": "", "created_at": "2026-11-25T05:10:00Z", "issue_url": "https://api.github.com/repos/o/r/issues/1"},
         ]),
         {"Vireo": ("2026-11-25T05:02:57Z", 416), "Merlin": ("2026-11-25T05:00:02Z", 219)}),
        ("a file main has, which an earlier pull request changes too, is a note and no refusal",
         [(blocking, text[:60]) for blocking, text in claim_problems(
             dict(claim("Task: a row\nFiles: `src/js/JsInterpreter.cpp`, `tools/new.py`\nPhase and finding: this pull request's number\nAgent: Vega\n"), number=390, phase=390, finding=390),
             [dict(claim("Task: another\nFiles: `src/js/JsInterpreter.cpp`, `tools/new.py`\nPhase and finding: this pull request's number\nAgent: Cedar\n"), number=387, phase=387, finding=387)],
             Main({1}, {1}, {1}, {1}, 1, 1), on_main_file=lambda name: name.startswith("src/"))],
         [(True, "#387 had tools/new.py first, and `main` has no such file: tw"),
          (False, "#387, opened first, changes src/js/JsInterpreter.cpp too. Go")]),
        ("...nor do two pull requests that both name it bare share it",
         same_files({"docs/phases/", "docs/findings/"}, {"docs/phases/", "docs/findings/"}), []),
        ("...a file in it that two name is still shared",
         same_files({"tests/suites/393-json-raw-json.cpp"}, {"tests/suites/393-json-raw-json.cpp", "tests/suites/"}),
         ["tests/suites/393-json-raw-json.cpp"]),
        ("a file named before its number is known is nobody's: two that both say N share nothing",
         same_files({"tests/suites/N-*.cpp", "docs/phases/N-*.md", "docs/findings/N-*.md", "src/js/VM.cpp"},
                    {"docs/findings/N-*.md", "docs/phases/N-*.md", "tests/suites/N-*.cpp", "src/js/JsOps.cpp"}), []),
        ("...however the number that is not there yet is written",
         [name for name in ("docs/phases/N-….md", "docs/phases/NNN-a-name.md", "tests/suites/<N>-a-name.cpp",
                            "tests/suites/*.cpp", "docs/findings/{N}-x.md", "docs/findings/#-x.md", "tests/suites/...",
                            "docs/phases/PR-x.md", "tests/suites/[N]-x.cpp")
          if not claims_nothing(name)], []),
        ("...a file there with a number, or with a name of its own, is a file",
         [name for name in ("docs/phases/419-*.md", "tests/suites/421-the-vm-binds.cpp", "tests/suites/README.md",
                            "tests/suites/Nesting.hpp", "docs/findings/names.md", "src/N-*.cpp", "tools/N-x.py")
          if claims_nothing(name)], []),
        ("...and the later of two that wrote N is refused nothing and told nothing for it",
         claim_problems(
             dict(claim("Task: a row\nFiles: src/js/VM.cpp, tests/suites/N-*.cpp, docs/phases/N-*.md, docs/findings/N-*.md\n"
                        "Phase and finding: this pull request's number\nAgent: Arena\n"), number=421, phase=421, finding=421),
             [dict(claim("Task: another\nFiles: `src/js/JsOps.cpp`, `tests/suites/N-*.cpp`, `docs/phases/N-*.md`, `docs/findings/N-*.md`\n"
                         "Phase and finding: this pull request's number\nAgent: Marten\n"), number=415, phase=415, finding=415)],
             Main({1}, {1}, {1}, {1}, 1, 1), on_main_file=lambda name: name.startswith("src/")), []),
        ("...and the pull request that names it bare is told so, in a note",
         [text[:74] for blocking, text in claim_problems(
             claim("Task: a row\nFiles: `src/a.cpp`, `docs/phases/`\nPhase and finding: this pull request's number\nAgent: Vega\n"),
             [], Main({1}, {1}, {1}, {1}, 1, 1)) if not blocking and "Files:" in text],
         ["its `Files:` line names `docs/phases/`: every pull request adds a file of "]),
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
    # What the landing session asks before it takes a pull request: who has a
    # number to change. #1 said Phase 9 first, #2 says it too and finding 11
    # alone, #3 says a phase that has a write-up on main.
    first = dict(claim("Task: a row\nFiles: `src/c.cpp`\nPhase 9, finding 10\nAgent: Heron\n"), number=1)
    second = dict(claim("Task: a row\nFiles: `src/d.cpp`\nPhase 9, finding 11\nAgent: Marten\n"), number=2)
    third = dict(claim("Task: a row\nFiles: `src/e.cpp`\nPhase 6, finding 12\nAgent: Kestrel\n"), number=3)
    own = claim_of({"number": 360, "title": "WIP: a change", "files": [],
                    "body": "Task: a row\nFiles: `src/f.cpp`,\n`src/g.cpp`\nPhase and finding: this pull request's number\nAgent: Heron\n\ntext"})
    own_too = claim_of({"number": 361, "title": "WIP: another", "files": [],
                        "body": "Task: a row\nFiles: `src/h.cpp`\nPhase and finding: this pull request's number\nAgent: Marten\n"})
    cases += [
        ("a claim whose numbers are the pull request's own number",
         (own["phase"], own["finding"], own["has_claim_lines"], sorted(own["files"]), own["agent"]),
         (360, 360, True, ["src/f.cpp", "src/g.cpp"], "Heron")),
        ("it is called by them", numbers_of(own), "Phase 360, finding 360"),
        ("two such claims have nothing to change, whenever they were opened", number_clashes([own, own_too], main), []),
        ("and nothing is wrong with one", [text for blocking, text in claim_problems(own, [own, own_too], main) if blocking], []),
        ("the line is read whatever its capitals, and with `the` for `this`",
         [bool(OWN_NUMBER_RE.match(line)) for line in ("Phase and finding: this pull request's number",
                                                       "phase and finding: the pull request's number (360)",
                                                       "Phase and finding: this pull request's own number",
                                                       "Phase 5, finding 7", "Phase and finding: 360")],
         [True, True, True, False, False]),
    ]
    cases += [
        ("the later of two that say one phase has a number to change, and so has one whose phase is on main; the first has not",
         number_clashes([first, second, third], main),
         [(2, "Phase 9: #1 had it first. This one moves"), (3, "Phase 6 is on main already")]),
        ("files that two pull requests share are not a number to change",
         number_clashes([dict(first, number=4), dict(claim("Task: a row\nFiles: `src/c.cpp`\nPhase 13, finding 14\n"), number=5)], main), []),
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
        ("a pull request with no change in it is a claim, whatever its title says: nothing to land",
         why_not_taken(waiting(title="Claim: the root layout box", files=()), board)[:62],
         "it changes no file: there is nothing in it to land. If it is a"),
        ("...and saying that it is taken does not make it something to land",
         why_not_taken(waiting(title="Claim: the root layout box", files=(), checks=[]),
                       board + [said("Lander: lot of 03:01 UTC: #7 (`fffffff`) with `main` `1234567`, one CI run.")])[:19],
         "it changes no file:"),
        ("a draft is not finished whatever its title says, and is told so in its own words",
         why_not_taken(dict(waiting(), isDraft=True), board), "it is a draft: `gh pr ready 7` when it is finished"),
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
        ("...not under a rule on main, which merges no head without a run (BLOCKED): its agent is told what starts one (#548)",
         [why_not_taken(dict(waiting(checks=[], files=files), mergeStateStatus="BLOCKED"), board, lambda pr: "b" * 40)[:70]
          for files in (("AGENTS.md", "docs/phases/5-a.md"), ("src/a.cpp",))],
         ["no CI run on its head commit, and since the rule on main GitHub merges"] * 2),
        ("...and BLOCKED says nothing of a head that has a run: taken while it goes, as before",
         why_not_taken(dict(waiting(checks=[check("Build", None, "IN_PROGRESS")]), mergeStateStatus="BLOCKED"), board), ""),
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
        ("work in progress that is no draft is told how to become one (#449, #491); a draft and a finished one are told nothing",
         [(draft_note(7, title, is_draft) or "")[:76] for title, is_draft in (("WIP: a change", False), ("WIP: a change", True),
                                                                               ("A change", False), ("A change", True))],
         ["#7 is work in progress and no draft. Make it one now, `gh pr ready 7 --undo`", "", "", ""]),
        ("claim N of a claim that is right and a draft: ok, and nothing else",
         claim_end(7, "Phase 7, finding 7", False, None, lambda: 1 // 0),
         [("ok", "#7: Phase 7, finding 7; no earlier open pull request has its numbers or its files")]),
        ("...of one that is right and no draft: ok for the numbers, refused for the draft, and no other numbers offered "
         "(#515, #521, #532, #539: four claims of one agent, each told so in a note under \"ok\")",
         [(kind, text[:36]) for kind, text in claim_end(7, "Phase 7, finding 7", False, "#7 is work in progress and no draft.", lambda: 1 // 0)],
         [("ok", "#7: Phase 7, finding 7; no earlier o"), ("no", "#7 is work in progress and no draft.")]),
        ("...of one whose numbers are somebody else's: the next numbers, and the draft is still said",
         (claim_end(7, "Phase 6, finding 6", True, None, lambda: "the next pair"),
          claim_end(7, "Phase 6, finding 6", True, "no draft", lambda: "the next pair")),
         ([("note", "the next pair")], [("note", "the next pair"), ("no", "no draft")])),
        ("no run on the head of an engine change that is a green commit with main merged in by git alone: taken (#492)",
         why_not_taken(waiting(checks=[]), board, lambda pr: "b" * 40), ""),
        ("...not when it is no such head",
         why_not_taken(waiting(checks=[]), board, lambda pr: None), "no CI run on its head commit"),
        ("...and git is not asked about a head that has a run, nor about one with WIP: in its title",
         [why_not_taken(pr, board, lambda pr: 1 // 0) for pr in (waiting(), waiting(title="WIP: a change", checks=[]))],
         ["", "its title still begins with WIP:"]),
        ("such a head while a watch is on: `merge N` says it is taken as it is, with no run of its own",
         verdict(**no_run, since=["ROADMAP.md", "src/js/JsEval.cpp"], merged_alone=True, watch_on=True), ([True, True], False)),
        ("...the same when the run on the commit before it was the short one",
         verdict(**no_run, ran_state="short", since=["ROADMAP.md", "src/js/JsEval.cpp"], merged_alone=True, watch_on=True),
         ([True, True], False)),
        ("...a watch does not take a head whose code changed since the run",
         verdict(**no_run, since=["ROADMAP.md", "src/js/JsEval.cpp"], watch_on=True), ([True, False], False)),
        ("...with no watch on somebody merges on it, so it needs its run, and the agent is told how to get one",
         [(ok, text[-len(UNMARKED_END):]) for state in ("passed", "short") for ok, text in ci_verdict(Evidence(
             head="a" * 40, behind=0, main_files=[], pr_files=["src/a.cpp", "README.md"], head_state="none", head_bad=[],
             ran_on="b" * 40, ran_state=state, since=["ROADMAP.md", "src/js/JsEval.cpp"], merged_alone=True))[0]],
         [(True, "main is in the head commit aaaaaaa"[-len(UNMARKED_END):]), (False, UNMARKED_END)] * 2),
        ("what the check runs of a commit say, as the landing session's tool reads them",
         [run_says([{"status": status, "conclusion": conclusion} for status, conclusion in runs])
          for runs in ([("completed", "success")] * 18,
                       [("completed", "success")] * 7 + [("completed", "skipped")] * 11,
                       [("completed", "success")] * 25 + [("completed", "skipped")] * 11,
                       [("completed", "success")] * 2 + [("completed", "skipped")] * 16,
                       [("completed", "success")] + [("completed", "skipped")] * 17,
                       [("completed", "success")] * 9,
                       [("completed", "success")] * 17 + [("completed", "failure")],
                       [("completed", "success")] * 6 + [("completed", "timed_out")] + [("completed", "skipped")] * 11,
                       [("completed", "success")] * 7 + [("completed", "skipped")] * 11 + [("completed", "cancelled")] * 18,
                       [("completed", "success")] * 6 + [("in_progress", None)] + [("completed", "skipped")] * 11,
                       [])],
         ["green", "green", "green", "green", "", "", "failed", "failed", "", "", ""]),
        ("a head with no run whose code is the code of a commit that was green (a repository made for the question)",
         leaning_heads(), LEANING_HEADS),
        ("while a watch is on, a main that moved by engine code is the landing session's: the agent is not told to merge it",
         verdict(**moved, main_files=["src/js/JsEval.cpp"], watch_on=True), ([True, True], False)),
        ("...and with no watch on it is still the agent's, as it was",
         verdict(**moved, main_files=["src/js/JsEval.cpp"]), ([False, True], False)),
        ("...a watch does not make a failed run good",
         verdict(**moved, main_files=["src/js/JsEval.cpp"], head_state="failed", head_bad=["Build: failure"],
                 watch_on=True), ([True, False], False)),
    ]

    run_of_main = {"databaseId": 45705344585, "headSha": "9fd0d22" + "0" * 33, "status": "completed", "conclusion": "failure"}
    jobs_of_main = [{"name": "Documentation shape", "conclusion": "failure"},
                    {"name": "Build & unit tests", "conclusion": "failure"},
                    {"name": "Render reftests (pixels)", "conclusion": "success"},
                    {"name": "Sanitizers: fuzzing", "conclusion": None},
                    {"name": "Deep and oversized pages: the dumps", "conclusion": "timed_out"},
                    {"name": "JavaScript conformance (Node-verified corpus)", "conclusion": "failure"}]
    broken_main = ("`main` is broken at 9fd0d22: 4 job(s) of its own CI run failed (Build & unit tests, Deep and oversized "
                   "pages: the dumps, Documentation shape and 1 more). `python3 tools/ci/run_report.py --run 45705344585` says why.")
    breaking_main = "`main` is broken at 9fd0d22: 1 job(s) of its own CI run failed so far (Documentation shape). `python3"
    cases += [
        ("main's newest run with every job passed or going: nothing is said",
         main_note(run_of_main, [job for job in jobs_of_main if job["conclusion"] in ("success", None)]), None),
        ("no run on main at all: nothing is said", main_note(None, []), None),
        ("a job of main's own run failed: the commit, how many, three names, and the run to read",
         (main_note(run_of_main, jobs_of_main) or "")[:len(broken_main)], broken_main),
        ("...and while that run is still going it says so: one failed job is enough",
         (main_note({**run_of_main, "status": "in_progress", "conclusion": None}, jobs_of_main[:1]) or "")[:len(breaking_main)],
         breaking_main),
        ("a job that was cancelled or skipped is not a failed one",
         main_note(run_of_main, [{"name": "a", "conclusion": "cancelled"}, {"name": "b", "conclusion": "skipped"}]), None),
        ("the newest run on main, as the API lists it, is read as main_note takes it",
         newest_run_on_main({"total_count": 2, "workflow_runs": [
             {"id": 45705344585, "head_sha": "9fd0d22" + "0" * 33, "status": "completed", "conclusion": "failure", "event": "push"},
             {"id": 1, "head_sha": "a" * 40, "status": "completed", "conclusion": "success"}]}), run_of_main),
        ("...and the note it gives is the same one",
         (main_note(newest_run_on_main({"workflow_runs": [{"id": 45705344585, "head_sha": "9fd0d22" + "0" * 33,
                                                             "status": "completed", "conclusion": "failure"}]}),
                    jobs_of_main) or "")[:len(broken_main)], broken_main),
        ("the API lists no run, or answers something else: no run", 
         [newest_run_on_main(answer) for answer in ({}, {"workflow_runs": []}, None, {"message": "Not Found"})], [None] * 4),
        ("a lookup that failed says that it failed, in one line, and how to look",
         unread_main_note("unknown flag: --event\n\nUsage:  gh run list [flags]\n")[:101],
         "whether `main` is broken could not be read here (unknown flag: --event Usage: gh run list [flags]). L"),
    ]

    blocked = "GitHub will not merge #7 as it is (it reports BLOCKED): the rule the person set on `main` wants a passed run of "
    two = ["Build & unit tests", "Documentation shape"]
    both = "`Build & unit tests`, `Documentation shape` on the head commit abc1234"
    no_run = ", and that commit has no run: it was pushed with the mark that skips CI. Push a commit without the mark"
    behind = "GitHub will not merge #7 as it is (it reports BEHIND): a rule on `main` wants `main` in the branch. Merge it"
    unnamed = blocked + "the checks it names on the head commit abc1234"
    classic = {"name": "main", "protected": True,
               "protection": {"enabled": True, "required_status_checks": {"enforcement_level": "everyone", "contexts": list(reversed(two)), "checks": []}}}
    ruleset = [{"type": "deletion"},
               {"type": "required_status_checks", "ruleset_id": 1,
                "parameters": {"strict_required_status_checks_policy": False,
                               "required_status_checks": [{"context": "Build & unit tests", "integration_id": 15368}]}}]
    cases += [
        ("no rule on main (the branch as GitHub gives it today, no ruleset): no names",
         required_from({"name": "main", "protected": False,
                        "protection": {"enabled": False, "required_status_checks": {"enforcement_level": "off", "contexts": [], "checks": []}}}, []), []),
        ("a rule set under Settings, Branches: its names, in order", required_from(classic, []), two),
        ("a ruleset: its names; a rule of another kind has none", required_from({}, ruleset), ["Build & unit tests"]),
        ("both at once: each name once", required_from(classic, ruleset), two),
        ("nothing could be read: no names", required_from(None, None), []),
        ("GitHub has nothing against the merge: nothing is said", rule_note("CLEAN", 7, "abc1234" * 5, "none", two), None),
        ("a check that is not required fails or is going (UNSTABLE): not the rule's business",
         rule_note("UNSTABLE", 7, "abc1234" * 5, "running", two), None),
        ("GitHub does not know yet, or was not asked: nothing is said",
         (rule_note("UNKNOWN", 7, "abc1234" * 5, "none", two), rule_note(None, 7, "abc1234" * 5, "none", [])), (None, None)),
        ("BLOCKED and the head has no run: the rule's names, the commit, and a push without the mark",
         (rule_note("BLOCKED", 7, "abc1234" * 5, "none", two) or "")[:len(blocked + both + no_run)],
         blocked + both + no_run),
        ("...and never --admin nor --auto, whatever the head's run says",
         [(rule_note("BLOCKED", 7, "abc1234" * 5, state, two) or "").endswith(
             "Never `gh pr merge --admin`, and not `--auto` either: `gh` offers both when it refuses")
          for state in ("none", "running", "failed", "passed", "short")], [True] * 5),
        ("a draft, or a title under WIP:, reports BLOCKED under the rule whatever ran on it: nothing is said of it "
         "(#487, 28 November: told to push an empty commit in the middle of its work)",
         [rule_note(state, 7, "abc1234" * 5, head_state, two, unfinished=True)
          for state in ("BLOCKED", "BEHIND") for head_state in ("none", "running", "passed")], [None] * 6),
        ("a watch is on and the head's own run is going: BLOCKED is no refusal, the landing session takes it meanwhile",
         [taken_while_it_runs(state, head_state, watch_on) for state, head_state, watch_on in (
             ("BLOCKED", "running", True), ("BLOCKED", "running", False), ("BLOCKED", "none", True), ("BLOCKED", "failed", True),
             ("BLOCKED", "passed", True), ("BEHIND", "running", True), ("CLEAN", "running", True), (None, "running", True))],
         [True] + [False] * 7),
        ("the mark that skips CI, as GitHub reads it: anywhere in the message, in five spellings, or as a trailer",
         [carries_skip_mark(message) for message in (
             "WATCH: the lot of 11:33 UTC, 28 November [skip ci]\n\nThe landing session's branch.",
             "Merge main\n\n[skip ci] a conflict in AGENTS.md", "notes [ci skip]", "[no ci] notes", "x [skip actions]",
             "x [actions skip]", "Notes\n\n\nskip-checks: true", "Start CI on the head", "skip the ci", "[Skip CI] in capitals",
             "Landed. [objects only]")],
         [True] * 7 + [False] * 4),
        ("BLOCKED while the head's run is going: ask again",
         "its run is still going: ask again when it has ended" in (rule_note("BLOCKED", 7, "abc1234" * 5, "running", two) or ""), True),
        ("BLOCKED though the head's run passed: the rule wants something else, and the person is told",
         "`gh pr checks 7 --required` shows" in (rule_note("BLOCKED", 7, "abc1234" * 5, "passed", two) or ""), True),
        ("BLOCKED and the names could not be read: it still says what is wanted",
         (rule_note("BLOCKED", 7, "abc1234" * 5, "none", []) or "")[:len(unnamed)], unnamed),
        ("BEHIND: main is wanted in the branch, and the person is told what that does to a lot",
         ((rule_note("BEHIND", 7, "abc1234" * 5, "passed", two) or "")[:len(behind)],
          "cannot land a lot of several pull requests" in (rule_note("BEHIND", 7, "abc1234" * 5, "passed", two) or ""),
          (rule_note("BEHIND", 7, "abc1234" * 5, "passed", two) or "").endswith("`gh` offers both when it refuses")),
         (behind, True, True)),
    ]

    names_stale = ["a head with main in it: quiet",
                   "39 commits behind and it does not merge: blocking, and it names the file",
                   "39 commits behind and it still merges: a note, not a refusal",
                   "a head this clone does not have: no answer, and it says so (it used to say main is in it)"]
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
    if arguments == ["numbers"]:
        return show_number_clashes()
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
