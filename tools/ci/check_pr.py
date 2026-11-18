#!/usr/bin/env python3
"""What the open pull requests claim, and whether one is ready to be merged.

    python3 tools/ci/check_pr.py             # the claims, the numbers taken, the next free pair
    python3 tools/ci/check_pr.py claim N     # after opening #N: are its numbers and its files free
    python3 tools/ci/check_pr.py merge N     # before `gh pr merge N --merge`
    python3 tools/ci/check_pr.py --self-test # how a claim is read, on fixed texts; no network

AGENTS.md says what a claim is and what has to be true before a merge. Each
of those was missed at least once by an agent that had read it, because each
is a thing to look up at a moment when the work feels finished. This looks
them up. It reads; it changes nothing on GitHub, and in the clone it only
fetches `main` when the clone is behind it.

`claim N` fails (exit 1) when
  - #N does not begin with the `Task:`, `Files:` and `Phase P, finding F`
    lines;
  - its phase or its finding is on `main` already, or an open pull request
    with a lower number has it (of two that took a number at the same
    moment, the later one moves);
  - a file it names or has changed belongs to an open pull request with a
    lower number. The files every pull request changes (AGENTS.md lists
    them) do not count.

`merge N` fails when
  - the title still begins with `WIP:`;
  - GitHub does not report the pull request as mergeable;
  - `main` is not contained in its head commit: `main` has moved since it
    was merged in, and whatever CI said, it said about another tree;
  - CI has not passed on the head commit. A head commit that was pushed with
    `[skip ci]` is accepted when CI passed on an ancestor that contains
    `main` too and nothing but documents (`*.md`) changed since;
  - its phase or finding is on `main` already;
  - the number of unit suites README.md states is not the number in
    tests/test_suite.cpp.

Everything is asked of GitHub through `gh` (which has to be logged in), and
nothing of the local clone: a sandbox's clone is shallow, and a shallow clone
answers "is `main` in this commit" wrongly. The one exception is the numbers
`main` has: `tools/next_ids.py` reads them out of git, from the commit that
GitHub says `main` is at. Run it inside the repository, so that `gh` knows
which one is meant.

The one exception is the set of numbers taken, which comes from
`tools/next_ids.py --taken --ref main` — that tool reads `main` out of git
rather than off the disk, and it is the allocator the rest of the repository
uses, so the two cannot drift apart.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
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
DOCUMENT = re.compile(r"\.md$")
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
            for part in re.sub(r"\([^)]*\)", "", line).split(","):
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
    and 72 and no Phase 68 an hour after #83 had put 68 and 74 on `main`.
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
    phases: set[int]            # taken: in a file, or at or below a floor
    findings: set[int]
    written_phases: set[int]    # in a file
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
    `docs/state.json` is taken, so that the allocator never hands it out
    again, whether or not a write-up of it exists. This file used to call all
    of them "on main already": #63 (Phase 62, finding 68) and #73 (Phase 58,
    finding 64) were open with numbers no file had when the floors rose to 66
    and 72, and were told "not ready" for a reason that was not true.
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


def next_free(taken: set[int]) -> int:
    return max(taken, default=0) + 1


def taken_numbers(claims: list[dict], main: Main) -> tuple[set[int], set[int]]:
    phases = set(main.phases) | {c["phase"] for c in claims if c["phase"] is not None}
    findings = set(main.findings) | {c["finding"] for c in claims if c["finding"] is not None}
    return phases, findings


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
        return False, (f"{kind} {number} is in no file on main. It is at or below the floor of docs/state.json "
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
    phases, findings = taken_numbers(claims, main)
    print(f"\nnext free: Phase {next_free(phases)}, finding {next_free(findings)}")
    return 0


def check_claim(number: int) -> int:
    claims = open_claims()
    mine = next((c for c in claims if c["number"] == number), None)
    if mine is None:
        sys.exit(f"#{number} is not an open pull request")
    main = on_main()
    report = Report()
    for blocking, problem in claim_problems(mine, claims, main):
        (report.no if blocking else report.note)(problem)
    if not report.failed:
        report.ok(f"#{number}: {numbers_of(mine)}; no earlier open pull request has its numbers or its files")
    else:
        phases, findings = taken_numbers(claims, main)
        report.note(f"next free: Phase {next_free(phases)}, finding {next_free(findings)}")
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


def behind_main(sha: str) -> int:
    """How many commits of `main` are not in `sha`."""
    return gh_json("api", f"{REPO}/compare/main...{sha}")["behind_by"]


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

    behind = behind_main(head)
    if behind == 0:
        report.ok(f"main is in the head commit {head[:7]}")
    else:
        report.no(f"main has moved: {behind} of its commits are not in {head[:7]}. "
                  "Merge it, build, run the checks, push, and let CI pass on that commit")

    state, bad = check_runs(head)
    if state == "passed":
        report.ok(f"CI passed on the head commit {head[:7]}")
    elif state in ("failed", "running"):
        report.no(f"CI on {head[:7]}: " + "; ".join(bad))
    else:
        # No run on the head: it was pushed with [skip ci], or the pull request
        # conflicted with main when it was pushed. Walk back along the branch
        # to the last commit that has one.
        found = None
        sha = head
        for _ in range(30):
            parents = commit(sha)["parents"]
            if not parents:
                break
            sha = parents[0]["sha"]
            ancestor_state, _bad = check_runs(sha)
            if ancestor_state != "none":
                found = (sha, ancestor_state)
                break
        if found is None:
            report.no(f"no CI run on {head[:7]} or on the 30 commits before it")
        else:
            sha, ancestor_state = found
            changed = [f["filename"] for f in gh_json("api", f"{REPO}/compare/{sha}...{head}").get("files", [])]
            code = [f for f in changed if not DOCUMENT.search(f)]
            if ancestor_state != "passed":
                report.no(f"no CI run on {head[:7]}; the last one, on {sha[:7]}, {ancestor_state}")
            elif behind_main(sha) != 0:
                report.no(f"no CI run on {head[:7]}; the last one, on {sha[:7]}, was on a tree without today's main")
            elif code:
                report.no(f"no CI run on {head[:7]}; since the last one, on {sha[:7]}, these changed: {', '.join(code[:8])}")
            else:
                report.ok(f"CI passed on {sha[:7]}, and since then only documents changed "
                          f"({', '.join(changed) or 'nothing'}). Say so in the pull request")

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
    readme = file_at("README.md", head)
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

    cases = [
        ("the three lines: the numbers", (numbered["phase"], numbered["finding"]), (5, 7)),
        ("the three lines: the files, over two lines, without what is in brackets",
         numbered["files"], {"src/a.cpp", "include/a.hpp", "tests/test_suite.cpp"}),
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
    # floors of 6 and 8: Phase 5 and finding 7 are taken and in no file.
    main = Main(set(range(1, 7)), set(range(1, 9)), {1, 2, 3, 4, 6}, {1, 2, 3, 4, 5, 6, 8}, 6, 8)
    below = claim_problems(numbered, [numbered], main)
    written = claim_problems(claim("Task: a row\nFiles: `src/c.cpp`\nPhase 6, finding 8\n"), [], main)
    free = claim_problems(claim("Task: a row\nFiles: `src/c.cpp`\nPhase 7, finding 9\n"), [], main)
    cases += [
        ("a number at or below a floor that no file has does not stop its pull request",
         [blocking for blocking, _text in below], [False, False]),
        ("and the remark says where it stands",
         ["in no file on main" in text and "floor" in text for _blocking, text in below], [True, True]),
        ("a number a file on main has stops it",
         written, [(True, "Phase 6 is on main already"), (True, "finding 8 is on main already")]),
        ("a number above the floors that no file has is free", free, []),
    ]
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
