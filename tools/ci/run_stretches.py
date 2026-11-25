#!/usr/bin/env python3
"""Run the unit suites as several processes side by side.

    python3 tools/ci/run_stretches.py BINARY N           # N equal stretches of BINARY's suites
    python3 tools/ci/run_stretches.py BINARY a,b,c       # four: from the start, and from the suites a, b, c
    python3 tools/ci/run_stretches.py BINARY a,b,c 1,2   # the same, stretches 1 and 2 after the others
    python3 tools/ci/run_stretches.py BINARY a,b,c 1,2 early   # stretches 0 and 3, and no others
    python3 tools/ci/run_stretches.py BINARY a,b,c 1,2 late    # stretches 1 and 2, and no others
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

Some fifty places in the suites measure time ("this loop takes a fifth of a
second, and took 44 when it was quadratic: fail above 5"). In the sanitizer
build such a place has little room, and none beside a process that is
turning over 5 GB: the first run with the deep suites in stretches of their
own, all four side by side, failed in `inner_html_is_parsed_as_the_content_
of_its_element` at its sixth second (run 45459129005, 24 November). So:

  * a third argument names the stretches that are started when the others
    have ended (`UNIT_SUITE_LATE`, the deep ones): what measures time never
    runs beside them;
  * a stretch that fails beside the others, with no report of a sanitizer
    in what it wrote, is run once more by itself when everything else has
    ended. If it passes then, the run passes, and says which stretch that
    was and what it had failed with (a warning on the job in CI). A failure
    that is real fails twice. A sanitizer's report is never asked again.

With `early` or `late` as a fourth argument only those stretches are run:
the ones that are not late, or the late ones. That is for two machines, one
call each (CI's two jobs for the unit suites since 25 November 2026): the
late stretches are the deep ones, 72 to 87 seconds there that began when the
others had taken 102 to 127, on a machine with nothing else to do. The two
calls share the binary's order and the cuts, so stretch K is the same suites
in both, and every suite is still some process's; each call checks its own
stretches, and that they do not add up to more than there are.

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
HALF = re.compile(r"Half of the time of these suites was over when (\w+) began\.")
SANITIZER = re.compile(r"ERROR: \w*Sanitizer|SUMMARY: \w*Sanitizer|runtime error: |ERROR: libFuzzer")
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def verdict(results: list[tuple[int, str]], indexes: list[int] | None = None,
            count: int | None = None) -> tuple[bool, str]:
    """(passed, the line to end on) for the (exit status, output) of each stretch, in order.

    `indexes` are the stretches the results are of, and `count` how many
    stretches there are: without them the results are of every stretch.
    """
    count = len(results) if count is None else count
    indexes = list(range(len(results))) if indexes is None else indexes
    totals: set[int] = set()
    ran = 0
    for index, (status, output) in zip(indexes, results):
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
    if len(indexes) < count:
        # Some of the stretches: the others are another call's, on another
        # machine. These may not be all the suites, and cannot be more.
        if ran >= total:
            return False, (f"stretches {numbers(indexes)} of {count} ran {ran} suites between them, and there are "
                           f"{total}: nothing is left for the other stretches")
        return True, (f"{'Stretch' if len(indexes) == 1 else 'Stretches'} {numbers(indexes)} of {count} passed: "
                      f"{ran} of the {total} engine test suites. "
                      f"The other {total - ran} are the other stretches', which another call runs")
    if ran != total:
        return False, f"the stretches ran {ran} suites between them, and there are {total}"
    return True, f"All {total} engine test suites passed successfully, as {count} processes side by side!"


def numbers(indexes: list[int]) -> str:
    """ "0, 1, 4 and 5" """
    words = [str(index) for index in indexes]
    return words[0] if len(words) == 1 else ", ".join(words[:-1]) + " and " + words[-1]


def measured(results: list[tuple[int, str]], seconds: list[float], megabytes: list[int], whole: float,
             late: frozenset[int] = frozenset(), indexes: list[int] | None = None) -> str:
    """One line: how long each stretch took, its suites, and its most memory.

    `indexes` are the stretches the results are of (all of them, without it).
    A late stretch is said to have run after the others only when the others
    were run by this call too.
    """
    indexes = list(range(len(results))) if indexes is None else indexes
    waited = late if any(index not in late for index in indexes) else frozenset()
    parts = []
    for place, (_, output) in enumerate(results):
        said = SENTENCE.search(output)
        suites = f"{said.group(3)} suites" if said else "no count"
        after = " (after the others)" if indexes[place] in waited else ""
        half = HALF.search(output)
        middle = f", half of it over when {half.group(1)} began" if half else ""
        parts.append(f"stretch {indexes[place]}{after}: {suites} in {seconds[place]:.0f} s, "
                     f"{megabytes[place]} MB at most{middle}")
    slowest = max(range(len(seconds)), key=lambda place: seconds[place]) if seconds else 0
    named = SLOWEST.search(results[slowest][1]) if results else None
    name = indexes[slowest] if indexes else 0
    return (f"{len(results)} processes side by side, {whole:.0f} s in all (the slowest is stretch {name}); "
            + "; ".join(parts)
            + (f". The slowest suites of stretch {name}: {named.group(1).strip()}" if named else ""))


def stretches_of(argument: str) -> tuple[int, str] | None:
    """(how many stretches, the names they begin at) for the second argument:
    a number of equal stretches, or the names, comma-separated."""
    if argument.isdigit():
        return (int(argument), "") if int(argument) >= 1 else None
    names = argument.split(",")
    if not all(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) for name in names):
        return None
    return len(names) + 1, argument


def late_of(argument: str, count: int) -> frozenset[int] | None:
    """The stretches that run after the others, from "1,2"; None when it does
    not name stretches there are, or names every one of them."""
    if argument == "":
        return frozenset()
    if not re.fullmatch(r"\d+(,\d+)*", argument):
        return None
    late = frozenset(int(number) for number in argument.split(","))
    if any(index >= count for index in late) or len(late) >= count:
        return None
    return late


def only_of(argument: str, late: frozenset[int]) -> str | None:
    """The fourth argument: "early" or "late", which mean something only when
    some stretches are late; None for anything else."""
    return argument if argument in ("early", "late") and late else None


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


def may_be_run_again(status: int, output: str) -> bool:
    """A stretch that failed, and not by a sanitizer's report: what it failed
    with may be a place that measures time, which is asked once more with the
    machine to itself. A sanitizer's report is a finding whatever the load."""
    return status != 0 and not SANITIZER.search(output)


def last_words(output: str) -> str:
    """The last line a stretch wrote that says anything, for the warning."""
    lines = [ANSI.sub("", line).strip() for line in output.split("\n")]
    lines = [line for line in lines if line]
    return lines[-1][:300] if lines else "nothing"


def phases_of(count: int, late: frozenset[int], only: str = "") -> list[list[int]]:
    """Which stretches are started together: the ones that are not late, and
    when they have ended the late ones. `only` leaves one of the two out:
    "early" the late ones, "late" the others."""
    first = [index for index in range(count) if index not in late] if only != "late" else []
    second = [index for index in range(count) if index in late] if only != "early" else []
    return [phase for phase in (first, second) if phase]


def side_by_side(binary: str, count: int, cuts: str, indexes: list[int], directory: str,
                 name: str = "stretch") -> dict[int, tuple[int, str, float, int]]:
    """Runs these stretches side by side; for each (exit status, what it
    wrote, seconds, megabytes at most)."""
    started = time.time()
    processes = {}
    for index in indexes:
        log = open(os.path.join(directory, f"{name}{index}.log"), "wb")
        environment = dict(os.environ, LITEBIRD_SUITE_SHARD=f"{index}/{count}")
        environment.pop("LITEBIRD_SUITE_CUTS", None)
        if cuts:
            environment["LITEBIRD_SUITE_CUTS"] = cuts
        processes[index] = (subprocess.Popen([binary], stdout=log, stderr=subprocess.STDOUT, env=environment), log)
    # Each is waited for by itself (`wait4`), for the moment it ended and
    # the most memory it held: waiting for them in order would give the
    # first one the time of the slowest.
    ended: dict[int, tuple[int, float, int]] = {}
    while len(ended) < len(processes):
        for index, (process, _) in processes.items():
            if index in ended:
                continue
            pid, raw, usage = os.wait4(process.pid, os.WNOHANG)
            if pid == 0:
                continue
            status = os.waitstatus_to_exitcode(raw)
            process.returncode = status   # so that Popen does not wait for it again
            ended[index] = (status, time.time() - started, usage.ru_maxrss // 1024)   # Linux counts in kilobytes
        if len(ended) < len(processes):
            time.sleep(0.1)
    outcome = {}
    for index, (_, log) in processes.items():
        log.close()
        with open(log.name, "rb") as written:
            outcome[index] = (ended[index][0], written.read().decode("utf-8", "replace"), ended[index][1], ended[index][2])
    return outcome


def run(binary: str, count: int, cuts: str = "", late: frozenset[int] = frozenset(), only: str = "") -> int:
    started = time.time()
    outcome: dict[int, tuple[int, str, float, int]] = {}
    again: list[str] = []
    phases = phases_of(count, late, only)
    chosen = sorted(index for phase in phases for index in phase)
    with tempfile.TemporaryDirectory(prefix="litebird-stretches-") as directory:
        for phase in phases:
            outcome.update(side_by_side(binary, count, cuts, phase, directory))
        # What failed beside the others and was no sanitizer's finding: once
        # more, one at a time, with the machine to itself.
        for index in chosen:
            status, output, _, _ = outcome[index]
            if len(chosen) > 1 and may_be_run_again(status, output):
                second = side_by_side(binary, count, cuts, [index], directory, name="again")[index]
                if second[0] == 0:
                    again.append(f"stretch {index} failed beside the others (exit status {status}; it ended with: "
                                 f"{last_words(output)}) and passed when it was run again by itself, in {second[2]:.0f} s")
                    outcome[index] = second
    results = [(outcome[index][0], outcome[index][1]) for index in chosen]
    seconds = [outcome[index][2] for index in chosen]
    megabytes = [outcome[index][3] for index in chosen]
    # What passed first, what failed last: the end of the output is the failure.
    order = sorted(range(len(chosen)), key=lambda place: (results[place][0] != 0, place))
    for place in order:
        sys.stdout.write(results[place][1])
        if not results[place][1].endswith("\n"):
            sys.stdout.write("\n")
    passed, line = verdict(results, chosen, count)
    whole = time.time() - started
    times = measured(results, seconds, megabytes, whole, late, chosen) + f". The machine: {machine()}"
    if len(chosen) < count:
        times = f"{'stretch' if len(chosen) == 1 else 'stretches'} {numbers(chosen)} of {count}: " + times
    print(f"\nThe unit suites as {times}")
    for told in again:
        print(f"\nNOTE: {told}. A suite that measures time can fail beside busy processes; if this was not one, "
              f"it is a suite that does not always pass, and that is a finding.")
    if os.environ.get("GITHUB_ACTIONS") == "true":
        # Notices on the job: read through the API where the log cannot be.
        print(f"::notice title=the unit suites as {len(chosen)} processes::{times}")
        for told in again:
            print(f"::warning title=a stretch of the unit suites passed only by itself::{told}")
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
        ("the late stretches are a phase of their own, after the others", phases_of(4, frozenset({1, 2})), [[0, 3], [1, 2]]),
        ("early is the ones that are not late, and nothing after them", phases_of(4, frozenset({1, 2}), "early"), [[0, 3]]),
        ("late is the late ones, at once", phases_of(4, frozenset({1, 2}), "late"), [[1, 2]]),
        ("early and late between them are every stretch once",
         sorted(index for only in ("early", "late") for phase in phases_of(6, frozenset({2, 3}), only) for index in phase),
         [0, 1, 2, 3, 4, 5]),
        ("the fourth argument is early or late", (only_of("early", frozenset({1})), only_of("late", frozenset({1}))),
         ("early", "late")),
        ("...and nothing else, and nothing at all when no stretch is late",
         (only_of("both", frozenset({1})), only_of("", frozenset({1})), only_of("late", frozenset())), (None, None, None)),
        ("some of the stretches pass when each passed, and say what they leave to the other call",
         verdict([three[0], three[2]], [0, 2], 3),
         (True, "Stretches 0 and 2 of 3 passed: 280 of the 420 engine test suites. "
                "The other 140 are the other stretches', which another call runs")),
        ("one of some stretches, by its own number", verdict([three[1]], [1], 3),
         (True, "Stretch 1 of 3 passed: 140 of the 420 engine test suites. "
                "The other 280 are the other stretches', which another call runs")),
        ("...a stretch that answers for another number fails", verdict([three[0]], [1], 3),
         (False, "stretch 1 of 3 says it is stretch 0 of 3")),
        ("...and one that failed fails, under its own number", verdict([three[0], (134, "Assertion failed\n")], [0, 2], 3),
         (False, "stretch 2 of 3 failed (exit status 134)")),
        ("some stretches that ran every suite left nothing for the others: the cuts are wrong",
         verdict([(0, said(0, 3, 210, 420)), (0, said(2, 3, 210, 420))], [0, 2], 3),
         (False, "stretches 0 and 2 of 3 ran 420 suites between them, and there are 420: nothing is left for the other stretches")),
        ("the line of some stretches names them by their own numbers",
         measured([three[0], three[2]], [10.0, 30.0], [1, 3], 30.0, frozenset({1}), [0, 2]),
         "2 processes side by side, 30 s in all (the slowest is stretch 2); "
         "stretch 0: 140 suites in 10 s, 1 MB at most; stretch 2: 140 suites in 30 s, 3 MB at most"),
        ("late stretches run by themselves did not wait for anything",
         measured([three[1]], [30.0], [2], 30.0, frozenset({1}), [1]),
         "1 processes side by side, 30 s in all (the slowest is stretch 1); stretch 1: 140 suites in 30 s, 2 MB at most"),
        ("with none late there is one phase", phases_of(3, frozenset()), [[0, 1, 2]]),
        ("late stretches are numbers of stretches there are", late_of("1,2", 4), frozenset({1, 2})),
        ("no late stretch is no late stretch", late_of("", 4), frozenset()),
        ("a stretch there is not cannot be late", late_of("4", 4), None),
        ("they cannot all be late", late_of("0,1", 2), None),
        ("nor can a name be", late_of("deep_one", 4), None),
        ("the line says which stretches ran after the others",
         measured(three, [10.0, 30.0, 12.0], [1, 2, 3], 42.0, frozenset({1})).split("; ")[2],
         "stretch 1 (after the others): 140 suites in 30 s, 2 MB at most"),
        ("an assertion is asked again by itself", may_be_run_again(-6, "Running test: b... Assertion `took < 5' failed.\n"), True),
        ("a sanitizer's report is not", may_be_run_again(1, "==1==ERROR: AddressSanitizer: heap-use-after-free\n"), False),
        ("nor undefined behaviour", may_be_run_again(1, "a.cpp:3:5: runtime error: signed integer overflow\n"), False),
        ("a stretch that passed is not run again", may_be_run_again(0, said(0, 3, 140, 420)), False),
        ("the last words of a stretch, without colours",
         last_words("Running test: a... \x1b[32mPASSED\x1b[0m\nRunning test: b... x.cpp:9: Assertion `took < 5' failed.\n\n"),
         "Running test: b... x.cpp:9: Assertion `took < 5' failed."),
        ("where the middle of a stretch's time is, when the binary says it",
         measured([(0, "\nHalf of the time of these suites was over when wide_two began.\n" + said(0, 1, 420, 420))],
                  [50.0], [900], 50.0),
         "1 processes side by side, 50 s in all (the slowest is stretch 0); "
         "stretch 0: 420 suites in 50 s, 900 MB at most, half of it over when wide_two began"),
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
    stretches = stretches_of(arguments[1]) if len(arguments) in (2, 3, 4) else None
    late = late_of(arguments[2], stretches[0]) if stretches is not None and len(arguments) >= 3 else frozenset()
    only = only_of(arguments[3], late) if late is not None and len(arguments) == 4 else ""
    if stretches is None or late is None or only is None:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    return run(arguments[0], stretches[0], stretches[1], late, only)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
