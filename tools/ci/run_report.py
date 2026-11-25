#!/usr/bin/env python3
"""What a CI run did: which jobs failed and what they said, and where the time went.

    python3 tools/ci/run_report.py N              # the newest run on the head commit of pull request #N
    python3 tools/ci/run_report.py --run RUN      # that run (the number in its address)
    python3 tools/ci/run_report.py N --times      # also every job with its long steps, the notices, the warnings
    python3 tools/ci/run_report.py --row COMMIT   # the newest run on that commit as three cells of a table
    python3 tools/ci/run_report.py --now          # the runs that are going: how many jobs have a machine, how many wait
    python3 tools/ci/run_report.py --self-test

CI's logs cannot be read from every sandbox. A job's annotations can: the
API itself serves them, and every job ends with a step that writes what
failed as annotations (tools/ci/annotate_failure.py). Reading them took a
run's number, then a job's id, then a third command, and an agent whose run
had failed did all three by hand or guessed. This is the three in one.

Without `--times` it reads the run, its jobs, and the annotations of the jobs
that failed: three or four requests. With `--times` it reads the annotations
of every job that ran, because that is where a step says what it measured
(the unit suites of the sanitizer build give each stretch's time and memory,
ccache says what it had), and where a job that passed can still carry a
warning (a stretch of the unit suites that passed only by itself).

It also says how long the jobs waited for a machine. No more than a fixed
number of jobs run at once over all the runs of the account, so a run can
be long without any of its jobs being long: on 24 November 2026 a lot had
ten jobs without a machine two and a half minutes after its push.

Exit status: 0 every job that ran passed; 1 a job failed, or the run was
cancelled (a later push to the same pull request ends a run); 3 the run is
still going; 2 it could not be read, or there is none on that commit.

`--row` is for the landing session (tools/watch/watch.sh): when the run
began, when its last job ended, and `RUN, 5.3 min`, with tabs between them.

`--now` counts over every run that has not ended. When jobs wait while
others run, the number that run is the account's limit, or close to it.

It reads GitHub and changes nothing there.
"""

from __future__ import annotations

import json
import subprocess
import sys
from datetime import datetime, timezone

REPO = "repos/{owner}/{repo}"      # `gh api` fills the two in from the clone
LONG_STEP = 20                     # seconds: a step shorter than this is not where the time went
LONG_WAIT = 30                     # seconds without a machine that are worth a line
# The runner says this of every job, about the actions the workflow uses.
NOISE = ("Node.js 20 is deprecated", "Node.js 20 actions are deprecated")
# What GitHub itself says of a failed step, and nothing about why.
BARE = "Process completed with exit code"
BAD = ("failure", "timed_out", "cancelled", "startup_failure", "action_required")


def moment(text: str | None) -> datetime | None:
    return datetime.fromisoformat(text.replace("Z", "+00:00")) if text else None


def seconds(begin: str | None, end: str | None) -> float | None:
    first, last = moment(begin), moment(end)
    return (last - first).total_seconds() if first and last else None


def minutes(amount: float) -> str:
    return f"{amount / 60:.1f} min"


def clock(text: str | None) -> str:
    return text[11:19] if text else "?"


def counts_of(jobs: list[dict]) -> str:
    """`15 passed, 1 failed`, in an order that puts what matters first."""
    names = {"success": "passed", "failure": "failed", "timed_out": "timed out", "cancelled": "cancelled",
             "skipped": "skipped", None: "not ended"}
    order = ["failure", "timed_out", "cancelled", None, "success", "skipped"]
    found: dict = {}
    for job in jobs:
        key = job.get("conclusion") if job.get("status") == "completed" else None
        found[key] = found.get(key, 0) + 1
    known = [f"{found[key]} {names[key]}" for key in order if key in found]
    other = [f"{count} {key}" for key, count in found.items() if key not in names]
    return ", ".join(known + other)


def long_steps(job: dict) -> str:
    parts = []
    for step in job.get("steps") or []:
        took = seconds(step.get("started_at"), step.get("completed_at"))
        if took is not None and took >= LONG_STEP:
            parts.append(f"{step['name']} {took:.0f} s")
    return "; ".join(parts)


def failed_step(job: dict) -> str:
    for step in job.get("steps") or []:
        if step.get("conclusion") in BAD:
            return step["name"]
    return ""


def worth_reading(annotation: dict) -> bool:
    return not any(noise in (annotation.get("message") or "") for noise in NOISE)


def report(run: dict, jobs: list[dict], annotations: dict[int, list[dict]], times: bool,
           now: datetime) -> tuple[list[str], int]:
    """The lines to print and the exit status. `annotations` is by job id."""
    lines: list[str] = []
    ran = [job for job in jobs if job.get("conclusion") != "skipped"]
    going = [job for job in ran if job.get("status") != "completed"]
    bad = [job for job in ran if job.get("status") == "completed" and job.get("conclusion") in BAD]
    pulls = ", ".join(f"#{pull['number']}" for pull in run.get("pull_requests") or [])
    where = f"{run.get('event', '?')}" + (f", {pulls}" if pulls else "")
    state = run.get("status", "?") + (f", {run['conclusion']}" if run.get("conclusion") else "")
    lines.append(f"run {run.get('id', '?')} on {(run.get('head_sha') or '?')[:7]} ({where}): {state}. "
                 f"{len(jobs)} jobs: {counts_of(jobs)}")

    began = run.get("run_started_at") or run.get("created_at")
    ends = [job["completed_at"] for job in ran if job.get("completed_at")]
    if going or not ran:
        gone = (now - moment(began)).total_seconds() if began else None
        if gone is not None:
            lines.append(f"begun at {clock(began)} UTC and going for {minutes(gone)}: "
                         + (", ".join(sorted(job["name"] for job in going)) or "no job has a machine yet"))
    elif ends:
        took = seconds(began, max(ends))
        lines.append(f"{minutes(took)} from its start to the end of its last job ({clock(began)} to {clock(max(ends))} UTC)")
    waits = [(seconds(job.get("created_at"), job.get("started_at")) or 0, job["name"]) for job in ran
             if job.get("started_at")]
    waiting = [job["name"] for job in ran if not job.get("started_at")]
    late = sorted((wait, name) for wait, name in waits if wait >= LONG_WAIT)
    if late:
        lines.append(f"{len(late)} of its jobs waited {late[0][0]:.0f} to {late[-1][0]:.0f} s for a machine "
                     f"(the longest: {late[-1][1]})" if len(late) > 1 else
                     f"one job waited {late[0][0]:.0f} s for a machine ({late[0][1]})")
    if waiting and going:
        lines.append(f"without a machine yet: {', '.join(sorted(waiting))}")

    for job in bad:
        step = failed_step(job)
        lines.append("")
        lines.append(f"{job['conclusion'].upper().replace('_', ' ')}: {job['name']}"
                     + (f", in the step \"{step}\"" if step else "") + f" (job {job['id']})")
        said = [note for note in annotations.get(job["id"], []) if note.get("annotation_level") == "failure"]
        telling = [note for note in said if BARE not in (note.get("message") or "")]
        for note in telling or said:
            title = f"{note['title']}: " if note.get("title") else ""
            for index, line in enumerate((title + (note.get("message") or "")).splitlines() or [""]):
                lines.append(("    " if index == 0 else "      ") + line)
        if not telling:
            lines.append("    (nothing more: the job did not reach the step that says what failed, or was cancelled)")

    warnings = [(job["name"], note) for job in ran for note in annotations.get(job["id"], [])
                if note.get("annotation_level") == "warning" and worth_reading(note)]
    if warnings:
        lines.append("")
        for name, note in warnings:
            title = f"{note['title']}: " if note.get("title") else ""
            lines.append(f"warning, {name}: {title}{note.get('message') or ''}")

    if times:
        lines.append("")
        timed = [(seconds(job.get("started_at"), job.get("completed_at")), job) for job in ran]
        for took, job in sorted(timed, key=lambda pair: -(pair[0] or 0)):
            what = {"success": "passed", None: "going"}.get(job.get("conclusion"), job.get("conclusion"))
            length = minutes(took) if took is not None else "       "
            steps = long_steps(job)
            lines.append(f"{length:>8}  {what:9} {job['name']}" + (f": {steps}" if steps else ""))
        notices = [(job["name"], note) for job in ran for note in annotations.get(job["id"], [])
                   if note.get("annotation_level") == "notice" and worth_reading(note)]
        if notices:
            lines.append("")
            for name, note in notices:
                title = f"{note['title']}: " if note.get("title") else ""
                lines.append(f"notice, {name}: {title}{note.get('message') or ''}")

    if bad or run.get("conclusion") in BAD:
        return lines, 1
    if going or not ran or run.get("status") != "completed":
        return lines, 3
    return lines, 0


def row_of(run: dict, jobs: list[dict]) -> str:
    """`02:23<tab>02:29<tab>45532555413, 5.6 min`, or nothing while a job has not ended."""
    began = run.get("run_started_at") or run.get("created_at")
    ran = [job for job in jobs if job.get("conclusion") != "skipped"]
    if not began or not ran or any(not job.get("completed_at") for job in ran):
        return ""
    last = max(job["completed_at"] for job in ran)
    return f"{began[11:16]}\t{last[11:16]}\t{run.get('id', '?')}, {minutes(seconds(began, last))}"


def at_once(runs: list[tuple[dict, list[dict]]]) -> list[str]:
    """One line for each run that is going, and the sum: jobs with a machine, jobs without."""
    lines, running, waiting = [], 0, 0
    for run, jobs in runs:
        has = sum(1 for job in jobs if job.get("status") == "in_progress")
        waits = sum(1 for job in jobs if job.get("status") in ("queued", "waiting", "pending", "requested"))
        if not has and not waits:
            continue
        running, waiting = running + has, waiting + waits
        pulls = ", ".join(f"#{pull['number']}" for pull in run.get("pull_requests") or []) or run.get("head_branch") or "?"
        lines.append(f"run {run.get('id', '?')} ({pulls}, {(run.get('head_sha') or '?')[:7]}, begun {clock(run.get('run_started_at') or run.get('created_at'))}): "
                     f"{has} with a machine, {waits} without")
    lines.append(f"at once: {running} jobs with a machine, {waiting} waiting for one, in {len(lines)} run(s)")
    return lines


def gh_json(path: str):
    result = subprocess.run(["gh", "api", path], capture_output=True, text=True)
    if result.returncode != 0:
        print(f"gh api {path}\n{result.stderr.strip()}", file=sys.stderr)
        sys.exit(2)
    return json.loads(result.stdout)


def newest_run_on(head: str, called: str) -> int:
    runs = gh_json(f"{REPO}/actions/runs?head_sha={head}&per_page=30")["workflow_runs"]
    if not runs:
        print(f"no run on {head[:7]}{called}. A push starts none while the pull request conflicts with main, "
              "or when its message has the mark that skips CI.", file=sys.stderr)
        sys.exit(2)
    return max(runs, key=lambda run: run["created_at"])["id"]


def newest_run_of(number: int) -> int:
    return newest_run_on(gh_json(f"{REPO}/pulls/{number}")["head"]["sha"], f", the head commit of #{number}")


def self_test() -> int:
    def job(identity: int, name: str, conclusion: str | None, begin: int, end: int | None, steps=None,
            created: int | None = None) -> dict:
        at = lambda second: f"2026-11-25T02:{second // 60:02d}:{second % 60:02d}Z"
        return {"id": identity, "name": name, "status": "completed" if conclusion else "in_progress",
                "conclusion": conclusion, "created_at": at(begin if created is None else created),
                "started_at": at(begin), "completed_at": at(end) if end is not None else None,
                "steps": [{"name": step, "conclusion": result, "started_at": at(first), "completed_at": at(last)}
                          for step, result, first, last in (steps or [])]}

    run = {"id": 7, "head_sha": "abcdef1234", "event": "pull_request", "status": "completed", "conclusion": "success",
           "run_started_at": "2026-11-25T02:00:00Z", "pull_requests": [{"number": 386}]}
    now = datetime(2026, 11, 25, 2, 10, tzinfo=timezone.utc)
    passed = [job(1, "Build & unit tests", "success", 2, 170, [("Build", "success", 5, 6), ("Engine test suites", "success", 6, 111)]),
              job(2, "Sanitizers: unit suites", "success", 3, 240)]
    failed = [passed[0], job(2, "Sanitizers: unit suites", "failure", 3, 240,
                             [("Sanitizer build", "success", 5, 100), ("Unit suites in the sanitizer build", "failure", 100, 240)])]
    notes = {2: [{"annotation_level": "failure", "title": "", "message": "Process completed with exit code 2."},
                 {"annotation_level": "failure", "title": "what failed (1 of 2)", "message": "Assertion `took < 5 s' failed.\nsecond line"},
                 {"annotation_level": "warning", "title": "", "message": "Node.js 20 is deprecated. The following actions..."},
                 {"annotation_level": "notice", "title": "the unit suites as 6 processes", "message": "117 s in all"}]}
    cases = []

    lines, status = report(run, passed, {}, False, now)
    cases.append(("a run that passed is status 0", status == 0))
    cases.append(("its first line counts the jobs", lines[0] == "run 7 on abcdef1 (pull_request, #386): completed, success. 2 jobs: 2 passed"))
    cases.append(("its length is from the start to the last job's end", lines[1].startswith("4.0 min from its start")))
    cases.append(("no table without --times", len(lines) == 2))

    bad_run = dict(run, conclusion="failure")
    lines, status = report(bad_run, failed, notes, False, now)
    text = "\n".join(lines)
    cases.append(("a failed job is status 1", status == 1))
    cases.append(("the job and its step are named", 'FAILURE: Sanitizers: unit suites, in the step "Unit suites in the sanitizer build" (job 2)' in text))
    cases.append(("what the job said is printed, every line", "what failed (1 of 2): Assertion `took < 5 s' failed." in text and "      second line" in text))
    cases.append(("GitHub's bare line is left out when the job said more", "Process completed" not in text))
    cases.append(("the runner's own warning is left out", "Node.js" not in text))
    cases.append(("a notice is not shown without --times", "117 s in all" not in text))

    bare = {2: [notes[2][0]]}
    text = "\n".join(report(bad_run, failed, bare, False, now)[0])
    cases.append(("a job that said nothing: the bare line, and that there is no more", "Process completed with exit code 2." in text and "nothing more" in text))

    lines, status = report(run, passed, notes, True, now)
    text = "\n".join(lines)
    cases.append(("--times lists the jobs, the longest first", text.index("Sanitizers: unit suites") < text.index("Build & unit tests", len(lines[0]))))
    cases.append(("--times gives the long steps and leaves out the short", "Engine test suites 105 s" in text and "Build 1 s" not in text))
    cases.append(("--times shows the notices", "notice, Sanitizers: unit suites: the unit suites as 6 processes: 117 s in all" in text))

    retried = {2: [{"annotation_level": "warning", "title": "a stretch of the unit suites passed only by itself", "message": "stretch 4 failed beside the others"}]}
    lines, status = report(run, passed, retried, True, now)
    cases.append(("a warning on a job that passed is shown, and the run still passes",
                  status == 0 and any(line.startswith("warning, Sanitizers: unit suites: a stretch") for line in lines)))

    going = [passed[0], job(2, "Sanitizers: unit suites", None, 3, None)]
    lines, status = report(dict(run, status="in_progress", conclusion=None), going, {}, False, now)
    cases.append(("a run that is going is status 3, and says which jobs are", status == 3 and "going for 10.0 min: Sanitizers: unit suites" in lines[1]))

    short = passed + [job(3, "Sanitizers: fuzzing", "skipped", 0, 0)]
    lines, status = report(run, short, {}, False, now)
    cases.append(("skipped jobs are counted and do not fail the run", status == 0 and lines[0].endswith("3 jobs: 2 passed, 1 skipped")))

    lines, status = report(dict(run, conclusion="cancelled"), [job(1, "Build & unit tests", "cancelled", 2, 30)], {}, False, now)
    cases.append(("a cancelled run is status 1", status == 1 and "CANCELLED: Build & unit tests" in "\n".join(lines)))

    queued = [job(1, "Build & unit tests", "success", 2, 170), job(2, "Sanitizers: unit suites", "success", 160, 400, created=2),
              job(3, "Sanitizers: fuzzing", "success", 100, 300, created=2)]
    text = "\n".join(report(run, queued, {}, False, now)[0])
    cases.append(("jobs that waited for a machine are said, with the longest", "2 of its jobs waited 98 to 158 s for a machine (the longest: Sanitizers: unit suites)" in text))
    text = "\n".join(report(run, queued[:2], {}, False, now)[0])
    cases.append(("one job that waited", "one job waited 158 s for a machine (Sanitizers: unit suites)" in text))

    lines, status = report(dict(run, status="queued", conclusion=None), [], {}, False, now)
    cases.append(("a run with no job yet is status 3", status == 3 and "no job has a machine yet" in lines[1]))

    cases.append(("a row of the table of lots", row_of(run, short) == "02:00\t02:04\t7, 4.0 min"))
    waiting_job = dict(job(4, "Sanitizers: fuzzing", None, 0, None), status="queued", started_at=None)
    counted = at_once([(run, going + [waiting_job]), (dict(run, id=8, pull_requests=[], head_branch="main"), passed)])
    cases.append(("--now counts the jobs with a machine and without, and leaves out a run that has ended",
                  counted == ["run 7 (#386, abcdef1, begun 02:00:00): 1 with a machine, 1 without",
                              "at once: 1 jobs with a machine, 1 waiting for one, in 1 run(s)"]))
    cases.append(("no row while a job is going", row_of(run, going) == ""))

    failed_count = 0
    for name, ok in cases:
        if not ok:
            failed_count += 1
            print(f"FAIL  {name}")
    print(f"run_report --self-test: {len(cases) - failed_count} of {len(cases)} passed")
    return 1 if failed_count else 0


def main(arguments: list[str]) -> int:
    if arguments == ["--self-test"]:
        return self_test()
    times = "--times" in arguments
    rest = [argument for argument in arguments if argument != "--times"]
    if rest == ["--now"]:
        active = [run for run in gh_json(f"{REPO}/actions/runs?per_page=40")["workflow_runs"] if run.get("status") != "completed"]
        print("\n".join(at_once([(run, gh_json(f"{REPO}/actions/runs/{run['id']}/jobs?per_page=100")["jobs"]) for run in active])))
        return 0
    if len(rest) == 2 and rest[0] == "--row":
        identity = newest_run_on(rest[1], "")
        cells = row_of(gh_json(f"{REPO}/actions/runs/{identity}"),
                       gh_json(f"{REPO}/actions/runs/{identity}/jobs?per_page=100")["jobs"])
        print(cells)
        return 0 if cells else 3
    if len(rest) == 2 and rest[0] == "--run" and rest[1].isdigit():
        identity = int(rest[1])
    elif len(rest) == 1 and rest[0].lstrip("#").isdigit():
        identity = newest_run_of(int(rest[0].lstrip("#")))
    else:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    run = gh_json(f"{REPO}/actions/runs/{identity}")
    jobs = gh_json(f"{REPO}/actions/runs/{identity}/jobs?per_page=100")["jobs"]
    annotations: dict[int, list[dict]] = {}
    for job in jobs:
        ended_badly = job.get("status") == "completed" and job.get("conclusion") in BAD
        if ended_badly or (times and job.get("conclusion") != "skipped" and job.get("started_at")):
            annotations[job["id"]] = gh_json(f"{REPO}/check-runs/{job['id']}/annotations")
    lines, status = report(run, jobs, annotations, times, datetime.now(timezone.utc))
    print("\n".join(lines))
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
