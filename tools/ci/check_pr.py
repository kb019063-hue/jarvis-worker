#!/usr/bin/env python3
"""What the open pull requests claim, and whether one is ready to be merged.

    python3 tools/ci/check_pr.py             # the claims, the numbers taken, the next free pair
    python3 tools/ci/check_pr.py claim N     # after opening #N: are its numbers and its files free
    python3 tools/ci/check_pr.py merge N     # before `gh pr merge N --merge`

AGENTS.md says what a claim is and what has to be true before a merge. Each
of those was missed at least once by an agent that had read it, because each
is a thing to look up at a moment when the work feels finished. This looks
them up. It reads; it changes nothing.

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
answers "is `main` in this commit" wrongly. Run it inside the repository, so
that `gh` knows which one is meant.

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

ROOT = Path(__file__).resolve().parent.parent.parent
NEXT_IDS = ROOT / "tools" / "next_ids.py"      # the one allocator

# AGENTS.md, "The files every pull request changes", and the two it leaves out.
SHARED = {
    "README.md", "ARCHITECTURE.md", "AUDIT.md", "AGENTS.md", "ROADMAP.md",
    "tests/test_suite.cpp", "Makefile", ".github/workflows/ci.yml",
}
DOCUMENT = re.compile(r"\.md$")
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
        elif collecting and (not line.strip() or line.startswith("Task:") or re.match(r"Phase \d+", line)):
            collecting = False
        if collecting:
            for part in re.sub(r"\([^)]*\)", "", line).split(","):
                part = part.strip().strip("`").strip()
                if part:
                    named.add(part)
    changed = {entry["path"] for entry in pr.get("files") or []}
    return {
        "number": pr["number"],
        "title": pr["title"],
        "task": task[len("Task:"):].strip() if task else None,
        "files": named | changed,       # what it says it will change, and what it has changed
        "has_claim_lines": task is not None and has_files_line and numbers is not None,
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


def on_main() -> tuple[set[int], set[int]]:
    """The phase and finding numbers `main` has taken.

    Asked of `tools/next_ids.py`, which is the one allocator in the
    repository. This used to parse ARCHITECTURE.md and AUDIT.md here with
    `^## Phase (\\d+)` and `^## (\\d+)\\.`, and quietly went blind when the
    phase and finding write-ups moved out of those two documents into
    `docs/phases/` and `docs/findings/`: every heading it looked for was
    gone, so it reported nothing taken and would have handed out numbers
    that were already somebody else's. One allocator, asked from both
    places, cannot disagree with itself like that.

    `main` and not the working tree, because the question is what `main`
    has and a sandbox's clone may be behind it. `--no-prs`, because the
    open claims are folded in separately below: without it, a pull request
    is told that the number it has just claimed is already taken — by
    itself.
    """
    result = subprocess.run(
        [sys.executable, str(NEXT_IDS), "--taken", "--ref", "main", "--no-prs"],
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
    return set(taken["phases"]), set(taken["findings"])


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


def taken_numbers(claims: list[dict], main_phases: set[int], main_findings: set[int]) -> tuple[set[int], set[int]]:
    phases = set(main_phases) | {c["phase"] for c in claims if c["phase"] is not None}
    findings = set(main_findings) | {c["finding"] for c in claims if c["finding"] is not None}
    return phases, findings


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


def claim_problems(claim: dict, claims: list[dict], main_phases: set[int], main_findings: set[int]) -> list[tuple[bool, str]]:
    """What is wrong with a claim: (does this pull request have to change, what)."""
    problems: list[tuple[bool, str]] = []
    if not claim["has_claim_lines"]:
        problems.append((True, "its text does not begin with the Task:, Files: and `Phase P, finding F` lines"))
    others = [c for c in claims if c["number"] != claim["number"]]
    for kind, key, on in (("Phase", "phase", main_phases), ("finding", "finding", main_findings)):
        number = claim[key]
        if number is None:
            continue
        if number in on:
            problems.append((True, f"{kind} {number} is on main already"))
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
    main_phases, main_findings = on_main()
    print(f"main: phases up to {max(main_phases)}, findings up to {max(main_findings)}")
    for claim in claims:
        pushed = hours_since(commit(claim["head"])["commit"]["committer"]["date"])
        numbers = ", ".join(f"{kind} {claim[key]}" for kind, key in (("Phase", "phase"), ("finding", "finding"))
                            if claim[key] is not None) or "no numbers"
        print(f"\n#{claim['number']}  {numbers}  (last commit {pushed:.1f} h ago)")
        print(f"    {claim['title'][:110]}")
        own = sorted(claim["files"] - SHARED)
        print("    files: " + (", ".join(own) if own else "(none)"))
        for blocking, problem in claim_problems(claim, claims, main_phases, main_findings):
            print(f"    {'!!' if blocking else '  '} {problem}")
    phases, findings = taken_numbers(claims, main_phases, main_findings)
    print(f"\nnext free: Phase {next_free(phases)}, finding {next_free(findings)}")
    return 0


def check_claim(number: int) -> int:
    claims = open_claims()
    mine = next((c for c in claims if c["number"] == number), None)
    if mine is None:
        sys.exit(f"#{number} is not an open pull request")
    main_phases, main_findings = on_main()
    report = Report()
    for blocking, problem in claim_problems(mine, claims, main_phases, main_findings):
        (report.no if blocking else report.note)(problem)
    if not report.failed:
        report.ok(f"#{number}: Phase {mine['phase']}, finding {mine['finding']}; no earlier open pull request has its numbers or its files")
    else:
        phases, findings = taken_numbers(claims, main_phases, main_findings)
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
    main_phases, main_findings = on_main()
    if not claim["has_claim_lines"]:
        report.no("the text does not begin with the Task:, Files: and `Phase P, finding F` lines")
    for kind, mine, on in (("Phase", claim["phase"], main_phases), ("finding", claim["finding"], main_findings)):
        if mine is None:
            continue
        if mine in on:
            report.no(f"{kind} {mine} is on main already")
        else:
            report.ok(f"{kind} {mine} is not on main")
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


def main() -> int:
    arguments = sys.argv[1:]
    if not arguments:
        return show_claims()
    if len(arguments) == 2 and arguments[0] in ("claim", "merge") and arguments[1].lstrip("#").isdigit():
        number = int(arguments[1].lstrip("#"))
        return check_claim(number) if arguments[0] == "claim" else check_merge(number)
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
