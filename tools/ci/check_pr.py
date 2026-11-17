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
  - `origin/main` is not contained in its head commit: `main` has moved
    since it was merged in, and whatever CI said, it said about another tree;
  - CI has not passed on the head commit. A head commit that was pushed with
    `[skip ci]` is accepted when CI passed on an ancestor that contains
    `origin/main` too and nothing but documents (`*.md`) changed since;
  - its phase or finding is on `main` already under another title;
  - the number of unit suites README.md states is not the number in
    tests/test_suite.cpp.

It needs `git` and an authenticated `gh`, and the repository as the working
directory. The last check of `merge` reads files from the pull request's head
commit, so it works from any checkout that has fetched it.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from datetime import datetime, timezone

# AGENTS.md, "The files every pull request changes", and the two it forgot.
SHARED = {
    "README.md", "ARCHITECTURE.md", "AUDIT.md", "AGENTS.md", "ROADMAP.md",
    "tests/test_suite.cpp", "Makefile", ".github/workflows/ci.yml",
}
DOCUMENT = re.compile(r"\.md$")


def run(*command: str, check: bool = True) -> str:
    result = subprocess.run(command, capture_output=True, text=True)
    if check and result.returncode != 0:
        sys.exit(f"{' '.join(command)}\n{result.stderr.strip()}")
    return result.stdout


def gh_json(*arguments: str):
    return json.loads(run("gh", *arguments))


def numbers_in(text: str) -> tuple[set[int], set[int]]:
    """The phases and findings a pull request says it is."""
    phases = {int(n) for n in re.findall(r"\bPhase (\d+)\b", text)}
    findings = {int(n) for n in re.findall(r"\b[Ff]inding (\d+)\b", text)}
    return phases, findings


def claim_of(pr: dict) -> dict:
    """The claim a pull request makes: the three lines AGENTS.md asks for."""
    body = pr.get("body") or ""
    head = "\n".join(body.split("\n")[:12])
    task = re.search(r"(?m)^Task:\s*(.+)$", head)
    files_line = re.search(r"(?m)^Files:\s*(.+)$", head)
    numbers = re.search(r"(?m)^Phase (\d+), finding (\d+)\s*$", head)
    phase = int(numbers.group(1)) if numbers else None
    finding = int(numbers.group(2)) if numbers else None
    if numbers is None:
        # No claim line: what the title says is still a number somebody is using.
        in_title = re.search(r"\bPhase (\d+)\b", pr["title"])
        phase = int(in_title.group(1)) if in_title else None
        in_title = re.search(r"\b[Ff]inding (\d+)\b", pr["title"])
        finding = int(in_title.group(1)) if in_title else None
    named: set[str] = set()
    if files_line:
        for part in files_line.group(1).split(","):
            part = re.sub(r"\(.*?\)", "", part).strip().strip("`").strip()
            if part:
                named.add(part)
    changed = {entry["path"] for entry in pr.get("files") or []}
    return {
        "number": pr["number"],
        "title": pr["title"],
        "task": task.group(1).strip() if task else None,
        "files": named | changed,       # what it says it will change, and what it has changed
        "named": named,
        "has_claim_lines": task is not None and files_line is not None and numbers is not None,
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


def on_main() -> tuple[dict[int, str], dict[int, str]]:
    """Phase and finding numbers `origin/main` has, with the title of each."""
    architecture = run("git", "show", "origin/main:ARCHITECTURE.md")
    audit = run("git", "show", "origin/main:AUDIT.md")
    phases: dict[int, str] = {}
    for number, title in re.findall(r"(?m)^\| \*\*Phase (\d+)\*\* \| (.{0,60})", architecture):
        phases[int(number)] = title
    for number, title in re.findall(r"(?m)^## Phase (\d+)\b(.{0,60})", architecture):
        phases.setdefault(int(number), title.strip())
    findings = {int(n): t for n, t in re.findall(r"(?m)^## (\d+)\. (.{0,60})", audit)}
    return phases, findings


def open_claims() -> list[dict]:
    prs = gh_json("pr", "list", "--state", "open", "--limit", "100",
                  "--json", "number,title,body,headRefOid,files")
    return sorted((claim_of(pr) for pr in prs), key=lambda c: c["number"])


def hours_since(stamp: str) -> float:
    then = datetime.strptime(stamp, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
    return (datetime.now(timezone.utc) - then).total_seconds() / 3600


def last_push_hours(number: int) -> float | None:
    commits = gh_json("api", f"repos/{{owner}}/{{repo}}/pulls/{number}/commits?per_page=100")
    if not commits:
        return None
    return hours_since(commits[-1]["commit"]["committer"]["date"])


def next_free(taken: set[int]) -> int:
    return max(taken, default=0) + 1


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


def taken_numbers(claims: list[dict], main_phases: dict, main_findings: dict) -> tuple[set[int], set[int]]:
    phases = set(main_phases) | {c["phase"] for c in claims if c["phase"] is not None}
    findings = set(main_findings) | {c["finding"] for c in claims if c["finding"] is not None}
    return phases, findings


def show_claims() -> int:
    run("git", "fetch", "--quiet", "origin", "main")
    claims = open_claims()
    main_phases, main_findings = on_main()
    print(f"main: phases up to {max(main_phases)}, findings up to {max(main_findings)}")
    for claim in claims:
        pushed = last_push_hours(claim["number"])
        age = "no commit" if pushed is None else f"last commit {pushed:.1f} h ago"
        numbers = ", ".join(f"{kind} {claim[key]}" for kind, key in (("Phase", "phase"), ("finding", "finding"))
                            if claim[key] is not None) or "no numbers"
        print(f"\n#{claim['number']}  {numbers}  ({age})")
        print(f"    {claim['title'][:110]}")
        own = sorted(claim["files"] - SHARED)
        print("    files: " + (", ".join(own) if own else "(none)"))
        for blocking, problem in claim_problems(claim, claims, main_phases, main_findings):
            print(f"    {'!!' if blocking else '  '} {problem}")
    phases, findings = taken_numbers(claims, main_phases, main_findings)
    print(f"\nnext free: Phase {next_free(phases)}, finding {next_free(findings)}")
    return 0


def claim_problems(claim: dict, claims: list[dict], main_phases: dict, main_findings: dict) -> list[tuple[bool, str]]:
    """What is wrong with a claim: (does it have to change, what)."""
    problems: list[tuple[bool, str]] = []
    if not claim["has_claim_lines"]:
        problems.append((True, "its text does not begin with the Task:, Files: and `Phase P, finding F` lines"))
    others = [c for c in claims if c["number"] != claim["number"]]
    for kind, key, on in (("Phase", "phase", main_phases), ("finding", "finding", main_findings)):
        number = claim[key]
        if number is None:
            continue
        if number in on:
            problems.append((True, f"{kind} {number} is on main: {on[number].strip()[:50]}"))
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
                                    "it is the one that has to stay out of them"))
        else:
            problems.append((True, f"#{other['number']} had {', '.join(both)} first: stay out of "
                                   f"{'it' if len(both) == 1 else 'them'}, or tell the person you work for"))
    return problems


def check_claim(number: int) -> int:
    run("git", "fetch", "--quiet", "origin", "main")
    claims = open_claims()
    mine = next((c for c in claims if c["number"] == number), None)
    if mine is None:
        sys.exit(f"#{number} is not an open pull request")
    main_phases, main_findings = on_main()
    report = Report()
    problems = claim_problems(mine, claims, main_phases, main_findings)
    for blocking, problem in problems:
        (report.no if blocking else report.note)(problem)
    if not report.failed:
        report.ok(f"#{number}: Phase {mine['phase']}, finding {mine['finding']}; no other open pull request has its files")
    else:
        phases, findings = taken_numbers(claims, main_phases, main_findings)
        report.note(f"next free: Phase {next_free(phases)}, finding {next_free(findings)}")
    return 1 if report.failed else 0


def check_runs(sha: str) -> tuple[str, list[str]]:
    """'passed', 'failed', 'running' or 'none' for a commit, and the checks that are not green."""
    data = gh_json("api", f"repos/{{owner}}/{{repo}}/commits/{sha}/check-runs?per_page=100")
    runs = data.get("check_runs", [])
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


def check_merge(number: int) -> int:
    pr = gh_json("pr", "view", str(number), "--json",
                 "number,title,body,state,mergeable,headRefOid,headRefName,files")
    report = Report()
    if pr["state"] != "OPEN":
        sys.exit(f"#{number} is {pr['state'].lower()}")
    head = pr["headRefOid"]
    run("git", "fetch", "--quiet", "origin", "main", pr["headRefName"])
    main = run("git", "rev-parse", "origin/main").strip()

    if pr["title"].startswith("WIP"):
        report.no("the title still begins with WIP:")
    else:
        report.ok("the title has no WIP:")

    if pr["mergeable"] == "MERGEABLE":
        report.ok("GitHub reports it mergeable")
    else:
        report.no(f"GitHub reports {pr['mergeable']} (UNKNOWN right after a push: ask again in a few seconds)")

    def contains_main(commit: str) -> bool:
        return subprocess.run(["git", "merge-base", "--is-ancestor", main, commit]).returncode == 0

    if contains_main(head):
        report.ok(f"origin/main ({main[:7]}) is in the head commit {head[:7]}")
    else:
        behind = run("git", "rev-list", "--count", f"{head}..{main}").strip()
        report.no(f"main has moved: {behind} commits of origin/main ({main[:7]}) are not in {head[:7]}. "
                  "Merge it, build, run the checks, push, and let CI pass on that commit")

    state, bad = check_runs(head)
    if state == "passed":
        report.ok(f"CI passed on the head commit {head[:7]}")
    elif state in ("failed", "running"):
        report.no(f"CI on {head[:7]}: " + "; ".join(bad))
    else:
        # No run: pushed with [skip ci], or the pull request conflicted when it was pushed.
        found = None
        for commit in run("git", "rev-list", "--first-parent", "--max-count=30", head).split()[1:]:
            ancestor_state, _ = check_runs(commit)
            if ancestor_state != "none":
                found = (commit, ancestor_state)
                break
        if found is None:
            report.no(f"no CI run on {head[:7]} or on the 30 commits before it")
        else:
            commit, ancestor_state = found
            changed = [f for f in run("git", "diff", "--name-only", commit, head).split("\n") if f]
            code = [f for f in changed if not DOCUMENT.search(f)]
            if ancestor_state != "passed":
                report.no(f"no CI run on {head[:7]}; the last one, on {commit[:7]}, {ancestor_state}")
            elif not contains_main(commit):
                report.no(f"no CI run on {head[:7]}; the last one, on {commit[:7]}, was before main was merged in")
            elif code:
                report.no(f"no CI run on {head[:7]}; since the last one, on {commit[:7]}, these changed: {', '.join(code[:8])}")
            else:
                report.ok(f"CI passed on {commit[:7]}; since then only documents changed ({', '.join(changed) or 'nothing'}). Say so in the pull request")

    claim = claim_of(pr)
    main_phases, main_findings = on_main()
    if not claim["has_claim_lines"]:
        report.no("the text does not begin with the Task:, Files: and `Phase P, finding F` lines")
    for kind, mine, on in (("Phase", claim["phase"], main_phases), ("finding", claim["finding"], main_findings)):
        if mine is None:
            continue
        if mine in on:
            report.no(f"{kind} {mine} is on main already: {on[mine].strip()[:60]}")
        else:
            report.ok(f"{kind} {mine} is not on main")
    for other in open_claims():
        if other["number"] == number:
            continue
        both = same_files(claim["files"], other["files"])
        if both:
            report.note(f"#{other['number']} is open and names or changes {', '.join(both)}: it will have to merge this")

    suites = len(re.findall(r"(?m)^    RUN_TEST\(", run("git", "show", f"{head}:tests/test_suite.cpp")))
    readme = run("git", "show", f"{head}:README.md")
    stated = {int(n) for n in re.findall(r"\| Engine unit tests \| (\d+) suites", readme)}
    stated |= {int(n) for n in re.findall(r"All (\d+) engine test suites passed", readme)}
    stated |= {int(n) for n in re.findall(r"# Run the (\d+) engine test suites", readme)}
    if stated == {suites}:
        report.ok(f"README.md says {suites} unit suites, and tests/test_suite.cpp has {suites}")
    else:
        report.no(f"tests/test_suite.cpp has {suites} unit suites; README.md says {sorted(stated)}")

    print("\nnot ready" if report.failed else f"\nready: gh pr merge {number} --merge, then `gh run list --branch main --limit 3`")
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
