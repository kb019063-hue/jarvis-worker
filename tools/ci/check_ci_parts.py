#!/usr/bin/env python3
"""Every part of the sanitizer run is run by one job of CI.

    python3 tools/ci/check_ci_parts.py              # the Makefile and the workflow of this tree
    python3 tools/ci/check_ci_parts.py --self-test  # the rules, on fixed texts

`make asan-core` used to be one recipe that CI ran as one step, 19 to 24
minutes of a run of 24 to 31. It is now a recipe that runs parts
(`$(MAKE) asan-build`, `$(MAKE) asan-unit`, ...), and CI gives the parts to
jobs that run side by side (.github/workflows/ci.yml, "Sanitizers: ...").

When it was one step, a check added to the recipe was run by CI because it
was there. Now there are two places, and a part that is in the Makefile and
in no job fails nothing: the run is green and the check is not made. This is
the step that fails instead. It reads two files and takes no time.

It fails when
  - the recipe of `asan-core` has a line that is not `$(MAKE) asan-NAME`: a
    check written there is run by `make asan` and by no job;
  - a part the recipe names is not a target of the Makefile;
  - a part, `asan-deep` or `tsan` is run by no job, or by two (`asan-build`
    is the exception: every job that runs a part makes the build first);
  - a job runs a part before it has run `asan-build`;
  - a job runs the whole of `asan-core` or `asan`, which would run every part
    a second time;
  - `asan-fuzz` is in a job that has neither run `asan-xml` nor unpacked
    the W3C suite (`run_xml_conformance.py --unpack-only`) before it:
    tools/fuzz/fuzz_xml.py mutates the documents of that suite, which
    are unpacked under build/xml-conformance/, and without them it
    mutates the dozen written in the script and reports no failure.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
MAKEFILE = ROOT / "Makefile"
WORKFLOW = ROOT / ".github" / "workflows" / "ci.yml"

WHOLE = "asan-core"                 # the recipe that names the parts
BUILD = "asan-build"                # the part every job needs first
ALSO = ("asan-deep", "tsan")        # what the old job ran besides asan-core
PART_LINE = re.compile(r"^\$\(MAKE\) (asan-[a-z0-9-]+)$")
JOB = re.compile(r"^  ([A-Za-z0-9_-]+):\s*$")
MAKE_CALL = re.compile(r"(?<![\w./-])make(?![\w./-])([^|;&\n]*)")
UNPACK_CALL = re.compile(r"run_xml_conformance\.py\b[^|;&\n]*--unpack-only\b")
UNPACKED = "the-xml-suite-unpacked"   # not a target: what a step that unpacks the suite counts as


def recipe_of(makefile: str, target: str) -> list[str] | None:
    """The recipe lines of a target, without their tabs; None if there is no such target."""
    lines = makefile.split("\n")
    for index, line in enumerate(lines):
        if re.match(r"^%s:(?!=)" % re.escape(target), line):
            recipe = []
            for following in lines[index + 1:]:
                if not following.startswith("\t"):
                    break
                recipe.append(following[1:].rstrip())
            return recipe
    return None


def jobs_of(workflow: str) -> dict[str, list[str]]:
    """For each job, the sanitizer targets its steps give to `make`, in order."""
    found: dict[str, list[str]] = {}
    job = None
    in_jobs = False
    for line in workflow.split("\n"):
        if line.rstrip() == "jobs:":
            in_jobs = True
            continue
        if not in_jobs or line.lstrip().startswith("#"):
            continue
        match = JOB.match(line)
        if match:
            job = match.group(1)
            found[job] = []
            continue
        if job is None:
            continue
        if UNPACK_CALL.search(line):
            found[job].append(UNPACKED)
        for call in MAKE_CALL.finditer(line):
            for word in call.group(1).split():
                if word in (WHOLE, "asan", "tsan") or word.startswith("asan-"):
                    found[job].append(word)
    return found


def problems(makefile: str, workflow: str) -> tuple[list[str], list[str]]:
    """What is wrong, and the parts the recipe names."""
    wrong: list[str] = []
    recipe = recipe_of(makefile, WHOLE)
    if recipe is None:
        return [f"the Makefile has no target `{WHOLE}`"], []
    parts: list[str] = []
    for line in recipe:
        match = PART_LINE.match(line)
        if match is None:
            wrong.append(f"`{WHOLE}` has a line that is not `$(MAKE) asan-NAME`: {line[:90]!r}. "
                         f"`make asan` would run it and no job of CI would: put it in one of the parts")
        elif match.group(1) in parts:
            wrong.append(f"`{WHOLE}` names `{match.group(1)}` twice")
        else:
            parts.append(match.group(1))
    if BUILD not in parts:
        wrong.append(f"`{WHOLE}` does not run `{BUILD}`")
    for part in parts:
        if recipe_of(makefile, part) is None:
            wrong.append(f"`{WHOLE}` runs `{part}`, which is not a target of the Makefile")

    jobs = jobs_of(workflow)
    for job, targets in jobs.items():
        for whole in (WHOLE, "asan"):
            if whole in targets:
                wrong.append(f"job `{job}` runs `make {whole}`, the whole of it: every part would be run twice")
        if "asan-fuzz" in targets and not {"asan-xml", UNPACKED} & set(targets[:targets.index("asan-fuzz")]):
            wrong.append(f"job `{job}` runs `asan-fuzz` without `asan-xml` before it, and without having unpacked "
                         f"the suite (`run_xml_conformance.py --unpack-only`): fuzz_xml.py would have "
                         f"only the documents written in it to mutate, and would not say so by failing")
        needing = [t for t in targets if t.startswith("asan-") and t not in (BUILD, WHOLE)]
        if needing and (BUILD not in targets or targets.index(BUILD) > targets.index(needing[0])):
            wrong.append(f"job `{job}` runs `{needing[0]}` before it has run `{BUILD}`")
    for required in [*parts, *ALSO]:
        where = [job for job, targets in jobs.items() if required in targets]
        if not where:
            wrong.append(f"no job of CI runs `{required}`: it is in the Makefile and would not be checked")
        elif len(where) > 1 and required != BUILD:
            wrong.append(f"`{required}` is run by {len(where)} jobs ({', '.join(where)}): once is enough")
    known = set(parts) | set(ALSO) | {WHOLE, "asan", UNPACKED}
    for job, targets in jobs.items():
        for target in targets:
            if target not in known:
                wrong.append(f"job `{job}` runs `{target}`, which `{WHOLE}` does not name")
    return wrong, parts


def self_test() -> int:
    make = ("asan-core:\n\t$(MAKE) asan-build\n\t$(MAKE) asan-xml\n\t$(MAKE) asan-fuzz\n\n"
            "asan-build:\n\tbuild\n\nasan-xml:\n\txml\n\nasan-fuzz:\n\tfuzz\n\nasan-deep:\n\tdeep\n\ntsan:\n\ttsan\n")
    flow = ("name: CI\njobs:\n  one:\n    steps:\n      - run: make tsan\n      - run: |\n"
            "          make -j\"$(nproc)\" asan-build 2>&1 | tee x.log\n          make asan-xml | tee asan-core.log\n"
            "      - run: make asan-fuzz\n  two:\n    steps:\n      # make asan-core\n"
            "      - run: make asan-build && make asan-deep\n")
    cases = [
        ("the two agree", make, flow, []),
        ("a check written into asan-core itself",
         make.replace("\t$(MAKE) asan-fuzz\n", "\t$(MAKE) asan-fuzz\n\tpython3 tools/new_check.py\n"), flow,
         ["is not `$(MAKE) asan-NAME`"]),
        ("a part no job runs",
         make.replace("\t$(MAKE) asan-fuzz\n", "\t$(MAKE) asan-fuzz\n\t$(MAKE) asan-new\n") + "\nasan-new:\n\tnew\n", flow,
         ["no job of CI runs `asan-new`"]),
        ("a part that is not a target",
         make.replace("\nasan-fuzz:\n\tfuzz\n", "\n"), flow, ["`asan-fuzz`, which is not a target"]),
        ("a part run by two jobs", make, flow.replace("make asan-deep", "make asan-deep && make asan-xml"),
         ["`asan-xml` is run by 2 jobs"]),
        ("the deep pages dropped", make, flow.replace(" && make asan-deep", ""), ["no job of CI runs `asan-deep`"]),
        ("tsan dropped", make, flow.replace("      - run: make tsan\n", ""), ["no job of CI runs `tsan`"]),
        ("fuzz moved away from the XML suite", make,
         flow.replace("      - run: make asan-fuzz\n", "").replace("make asan-deep", "make asan-deep && make asan-fuzz"),
         ["runs `asan-fuzz` without `asan-xml` before it"]),
        ("fuzz in a job of its own that unpacks the suite first", make,
         flow.replace("      - run: make asan-fuzz\n", "").replace(
             "      - run: make asan-build && make asan-deep\n",
             "      - run: make asan-build && make asan-deep\n  three:\n    steps:\n"
             "      - run: python3 tools/conformance/run_xml_conformance.py --unpack-only\n"
             "      - run: make asan-build\n      - run: make asan-fuzz\n"),
         []),
        ("fuzz in a job of its own that unpacks the suite after it", make,
         flow.replace("      - run: make asan-fuzz\n", "").replace(
             "      - run: make asan-build && make asan-deep\n",
             "      - run: make asan-build && make asan-deep\n  three:\n    steps:\n"
             "      - run: make asan-build\n      - run: make asan-fuzz\n"
             "      - run: python3 tools/conformance/run_xml_conformance.py --unpack-only\n"),
         ["runs `asan-fuzz` without `asan-xml` before it"]),
        ("the archive fetched and not unpacked is not the suite", make,
         flow.replace("      - run: make asan-fuzz\n", "").replace(
             "      - run: make asan-build && make asan-deep\n",
             "      - run: make asan-build && make asan-deep\n  three:\n    steps:\n"
             "      - run: python3 tools/conformance/run_xml_conformance.py --fetch-only\n"
             "      - run: make asan-build\n      - run: make asan-fuzz\n"),
         ["runs `asan-fuzz` without `asan-xml` before it"]),
        ("fuzz before the XML suite", make,
         flow.replace("          make asan-xml | tee asan-core.log\n      - run: make asan-fuzz\n",
                      "          make asan-fuzz\n      - run: make asan-xml | tee asan-core.log\n"),
         ["runs `asan-fuzz` without `asan-xml` before it"]),
        ("a part before the build", make, flow.replace("make asan-build && make asan-deep", "make asan-deep && make asan-build"),
         ["job `two` runs `asan-deep` before it has run `asan-build`"]),
        ("a job without the build", make, flow.replace("make asan-build && make asan-deep", "make asan-deep"),
         ["job `two` runs `asan-deep` before it has run `asan-build`"]),
        ("the whole of it as well", make, flow.replace("      # make asan-core\n", "      - run: make asan-core\n"),
         ["job `two` runs `make asan-core`, the whole of it"]),
        ("a part the recipe does not name", make, flow.replace("make asan-deep", "make asan-deep asan-gone"),
         ["runs `asan-gone`, which `asan-core` does not name"]),
        ("no such recipe", make.replace("asan-core:", "asan-all:"), flow, ["has no target `asan-core`"]),
    ]
    failed = 0
    for name, makefile, workflow, expected in cases:
        wrong, _ = problems(makefile, workflow)
        good = (not wrong) if not expected else all(any(e in w for w in wrong) for e in expected) and len(wrong) == len(expected)
        if not good:
            failed += 1
            print(f"FAIL {name}: expected {expected}, got {wrong}")
    # The log name `asan-core.log` and a comment are not a call of make.
    if jobs_of(flow) != {"one": ["tsan", "asan-build", "asan-xml", "asan-fuzz"], "two": ["asan-build", "asan-deep"]}:
        failed += 1
        print(f"FAIL what a job runs: {jobs_of(flow)}")
    print(f"check_ci_parts self-test: {len(cases) + 1 - failed} of {len(cases) + 1} pass")
    return 1 if failed else 0


def main() -> int:
    if "--self-test" in sys.argv[1:]:
        return self_test()
    makefile = MAKEFILE.read_text(encoding="utf-8")
    workflow = WORKFLOW.read_text(encoding="utf-8")
    wrong, parts = problems(makefile, workflow)
    for line in wrong:
        print(f"error: {line}")
    if wrong:
        print(f"\ncheck_ci_parts: {len(wrong)} problem(s). Makefile (`{WHOLE}` and its parts) and "
              f".github/workflows/ci.yml (the jobs \"Sanitizers: ...\") have to agree; "
              f"python3 tools/ci/check_ci_parts.py says how they do not.")
        return 1
    for job, targets in jobs_of(workflow).items():
        if targets:
            print(f"  {job}: {' '.join(targets)}")
    print(f"check_ci_parts: the {len(parts)} parts of `{WHOLE}`, `asan-deep` and `tsan` are each run by one job")
    return 0


if __name__ == "__main__":
    sys.exit(main())
