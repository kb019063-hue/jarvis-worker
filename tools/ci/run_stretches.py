#!/usr/bin/env python3
"""Run the unit suites as several processes side by side.

    python3 tools/ci/run_stretches.py BINARY N       # N equal stretches of BINARY's suites
    python3 tools/ci/run_stretches.py BINARY a,b,c   # four: from the start, and from the suites a, b, c
    python3 tools/ci/run_stretches.py --self-test

`BINARY` is the test binary (`build/test_suite`, `build-asan/test_suite`).
With `LITEBIRD_SUITE_SHARD=K/N` in its environment it runs the K-th of N
equal stretches of its suites and says at the end
"Stretch K of N: its X of the T engine test suites passed."
(tests/test_suite.cpp). This starts the N of them, waits, prints what each
wrote, one after another in the order of a whole run, and passes when

  * every one of them exited 0 and said that sentence,
  * they agree on T, and
  * their X add up to T: every suite was some process's.

It is what `make asan-unit` runs. One process took 8 min 52 s there in CI on
24 November 2026, on a machine with four cores, and that was the length of a
whole CI run.

Equal stretches are equal in suites, not in time: three of them took 22, 322
and 24 seconds that evening, because the few suites that nest pages 200,000
deep stand together in the middle of the order. So the second argument may
instead name the suites at which a new stretch begins, in the order of the
run (`LITEBIRD_SUITE_CUTS`, tests/test_suite.cpp); the Makefile keeps the
names (`UNIT_SUITE_CUTS`). A name that is not a suite fails every stretch.

A stretch that fails is printed last, so that the end of the output is the
failure, which is where a person and tools/ci/annotate_failure.py look.

It says at the end how long each stretch took and how much memory it used
at most, and in CI it says so as a notice on the job as well, which can be
read where the log cannot (`gh api repos/OWNER/REPO/check-runs/JOB_ID/
annotations`). The stretches are equal in suites, not in time, and the step
that runs this also compiles the test binary first: without these numbers
nobody could say whether the step is long for the compiling, for one slow
stretch, or for all of them.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
import time

SENTENCE = re.compile(r"Stretch (\d+) of (\d+): its (\d+) of the (\d+) engine test suites passed\.")
SLOWEST = re.compile(r"The slowest suites of \d+ \(\d+ s in all\): (.+)")


def verdict(results: list[tuple[int, str]]) -> tuple[bool, str]:
    """(passed, the line to end on) for the (exit status, output) of each stretch, in order."""
    count = len(results)
    totals: set[int] = set()
    ran = 0
    for index, (status, output) in enumerate(results):
        said = SENTENCE.search(output)
        if status != 0:
            return False, f"stretch {index} of {count} failed (exit status {status})"
        if not said:
            return False, f"stretch {index} of {count} exited 0 and did not say how many suites it ran"
        if (int(said.group(1)), int(said.group(2))) != (index, count):
            return False, f"stretch {index} of {count} says it is stretch {said.group(1)} of {said.group(2)}"
        ran += int(said.group(3))
        totals.add(int(said.group(4)))
    if len(totals) != 1:
        return False, f"the stretches do not agree on how many suites there are: {sorted(totals)}"
    total = totals.pop()
    if ran != total:
        return False, f"the stretches ran {ran} suites between them, and there are {total}"
    return True, f"All {total} engine test suites passed successfully, as {count} processes side by side!"


def measured(results: list[tuple[int, str]], seconds: list[float], megabytes: list[int], whole: float) -> str:
    """One line: how long each stretch took, its suites, and its most memory."""
    parts = []
    for index, (_, output) in enumerate(results):
        said = SENTENCE.search(output)
        suites = f"{said.group(3)} suites" if said else "no count"
        parts.append(f"stretch {index}: {suites} in {seconds[index]:.0f} s, {megabytes[index]} MB at most")
    slowest = max(range(len(seconds)), key=lambda index: seconds[index]) if seconds else 0
    named = SLOWEST.search(results[slowest][1]) if results else None
    return (f"{len(results)} processes side by side, {whole:.0f} s in all (the slowest is stretch {slowest}); "
            + "; ".join(parts)
            + (f". The slowest suites of stretch {slowest}: {named.group(1).strip()}" if named else ""))


def stretches_of(argument: str) -> tuple[int, str] | None:
    """(how many stretches, the names they begin at) for the second argument:
    a number of equal stretches, or the names, comma-separated."""
    if argument.isdigit():
        return (int(argument), "") if int(argument) >= 1 else None
    names = argument.split(",")
    if not all(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) for name in names):
        return None
    return len(names) + 1, argument


def machine() -> str:
    """The cores and the memory of this machine, for the notice."""
    memory = ""
    try:
        with open("/proc/meminfo", encoding="ascii") as info:
            found = re.search(r"MemTotal:\s+(\d+) kB", info.read())
            memory = f", {int(found.group(1)) // 1024} MB of memory" if found else ""
    except OSError:
        pass
    return f"{os.cpu_count() or '?'} cores{memory}"


def run(binary: str, count: int, cuts: str = "") -> int:
    started = time.time()
    with tempfile.TemporaryDirectory(prefix="litebird-stretches-") as directory:
        processes = []
        for index in range(count):
            log = open(os.path.join(directory, f"stretch{index}.log"), "wb")
            environment = dict(os.environ, LITEBIRD_SUITE_SHARD=f"{index}/{count}")
            environment.pop("LITEBIRD_SUITE_CUTS", None)
            if cuts:
                environment["LITEBIRD_SUITE_CUTS"] = cuts
            processes.append((subprocess.Popen([binary], stdout=log, stderr=subprocess.STDOUT, env=environment), log))
        # Each is waited for by itself (`wait4`), for the moment it ended and
        # the most memory it held: waiting for them in order would give the
        # first one the time of the slowest.
        statuses: list[int | None] = [None] * count
        seconds = [0.0] * count
        megabytes = [0] * count
        while any(status is None for status in statuses):
            for index, (process, _) in enumerate(processes):
                if statuses[index] is not None:
                    continue
                pid, raw, usage = os.wait4(process.pid, os.WNOHANG)
                if pid == 0:
                    continue
                statuses[index] = os.waitstatus_to_exitcode(raw)
                process.returncode = statuses[index]   # so that Popen does not wait for it again
                seconds[index] = time.time() - started
                megabytes[index] = usage.ru_maxrss // 1024   # Linux counts in kilobytes
            if any(status is None for status in statuses):
                time.sleep(0.1)
        results: list[tuple[int, str]] = []
        for index, (_, log) in enumerate(processes):
            log.close()
            with open(log.name, "rb") as written:
                results.append((statuses[index] or 0, written.read().decode("utf-8", "replace")))
    # What passed first, what failed last: the end of the output is the failure.
    order = sorted(range(count), key=lambda index: (results[index][0] != 0, index))
    for index in order:
        sys.stdout.write(results[index][1])
        if not results[index][1].endswith("\n"):
            sys.stdout.write("\n")
    passed, line = verdict(results)
    whole = time.time() - started
    times = measured(results, seconds, megabytes, whole) + f". The machine: {machine()}"
    print(f"\nThe unit suites as {times}")
    if os.environ.get("GITHUB_ACTIONS") == "true":
        # A notice on the job: read through the API where the log cannot be.
        print(f"::notice title=the unit suites as {count} processes::{times}")
    colour = "\033[1;32m" if passed else "\033[1;31mFAILED\033[0m: "
    print(f"\n{colour}{line}{'' if not passed else chr(27) + '[0m'} ({whole:.0f} s)\n", flush=True)
    return 0 if passed else 1


def self_test() -> int:
    def said(index: int, count: int, ran: int, total: int) -> str:
        return f"Running test: a... PASSED\n\nStretch {index} of {count}: its {ran} of the {total} engine test suites passed.\n"

    three = [(0, said(0, 3, 140, 420)), (0, said(1, 3, 140, 420)), (0, said(2, 3, 140, 420))]
    cases = [
        ("three stretches that passed and add up", verdict(three),
         (True, "All 420 engine test suites passed successfully, as 3 processes side by side!")),
        ("one of them failed", verdict(three[:1] + [(134, "Running test: b... Assertion failed\n")] + three[2:]),
         (False, "stretch 1 of 3 failed (exit status 134)")),
        ("one exited 0 without the sentence: a whole run's binary that knows no stretches",
         verdict(three[:2] + [(0, "All 420 engine test suites passed successfully!\n")]),
         (False, "stretch 2 of 3 exited 0 and did not say how many suites it ran")),
        ("they add up to less than there are", verdict(three[:2] + [(0, said(2, 3, 139, 420))]),
         (False, "the stretches ran 419 suites between them, and there are 420")),
        ("they do not agree on the number", verdict(three[:2] + [(0, said(2, 3, 141, 421))]),
         (False, "the stretches do not agree on how many suites there are: [420, 421]")),
        ("one says it is another stretch", verdict(three[:2] + [(0, said(1, 3, 140, 420))]),
         (False, "stretch 2 of 3 says it is stretch 1 of 3")),
        ("one process is a whole run", verdict([(0, said(0, 1, 420, 420))]),
         (True, "All 420 engine test suites passed successfully, as 1 processes side by side!")),
        ("what each stretch took, and which was the slowest",
         measured(three, [101.4, 240.6, 99.0], [900, 1500, 800], 241.2),
         "3 processes side by side, 241 s in all (the slowest is stretch 1); "
         "stretch 0: 140 suites in 101 s, 900 MB at most; stretch 1: 140 suites in 241 s, 1500 MB at most; "
         "stretch 2: 140 suites in 99 s, 800 MB at most"),
        ("the slowest suites of the slowest stretch are named, when the binary says them",
         measured([(0, said(0, 2, 210, 420)),
                   (0, "\nThe slowest suites of 210 (300 s in all): deep 250 s, wide 30 s\n" + said(1, 2, 210, 420))],
                  [20.0, 300.0], [700, 5000], 300.0),
         "2 processes side by side, 300 s in all (the slowest is stretch 1); "
         "stretch 0: 210 suites in 20 s, 700 MB at most; stretch 1: 210 suites in 300 s, 5000 MB at most. "
         "The slowest suites of stretch 1: deep 250 s, wide 30 s"),
        ("a number is that many equal stretches", stretches_of("3"), (3, "")),
        ("names are one stretch more than there are names", stretches_of("deep_one,wide_two,last_3"), (4, "deep_one,wide_two,last_3")),
        ("one name is two stretches", stretches_of("deep_one"), (2, "deep_one")),
        ("nought is not a number of stretches", stretches_of("0"), None),
        ("an empty name is not a name", stretches_of("deep_one,,wide_two"), None),
        ("nor is anything that could not be a suite", stretches_of("deep-one"), None),
        ("a stretch that died says no count, and the line is still written",
         measured([(134, "Running test: b... Assertion failed\n")], [12.0], [300], 12.0),
         "1 processes side by side, 12 s in all (the slowest is stretch 0); stretch 0: no count in 12 s, 300 MB at most"),
    ]
    failed = 0
    for what, got, want in cases:
        if got != want:
            failed += 1
            print(f"  FAIL  {what}: got {got!r}, expected {want!r}")
    print(f"run_stretches --self-test: {len(cases) - failed} of {len(cases)} passed")
    return 1 if failed else 0


def main(arguments: list[str]) -> int:
    if arguments == ["--self-test"]:
        return self_test()
    stretches = stretches_of(arguments[1]) if len(arguments) == 2 else None
    if stretches is None:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    return run(arguments[0], stretches[0], stretches[1])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
