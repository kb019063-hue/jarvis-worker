#!/usr/bin/env python3
"""Run the unit suites as several processes side by side.

    python3 tools/ci/run_stretches.py BINARY N     # N stretches of BINARY's suites
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

A stretch that fails is printed last, so that the end of the output is the
failure, which is where a person and tools/ci/annotate_failure.py look.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
import time

SENTENCE = re.compile(r"Stretch (\d+) of (\d+): its (\d+) of the (\d+) engine test suites passed\.")


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


def run(binary: str, count: int) -> int:
    started = time.time()
    with tempfile.TemporaryDirectory(prefix="litebird-stretches-") as directory:
        processes = []
        for index in range(count):
            log = open(os.path.join(directory, f"stretch{index}.log"), "wb")
            environment = dict(os.environ, LITEBIRD_SUITE_SHARD=f"{index}/{count}")
            processes.append((subprocess.Popen([binary], stdout=log, stderr=subprocess.STDOUT, env=environment), log))
        results: list[tuple[int, str]] = []
        for process, log in processes:
            status = process.wait()
            log.close()
            with open(log.name, "rb") as written:
                results.append((status, written.read().decode("utf-8", "replace")))
    # What passed first, what failed last: the end of the output is the failure.
    order = sorted(range(count), key=lambda index: (results[index][0] != 0, index))
    for index in order:
        sys.stdout.write(results[index][1])
        if not results[index][1].endswith("\n"):
            sys.stdout.write("\n")
    passed, line = verdict(results)
    colour = "\033[1;32m" if passed else "\033[1;31mFAILED\033[0m: "
    print(f"\n{colour}{line}{'' if not passed else chr(27) + '[0m'} ({time.time() - started:.0f} s)\n", flush=True)
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
    if len(arguments) != 2 or not arguments[1].isdigit() or int(arguments[1]) < 1:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    return run(arguments[0], int(arguments[1]))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
