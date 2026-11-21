# Working in this repository

Litebird is written by AI agents, at times more than one at once. Each agent
has a branch of its own. No agent is told what another one is doing: an agent
knows what `main` holds and what the open pull requests say, at the moment it
looks, and nothing else. Whatever cannot be said that way goes through the
person the agents work for. This file is what the agents have to agree on so
that two pieces of work do not collide.

## Notice — read this first (2026-11-17, rewritten the same day)

**New phase write-ups and findings no longer go in `ARCHITECTURE.md` and
`AUDIT.md`.** They go in `docs/phases/NN-short-title.md` and
`docs/findings/NN-short-title.md`; `docs/README.md` says what goes in one, and
why files rather than sections.

**The move has happened.** `ARCHITECTURE.md` was 435 KB and is now 71 KB: 46
phase write-ups left it for `docs/phases/`. `AUDIT.md` was 296 KB and is now
38 KB: 56 resolved findings left it for `docs/findings/`. Nothing was rewritten
on the way — each file is its section, in the same order, and every heading the
two documents had is still in the repository. An agent claiming a task used to
read 731 KB of markdown to find two integers, and two agents that looked at
the same moment still took the same one, which is what a number read out of a
document cannot prevent. So:

- **The two documents are frozen as append targets.** What is still in them
  stays exactly as it is: do not add to it, reorder it, re-wrap it or delete it
  in passing. Everything in them is in the repository's history besides.
- **The two numbers come from `python3 tools/next_ids.py`**, not from reading
  either document. It reads the documents, `docs/`, `docs/state.json` and the
  open pull requests; `--pr-body` prints the block a pull request opens with,
  `--sources` says where each number came from, and `--taken` lists every
  number taken rather than only the next free one. It reserves nothing — a
  number is still taken by opening the pull request — but a collision is then
  visible in `gh pr list` instead of hidden in two documents.
- **It is the only allocator.** `tools/ci/check_pr.py` asks it
  (`--taken --ref main --no-prs`) instead of parsing the two documents itself,
  which is what it did until the write-ups left them — after which it went on
  answering "nothing is taken", and would have handed out numbers that were
  somebody else's. One allocator cannot disagree with itself.
- **CI enforces both rules** (`python3 tools/ci/check_docs.py`): a new
  `## Phase NN` heading in `ARCHITECTURE.md`, or a new `## NN.` finding in
  `AUDIT.md`, fails the build, and so does either file growing past its budget.
  The budgets came **down** with the files, to 120 KB and 80 KB: a floor in CI
  is not lowered to let a pull request pass.

A change like this one — documents, tooling, CI, nothing that moves a
scoreboard — is not a phase and takes no phase or finding number.

## Before starting

1. **An open pull request is a claim.** Run `gh pr list --state open` and read
   the first lines of each: they name the task and the files it changes. (A
   pull request nobody has pushed to for days is not a claim; ask in a comment
   on it before taking its task.)
2. **Claim before doing the work.** Pick a row of the table below that nobody
   has, push a first commit (an empty one will do) and open a pull request
   whose title begins with `WIP:` and whose text begins:

   ```
   Task: <the row of the table>
   Files: <what it will change>
   Phase P, finding F
   ```

   `P` and `F` come from `python3 tools/next_ids.py`, which reads the
   documents, `docs/`, `docs/state.json` and the open pull requests and prints
   the next free number of each; `--pr-body` prints the three lines above with
   them filled in. It asks GitHub for `main` as well, so a tree that is hours
   old gets the same answer as a new one. (They used to be read out of
   ARCHITECTURE.md and AUDIT.md — see the notice at the top.) They are taken
   now so that two pull requests do not both call themselves Phase 27. A change that is not engine work —
   documents, tooling, CI — takes neither: write `No phase, no finding` and
   say why instead.
3. **One task at a time, inside its files.** If the work turns out to need a
   file that another open pull request names, stop before changing it and
   tell the person you work for: the other agent will not see a comment
   unless it happens to look. A bug found outside the task becomes a new row
   of the table; it is not fixed in passing.
4. **Look again once the pull request is open.** Two agents that look at the
   same moment both see the same number free, and the same file, and each of
   them did look first. `python3 tools/ci/check_pr.py claim N` lists what #N
   has in common with the other open pull requests. Of two that have the
   same phase, finding or file, **the one opened later moves**: it takes the
   next free numbers, or stays out of the file, and the earlier one changes
   nothing. A pull request holds the files it has changed as well as the
   ones it names. Put `[skip ci]` in the message of the first commit: an
   empty commit needs no run, and a run is an hour of runner time.

## The files every pull request changes

`README.md`, `ARCHITECTURE.md`, `AUDIT.md`, `tests/test_suite.cpp` (one file,
with the suites registered in a list at its end), `Makefile`,
`.github/workflows/ci.yml` and the table below. Two pull requests written at
the same time will both have changed them. So:

1. Add a block of your own: a section, a group of tests. Do not re-wrap or
   reorder what is there.
2. **Before merging, merge `main` into your branch.** Resolve, run
   `make metrics` (below) rather than editing the counts by hand, run the
   checks again, push, and let CI pass on that commit. CI that passed on a
   branch behind `main` says nothing about the merge.

### The numbers are generated, not copied

The counts and scores a document publishes are not typed into it by hand any
more, and the merge step above is one command:

```bash
make metrics     # python3 tools/ci/check_numbers.py --write
```

Every live figure in `README.md` and `ROADMAP.md` carries an invisible marker
naming it — `**1137 / 1156 (98.4%)**<!--metrics:js/frac_pct-->` — and `make
metrics` rewrites the text in front of each marker from `docs/metrics.json` and
from the tree. Counted things are derived where they are checked: the suite
count is the `RUN_TEST` list of `tests/test_suite.cpp`, the corpus totals are
the cases on disk. Measured things are in `docs/metrics.json` with the command
that produced them and the day it did, so a figure can be argued with instead
of trusted. `make check-numbers` fails when any marked figure disagrees, and
`--write` never lowers a floor — neither the `--min-pass` ones of `ci.yml` nor
`URL_FLOOR` and `WPT_FLOOR` in the Makefile, which it also maintains: a floor
that would go down is a regression, and it is reported instead.

A number without a marker is history — a phase write-up quoting the figure of
its own day — and history is left alone. `ARCHITECTURE.md` and `AUDIT.md` are
frozen append targets, so their rows are not marked either.

Three numbers are still a person's to correct, and `docs/metrics.json` says
why: `XML_FLOOR`, whose corpus is downloaded on first use, and the two ASan
floors, which are the same scores under sanitizers and are written by `make
asan`.

## What "done" means

- The checks: `make test`, `make conformance`, `make js-conformance`,
  `make dom-conformance`, `make url-conformance`, `make xml-conformance`,
  `make wpt`, `make diff-trees`, `make diff-xml`,
  `make fuzz`, `make deep`, `make asan`. The last two take
  about eight minutes each. CI runs all of them, and one CI run is about 16
  minutes and an hour of runner time: run the checks here first and push
  when they pass, not to find out whether they do. `make xml-conformance` downloads the
  W3C suite the first time (`tests/xml/README.md`), and `make diff-xml`
  needs `pip install lxml`.
- A unit suite that passes in the normal build can fail in the sanitizer
  build, and CI's log cannot be read from every sandbox: run `make asan`
  here first. When one of CI's four sanitizer jobs fails all the same, its
  last step says what failed in the job's annotations, which the API itself serves:
  `gh run view RUN --json jobs` for the job's id, then
  `gh api repos/OWNER/REPO/check-runs/JOB_ID/annotations --jq '.[].message'`.
  (`tools/ci/annotate_failure.py`; any job that keeps its output with
  `tee` can end with the same step.) The name of the step that failed is a
  part of `make asan-core`, and `make asan-build` and then that part
  (`make asan-unit`, say; `make help` lists the nine) runs it here without
  the rest.
- A floor in CI is not lowered to let a pull request pass.
  `tests/js-conformance/everyday-*.dat` and
  `tests/dom-conformance/everyday.dat` are not edited. A DOM case that
  fails is not "fixed" by recording another answer: its expectation is
  what jsdom prints, and where jsdom is wrong the case is taken out and
  the standard's answer goes into a unit test
  (`tests/dom-conformance/README.md`).
- A full score is where work on the DOM starts, not where it ends. The
  corpus knows the questions its author thought of. Before and after a
  change, run pages that are not in it through
  `tools/conformance/compare_with_jsdom.py`, and turn what that finds into
  cases.
- The three documents are updated: what was wrong, what changed, how it was
  verified (a command anyone can run, and the number it prints), and what is
  still open. AUDIT.md says what is not fixed as plainly as what is.
  (`docs/README.md` says where a write-up goes now, and what one says.)
- The pull request takes its row out of the table below, or rewrites the row
  to what is left.
- The `WIP:` comes off the title. Merged into `main` with a merge commit
  (`gh pr merge N --merge`) once CI is green, and CI on `main` checked
  afterwards, in the same turn: once you have merged and replied, GitHub is
  out of reach until the person presses Continue ("The turn, and what a
  merge does to it", below).
- `python3 tools/ci/check_pr.py merge N` says "ready" right before the merge.
  It asks what is easy to believe and was wrong more than once: that `main`
  is in the head commit, that CI passed on that commit, that the numbers are
  still free.
- **When `main` has moved, ask it before you merge `main` again.** A new CI
  run is needed only when both sides changed something other than documents
  and the checks that read them. When `main` moved by a line of README.md,
  or your pull request is documents alone, it runs those checks itself on
  the tree that would be merged, in seconds, and says "ready" ("A line of
  README.md cost 28 minutes", below).

## What has gone wrong, and what to do about it

Everything here happened between 15 and 21 November 2026, to agents that had
read this file. Most of it is a rule from above that was not looked up at the
moment it applied, so `tools/ci/check_pr.py` looks it up (it reads GitHub and
changes nothing there):

```bash
python3 tools/ci/check_pr.py            # what each open pull request claims; the numbers taken; the next free pair
python3 tools/ci/check_pr.py claim N    # right after opening #N
python3 tools/ci/check_pr.py merge N    # right before `gh pr merge N --merge`
```

**Claims**

- **The same number twice.** #68 and #69 were opened 72 seconds apart and both
  were Phase 55, finding 61: neither could have seen the other. That is what
  step 4 above is for. Four more took a number that an open pull request
  already had: #63 took 52 and 58 half an hour after #62 had them, #66 took
  53 and 59 two hours after #64, #71 took 54 (which #67 held), then 55, then
  56, and #68 took 59 and 65 twenty-two minutes after #74 had them. Count the
  open pull requests, not `main` alone. And count the `## Phase N` headings
  of ARCHITECTURE.md, not its phase table alone: eight phases (36, 42, 45,
  47, 51, 52, 54, 56) had a section and no row until Phase 63 added the rows.
- **Whoever merges first keeps the number, whoever claimed it first.** #70
  claimed Phase 56 and finding 62 at 01:11 on 17 November. #71 put 56 in its
  title at 02:40, was told in a comment at 02:43, and was merged with 56 and
  62 at 03:02. Once a number is on `main` there is nothing left to settle:
  #70 became Phase 63, finding 69, two hours of documents later. So run
  `check_pr.py merge N` before you merge (it refuses a number that an
  earlier open pull request holds), and write so that a renumbering is one
  `sed`: always the words `Phase N` and `finding F`, which stand in six
  files (`ARCHITECTURE.md`, `AUDIT.md`, `README.md`, `AGENTS.md`,
  `tests/test_suite.cpp`, `tests/js-conformance/README.md`). Renumber on
  your branch *before* you merge `main`, while every `Phase N` in the tree
  is still yours.
- **A number that was told it was taken, and was not.** A floor of
  `docs/state.json` says that no number up to it is handed out again; it
  does not say that a write-up of each is on `main`. From #80 (17 November,
  20:18) `check_pr.py` asked `tools/next_ids.py` what is taken and reported
  the answer as "on main already": to #73 for Phase 58 and finding 64 and to
  #63 for Phase 62 and finding 68, both open since before the floors passed
  them, with numbers that have no write-up. A pull request can raise the
  floors to its own numbers when it merges (#75 and #83 did), so any open
  one that a later one overtakes would have been told the same.
  `check_pr.py` now stops a pull request only for a number that has a
  write-up on `main`: a file `docs/phases/NN-…` or `docs/findings/NN-…`, or
  a place in one of the two frozen documents. For one at or below a floor
  it remarks. What a document says *about* a number is not a write-up of
  it: this paragraph names four, and read as one it would have told #63 and
  #73 the same thing again.
- **`main`, as the clone remembers it.** From #80 the same question was put
  to the clone's own branch `main`, which is where the clone was made and
  which `git fetch` of `origin` does not move. Twenty minutes after #83 had
  put Phase 68 and finding 74 on `main`, a clone made before it still
  answered with floors of 66 and 72 and no Phase 68; a clone with no branch
  `main` answered that no number was taken, without an error. That is the
  answer from before a number landed, to the question whether it has
  landed. `check_pr.py` now asks GitHub which commit `main` is, fetches it
  when the clone does not have it, and reads that. `tools/next_ids.py`
  itself had the other half of the error: it read the tree it was run in
  and the open pull requests, and a number merged since that tree was made
  is in neither. Asked from the tree a session had begun with, it handed out
  Phase 74 and finding 80 ten minutes after #90 had merged them. It asks
  GitHub for `main` now, too.
- **The work first, the claim afterwards.** #97 and #98 were opened on 18
  November (07:09 and 07:14) with the work already in them. #98 came as
  Phase 76 and finding 82, which #94 had claimed 16 minutes before, and was
  renumbered four minutes later. #97 changes five files that #86 had held
  for four hours, so one of the two has to be merged through the other. The
  claim comes before the work so that the other agents can stay out of the
  way while it is done; a claim made when it is finished protects nothing.
- **A claim nobody can read.** #65 and #71 had no `Task:`, `Files:` and
  `Phase P, finding F` lines. So nothing said that #71 changed
  `src/js/JsEval.cpp`, which #68 named, and `src/js/JsOps.cpp`, which #70
  named. Without those lines the other agents work as if the pull request
  were not there.
- **A claim nobody holds.** #66 had the task #67 has since merged, and stood
  open for almost 28 hours, until another agent was asked to close it. It
  was opened from #62's branch 75 seconds after #62 was merged, late on 16
  November; #67 came 52 minutes later from another branch, with the same
  task and much of the same text. #81 and #82, on the evening of the 17th,
  are the same pair (#81 from #80's branch, two minutes after #80 was
  merged), and the comment that closed #81 says what happened: "#81's branch
  cannot be pushed to from here". After a merge the next message puts a
  session on a new branch, and a claim opened from the old one in between is
  left behind. That comment is also what to do: #82 was opened and #81
  closed nine seconds apart.
  #63 stood for almost seven hours with nothing but the commit
  it was opened with, naming three files that five other rows of the table
  needed, before work on it began. An open pull request is a claim for as
  long as it is open: close the one you give up, say in it when it has to
  wait, and when your session moves to a new branch, close what the old
  branch left open.
- **A finished pull request whose session has gone quiet.** #63 was
  finished on 17 November at 05:36 and could not get a CI run; nothing was
  pushed to it for 26 hours. #73's CI passed on 18 November at 07:08, five
  minutes after `main` had moved again, and nothing was pushed to it after
  that; it had merged `main` thirteen times. #87's CI passed five times
  that morning, and each time `main` had moved before the run ended.
  Another agent can land such a pull request, if the person we work for has
  said so: merge its head into your own branch together with `main`
  (`git fetch origin pull/N/head`, then `git merge FETCH_HEAD`), resolve,
  run the checks, and merge your pull request. GitHub then shows #N as
  merged, with its commits. The three were landed that way in #100, in one
  branch and with one CI run, and #94 with them, whose agent was at work,
  read the note and handed it over; #98 in the next. It is the only way
  known to land several engine changes for the price of one run. Say in #N
  what you did and what you resolved by hand, before and after.
  On 19 November #118 took four more (#105, #111, #112, #116): the CI of
  each had passed, on 18 November between 18:42 and 20:48, on a head `main`
  had left behind while the run was going, and nothing was pushed to any
  of them for six hours and more. #111's agent came back within minutes
  of the note and merged `main` itself. #118 took that new head in place
  of the old one, so that whichever of the two was merged first, the other
  had nothing left to run: if the agent of a pull request you carry is at
  work, carry what it pushes and leave the merging of its own to it.
- **A merge that compiles is not a merge that is right.** #97 added a
  member to `ComputedStyle`, and #86, which it was written without, had
  made every box share the style of any box whose style compares equal,
  with a comparison that listed the members by hand. Merged, the two
  compiled, and the text inside an `<em>` took the upright style of the
  text beside it. git showed 11 conflicts, all of them names, and not
  this. #97's own suite showed it, which is what a suite is for; the
  comparison is written by the compiler now. When you carry a change
  across another, read what the other one made every member of a type
  take part in.

**`main`**

- **Two pull requests that are each green can break `main` together.** #58
  and #60 were merged three minutes apart (20:42 and 20:45 on 16 November).
  Each had passed CI against a `main` without the other; one called a
  function the other deleted, and `main` did not compile for 43 minutes.
  Three agents then wrote the same one-line repair (#59, #62, #63). So:
  `git fetch` and merge `main` *last*, build and run the checks on that tree,
  push, let CI pass on that commit, and merge at once. If `main` moves in
  between, do it again; `check_pr.py merge N` says whether it has.
- **Merged with `WIP:` in the title, on a head without `main`.** #91 was
  merged on 18 November at 06:06, 19 seconds after its CI passed. Its head
  did not have #92, merged 25 minutes before, which had made
  `make check-numbers` the first step of CI; `tests/test_suite.cpp`
  registered 219 suites and README.md said 218. `main` failed for 25
  minutes, until #93, and every agent that merged `main` in between merged a
  tree that fails CI. `check_pr.py merge 91` asks about the title and about
  `main`, and would have said "not ready" twice. #76, #77 and #84 were
  merged behind `main` as well, and were lucky.
- **A line of README.md cost 28 minutes.** Until 18 November every commit
  on `main` sent every finished pull request back for a whole CI run. That
  morning `main` moved eight times in three hours and twenty minutes, with
  up to eleven pull requests open: #86 merged `main` six times after its
  work was done, #73 thirteen times in all. That is why agents stop asking
  whether `main` has moved. But nothing in CI reads a document except four
  quick steps (`make check-numbers`, and the three of "Documentation
  shape"), so `check_pr.py merge N` now runs those on the tree that would be
  merged, and asks for a new CI run only when both sides changed something
  else. A document is `*.md` at the top of the tree, anything under `docs/`
  and the `README.md` of a test directory; the checks that read them are
  `tools/ci/` and `tools/next_ids.py`. Not every `.md`: `run_wpt.py` fails
  for a file under `tests/wpt/` that its manifest does not list. Two engine
  changes still need a run together; nothing is known to make that cheaper
  but merging them in one branch and running CI once. And the plainest case
  needs no run: when merging would put on `main`, file for file, the tree
  of a commit CI passed on, `check_pr.py merge N` says so and runs the
  quick checks against the new `main` (#118; `main` had moved by commits
  the branch already had).
- **Watch CI on `main` after your merge until it ends.** The sanitizer job
  failed on `main` at two merges that evening and other pull requests were
  merged on top meanwhile. A red `main` is the next task of whoever merged
  last, before anything else, and the person we work for should hear of it.
  Look for a repair that exists (`gh pr list`, `git log origin/main`) before
  writing one.
- **A page that "reported nothing" in the sanitizer job was the engine's
  real-time limit, not chance.** The job failed that way on `main` at
  `73e23a2` and on #73's first commit: the engine stops a page's callbacks
  after 10 seconds of real time, in silence and with exit status 0, and the
  heaviest web-platform-test page spends 7 of them on its load handlers in
  the sanitizer build on an idle machine. Since Phase 63 `run_wpt.py`
  passes its own timeout as that limit (`--wall-limit`), and the engine
  says on standard error when the limit ended a run. The other runners
  still use the default: when a case of theirs ends with no output and exit
  status 0, look for that line. Read a failed job's annotations before
  anything else ("What done means" has the command), and when a job failed
  for a reason outside the tree, `gh run rerun RUN --failed` runs it again
  without the other six.
- **A push starts no CI run while the pull request conflicts with `main`.**
  It looks like a run that has not started yet. `gh pr view N --json
  mergeable` after `git fetch`, before spending a commit on CI.
- **A run is eleven jobs, about 16 minutes, and an hour of runner time.**
  `[skip ci]` goes on every
  commit that is not meant to be tested: the claim, work in progress, a
  change to the documents alone. The commit that is merged has a green run,
  or differs from one that has only in documents, and the pull request says
  so; a pull request of documents alone needs no run at all
  (`check_pr.py merge N` says which it is).
- **A run was as long as its one sanitizer job, and that job is now four.**
  Until 20 November `make tsan`, `make asan-core` and `make asan-deep` were
  three steps of one job: 21 to 28 minutes, while every other job was done
  in six. Everything after a push waited for it: the merge, the next
  agent's merge of `main`, the look at `main` afterwards. The same checks
  are now four jobs side by side. A run on the same engine code went from
  31.3 minutes (run 44664786397 on `main`, 03:00 that day) to 15.6 (run
  44672093418 on this change, an hour later); the two runs on `main` the
  day before had taken 24.4 and 26.7. Nothing was taken out and no count,
  floor or timeout was changed. What it costs is runner time, 46 minutes
  of it before and 65 after: each of the four jobs makes the sanitizer
  build for itself.

  | job (`Sanitizers: …`) | what it runs, after `asan-build` (4:20 to 5:39) | the job |
  | :--- | :--- | :--- |
  | unit suites | `asan-unit` 5:52 | 11.6 min |
  | XML suite and fuzzing | `asan-xml` 1:29, then `asan-fuzz` 3:06 | 9.2 min |
  | web-platform-tests, deep and oversized pages | `asan-wpt` 3:18, `asan-deep` 3:01 | 11.8 min |
  | script corpora, verifiers, generated XML, string stress tests | `tsan` 1:05 (before the build), `asan-js` 2:28, `asan-dom` 0:42, `asan-verify` 0:23, `asan-xml-generated` 0:51 | 11.1 min |

  They start when "Build & unit tests" has passed (2.9 to 3.8 minutes), so
  a run cannot be shorter than that job, the sanitizer build and the unit
  suites in it: about 15 minutes. Three rules keep it the same checks:
  - `make asan-core` is nine parts in the Makefile and runs them in the
    order it always had; **a new check goes into the part it belongs
    with**, and then both `make asan` and CI run it.
  - **A part that no job runs fails the run** before anything is built:
    `tools/ci/check_ci_parts.py`, a step of the first job, reads the recipe
    of `asan-core` and the workflow. It also fails for a check written into
    `asan-core` itself, for a part two jobs run, and for a job that runs a
    part before `asan-build`. With one job none of this could happen; with
    four, a part that is in the Makefile and in no job would be a green run
    that did not make the check.
  - **`asan-fuzz` runs after `asan-xml`, in the same job.** The XML fuzzer
    mutates the documents of the W3C suite, which `asan-xml` unpacks under
    `build/xml-conformance/`. Without them it does not fail: it mutates the
    dozen documents written in the script, passes, and has checked far
    less. The one job ran them in this order and nothing said that it
    mattered; the check above now requires it.

**The turn, and what a merge does to it**

- **An agent works while its turn lasts, and not in between.** A turn begins
  when the person we work for writes to the agent and ends with the agent's
  reply. Nothing wakes an agent in between: not a CI run that ends, not a
  comment on its pull request, not a line of this file. #164's CI passed on
  20 November at 19:16, after the turn that had opened it had ended; nobody
  was there to merge it, `main` moved at 19:23, and it was closed the next
  day. So what has to happen is done before the reply, and the reply, which
  is all that reaches the person, says which pull request is merged, which
  is still open and why, and what they have to do before you can go on.
- **After `gh pr merge`, GitHub still answers until the turn ends.** #171
  was merged on 21 November at 03:05:20. Ten seconds later `git fetch` and
  `git push --dry-run` worked, and within the minute a comment on one pull
  request and `gh pr close` on another. A new pull request can be opened
  from the same branch too: #164 was, a minute after #162 had been merged
  from it, and #159 seventeen minutes after #146; thirteen branches have
  done it. So what a merge leaves to do is done in that turn: CI on `main`
  watched to its end, and `main` repaired if it is red ("Watch CI on
  `main`", above); the notes in the pull requests you carried; what you
  leave open closed, or handed over in the reply.
- **Once that turn has ended, the branch is `pr_merged` and GitHub is out of
  reach.** The next message came with a note from the platform: edit no
  file, commit nothing, and pushes, pull requests and every `gh` call that
  reaches GitHub "will fail and must not be attempted"; files can be read
  and questions answered. (21 November, 03:40, the first message after
  #171. Nothing was tried, as the note says; the checkout was the branch's
  base commit.) It stays that way until the person presses Continue beside
  the pull request's status, or starts a new session where there is no
  such button, so that is the first thing to tell a person who writes to
  such a branch. And an agent that has merged and stopped cannot be reached
  through GitHub by anyone: a note on its pull request waits until it is
  continued, and what it should do next has to be said to it by the person.
- **Continue is a new branch, cut from `main` as it is then.** It has
  another name (`arena/` and another ID), and the old one is no longer
  yours to push to. One session has had eleven names since 15 November, so
  an ID is not an agent: two pull requests with different IDs can be one
  agent's, and GitHub does not say. A pull request still open on the old
  branch cannot be pushed to any more. It can be merged as it is, when
  `check_pr.py merge N` says "ready"; or closed; or carried, its head
  merged into the new branch and landed from there ("A finished pull
  request whose session has gone quiet", above). #134 was opened a minute
  after #130 had been merged from the same branch, the turn ended, and it
  was carried in #141; #164 was closed. So a pull request opened after a
  merge is merged in the same turn, or the reply says that it is open and
  what should become of it.
- **Whether another task follows the merge is for the person to say.**
  Asked for one task, an agent does what the merge leaves to do, and
  replies. Asked for more, the next one begins before the reply, because
  afterwards nothing can begin it: `git merge --ff-only origin/main`, a
  claim as in "Before starting", and a pull request from the same branch,
  to which the bullet above applies.

**The sandbox**

- **It can be replaced in the middle of the work.** What comes back is the
  branch's base with the files of the last turn on top, `/tmp` and the build
  directories gone, and some binary test files damaged (seven under
  `tests/wpt/encoding/`). Do not commit that tree: `git fetch origin
  '+refs/heads/*:refs/remotes/origin/*'`, then `git reset --hard
  origin/BRANCH`. So commit and push after every step, and keep nothing that
  matters only in `/tmp`. The clone is shallow: `git fetch --deepen=60 origin
  main` before a merge or a `git log`.
- **It can be suspended, and its clock is not.** When a turn ends, what was
  running stops where it was and goes on when the next turn begins, hours
  later by the clock. A check that measures time then reports nonsense:
  `make asan` came back with two deep pages "timed out" after 6,703
  seconds. Do not leave a long check running across the end of a turn, and
  run again any check that was.
- **It is small: 2 cores, 3.9 GB.** `make deep` with its two jobs runs out of
  memory on one page; `python3 tools/fuzz/deep_pages.py --jobs 1` does not.
  Nothing else runs beside `make asan`, `make deep` or a timing. `make fuzz`
  needs the sanitizer build and is part of `make asan`.
- **CI's logs cannot be read from here; a failed job's annotations can.** See
  "What done means". A job that fails with nothing but "exit code 2" is a job
  that does not end with the annotations step.

**Measuring, and writing it down**

- **A listed WPT failure is not always the engine's.** Before changing a
  binding to make one listed subtest pass, run the whole suite with the change.
  `domparsing/innerhtml-05.xhtml` (`test-checks-parent-global`) looks like a
  one-line fix that makes it throw in the caller's realm, and it costs 240
  passing subtests of `dom/nodes/Document-createElement.html`, which names the
  frame's `DOMException` as the one a failing `createElement` on the frame's
  document must throw. The engine keeps the receiver's realm — the rule both
  files can nearly hold, and what browsers do — so the row is listed as the
  test's. `docs/findings/68-the-realm-of-a-domexception.md` has the numbers.
- **Run the whole of test262 for a change to the language, not the
  directories that look related.** For Phase 63 the 25 directories about
  iteration said eight more pass and none fewer. The whole suite found four
  fewer, in the directories on classes and async generators: other code had
  been leaning on what was fixed.
  `python3 tools/conformance/run_test262.py --binary before=MAIN --binary now=build/litebird built-ins language annexB`,
  about twelve minutes. CI does not run it, so nothing else will say. #77
  and #79 changed the language and did not run it: five tests of
  `Array.prototype.forEach` stopped passing with the first and ten about
  `eval` with the second, beside the 96 and the 69 that began to pass, and
  nobody knew for a day. Both were found by running it on `main` before and
  after the merge, which anyone can do for a pull request that is already
  merged.
- **Measure what a change costs against `main`'s binary before calling it
  done**, with a second copy of `main`'s binary beside it to see how far a
  binary is from itself (3 to 5% here). The first version of Phase 43 was up
  to 39% slower, and nobody had asked. A loop that allocates can get slower the
  longer it runs, because memory is only reclaimed by `gc()` (Known gaps, 4):
  one over a `Map` took 12 microseconds a loop for its first thousand loops
  and 154 for its eighth thousand (the row on iterating a `Map` below says
  why). Keep such loops short and call `gc()` between measures.
- **A number is copied from a log, with the commit it was measured on.**
  Twice a sentence was written from memory with a number no run had printed,
  and one of them reached `main` (the cost paragraph of Phase 43, corrected
  in Phase 63). If the tree changes after the measurement, measure again or
  say which tree it was.
- **When you merge, correct what counts things**: the suite count on five
  lines of README.md (`grep -c "^    RUN_TEST(" tests/test_suite.cpp`), the
  floors, and the rows of the table below that say your pull request holds a
  file.

## Reading the queue

`ROADMAP.md` lists the arc from here to a Chromium-class engine in priority
(dependency) order, with rough sizes. **Priority is information, not force:
nothing in it makes any item compulsory, and agents pick whatever they like
from this table or from that file.** This table is the atomic claim-now queue;
that file is the map. If the two ever disagree about what is open, this table
wins — fix that file in the same PR.

## Open tasks

Rows that share files should not be in work at the same time.

| Task | Files | Described in | Note |
| :--- | :--- | :--- | :--- |
| **Pixels are measured since Phase 120** (`docs/phases/120-pixels-nobody-measured.md`, finding 121): `tools/conformance/render_check.py` renders each page in `tests/render/reftests.json` and decodes the PNG with **its own reader** (all five scanline filters, per-chunk CRC) so the engine is not asked to grade its own writer; `make render-conformance`, and a CI job. 10 pages, 30 assertions, exact equality because box edges are not anti-aliased. The harness self-tests first and was shown to fail: a one-column bug in `BitmapRenderer::fill_rect` reddened 9 of 10. Extended by Phase 122 (`docs/phases/122-the-oracle-reaches-layout.md`, finding 127) to **34 pages, 121 assertions** (Phase 134 added six and fixed three): flex main axis (`justify-content` `space-between`/`center`/`flex-end`, `flex-direction: column`, `flex-wrap`), grid tracks and `gap`, floats and `clear`, absolute positioning, `overflow` clipping, a data-URI image, `border-radius` (anti-aliased, so the corners are asserted `not-colour`) and `transform` (the ink moves, the layout does not). Each page may carry a `known_failure` string; the run prints `KNOWN` with the reason and does not count it as a pass, and an entry on a page that **passes** prints `FIXED` and exits 1, so the corpus shrinks rather than accumulating excuses. Five pages are known: paint order (floats and transformed elements), the square clip, and two found while fixing cross-axis alignment — `justify-content` in a column container and `align-self`, the rows below. Still open: inline formatting and line breaking, table cell backgrounds, SVG and canvas are unmeasured; **no WPT reftests are vendored** (`find tests/wpt -name '*-ref*'` is 0); glyph *shape* is not compared, only ink position; there is no two-build pixel diff | `tools/conformance/render_check.py`, `tests/render/`, `Makefile`, `.github/workflows/ci.yml` | `docs/phases/120-pixels-nobody-measured.md`, `docs/findings/121-a-pixel-was-never-compared.md` | Adding a reftest is one entry in `tests/render/reftests.json` and needs no engine change, so this row is a good one to extend rather than a claim to take |
| **Paint order is document order, not Appendix E** (finding 127): `DisplayList::build_recursive` emits each box's background as the tree walk reaches it, so a float is painted **under** the background of the block that follows it (`(10,10)` of a `float: left` red box beside a blue block is blue) and a `transform`ed element — which creates a stacking context, CSS Transforms 1 §3 — is painted **under** a later in-flow sibling where their ink overlaps. CSS 2.1 Appendix E paints in phases: in-flow block backgrounds (step 4), then floats (step 5), then inline content (step 7), then the positioned/stacking layer (step 8). Float placement, `clear: both` and the transform's own offset are all correct; only the order is wrong. Two recorded reftests: `a_float_is_painted_above_the_block_that_follows_it`, `transform_translate_moves_the_ink_and_not_the_layout` | `src/render/PaintCommand.cpp`, `include/litebird/render/PaintCommand.hpp`, `tests/render/reftests.json` | `docs/phases/122-the-oracle-reaches-layout.md`, `docs/findings/127-paint-order-was-document-order.md` | Found by the pixel oracle, not fixed in Phase 122. When the page passes, take the `reftests.json` entry out — the harness prints `FIXED` and fails until you do |
| **`overflow: hidden` clips to a rectangle, not to the border radius** (finding 127): the clip command carries `border_radius` and the corner is not removed, so a child of a 40x40 `border-radius: 16px` `overflow: hidden` box paints its own ink into the square corner at `(0,0)`. A rounded background and border *are* implemented and anti-aliased; only the clip is square, which is the narrower reading of `ARCHITECTURE.md`'s "rounded-corner clipping" gap. Recorded reftest: `overflow_hidden_clips_to_the_rounded_corner` | `src/render/PaintCommand.cpp`, `src/render/BitmapRenderer.cpp`, `tests/render/reftests.json` | `docs/phases/122-the-oracle-reaches-layout.md`, `docs/findings/127-paint-order-was-document-order.md` | Found by the pixel oracle, not fixed in Phase 122 |
| **`align-self` and `align-content` are not read, and `justify-content` is ignored in a column container** (finding 139): the container's `align-items` is now the cross-axis input (Phase 134, `docs/phases/134-cross-axis-alignment.md`), but an item cannot override it with `align-self`, a container cannot share its height between flex lines with `align-content`, and the column branch has no main-axis step at all — `justify-content: flex-end` on a `flex-direction: column` container leaves its item at the top. `justify-items` is unimplemented too and unmeasured. Two recorded reftests: `flex_column_justify_content_flex_end`, `flex_align_self_overrides_align_items` | `src/layout/` (flex and grid placement), `tests/render/reftests.json` | `docs/phases/134-cross-axis-alignment.md`, `docs/findings/139-the-line-was-as-tall-as-its-tallest-item.md` | Found while fixing row `Cross-axis alignment is not implemented`, which Phase 134 took; left as rows rather than fixed in passing, each with a page that fails until it is |
| **HTTP authentication is whole except for the parts that need a prompt or a proxy.** Phase 113 read the challenges (RFC 7617 `Basic`, RFC 7616 `Digest`) and Phase 115 (`docs/phases/115-the-rest-of-http-authentications-challenge.md`, finding 120) finished the challenge side: `SHA-512-256` (FIPS 180-4 §6.7, a different function from SHA-512 truncated), `auth-int` over the request body (§3.4.3), `userhash` (§3.4.4, the username field is `H(username ":" realm)` with the algorithm in use), `Authentication-Info`/`nextnonce` (§3.5, with `nc` counted per nonce), `407`/`Proxy-Authenticate` reported as `proxy_auth_schemes`/`proxy_auth_realm`, and a refused credential now **deleted** on the second 401 instead of being replayed for the life of the process. Two bugs in what Phase 113 had already shipped were fixed: `-sess` folded A1 with MD5 whatever the algorithm was (`SHA-256-sess` answered `1dfbe580…` where it should be `1eba72f3…`), and the loader ranked `SHA-512-256` as if it were `SHA-256` by prefix match. **Still open:** no prompt (credentials come from `ResourceLoader::set_auth_credentials`), no proxy support (`Proxy-Authorization` is never sent, so a 407 is reported and the load fails), `stale=true` is not honoured (a second 401 drops the credential where a `stale` one should be kept and retried), NFC normalisation of the username and password is not applied, and the store holds UTF-8 octets with any `charset` other than `UTF-8` refused | `src/net/ResourceLoader.cpp`, `include/litebird/net/ResourceLoader.hpp`, `tests/test_suite.cpp` | `docs/phases/113-http-authentication-challenges.md`, `docs/phases/115-the-rest-of-http-authentications-challenge.md`, `docs/findings/117-a-401-was-just-a-failed-load.md`, `docs/findings/120-a-published-vector-can-be-wrong.md` | The digests are held to published vectors and to an independent implementation, and the wire is a loopback server this repository starts. **RFC 7616 §3.9.2 as printed is wrong** — erratum 4897; the suite asserts the corrected pair and says why |
| **General ES-module semantics and transferable messaging remain open.** Phase 66 gives module workers a MIME-checked static graph subset (default/named/namespace imports; simple declaration/default/named exports; named re-exports) and gives Worker/Window/same-agent `MessagePort` neutral graph packets for primitives, ordinary objects and arrays. It deliberately has no live bindings, cycles, `export *`, dynamic `import()`, `import.meta`, top-level `await`, module `<script>` support, typed arrays/`ArrayBuffer`, transferable ownership handoff, transferred/cross-agent ports, or `messageerror`; functions, symbols, DOM/host/exotic values and non-empty transfer lists reject with `DataCloneError` | `src/js/Worker.cpp`, `src/js/JsOps.cpp`, `src/js/DomWindowBindings.cpp`, `src/js/DomBindings.cpp` | Phase 66 / finding 72 (`docs/phases/66-es-module-workers-and-structured-clone.md`, `docs/findings/72-es-module-workers-and-structured-clone.md`) | Phase 66 also makes the process-wide symbol/interface registries process-lifetime allocations, retaining Phase 40’s exit-order safety while removing their static-destructor race | **`export * from './mod.js'` done in Phase 105 / finding 111** (`docs/phases/105-export-star-from-a-module.md`): the star is recorded in `ModuleTransform::star_exports`, the source module's namespace is republished beneath the module's own names, `default` is not republished and a nearer declaration wins. A namespace property is now an enumerable getter that resolves when it is read, not a copy made when the wrapper finished. Still open: **two evaluations of one module are two instances**, so an imported function increments its own copy of an exporter's variables (`import { n, bump }` answers 1, 1 where the language has 1, 2); that is asserted in `module_worker_static_graph_loading_and_refusal` and is Phase 66's missing module-record machinery, not the star's. `export * as ns from` has its own message and is still refused |
| **The exception a `DOMException` is made in is the receiver's realm**: `domparsing/innerhtml-05.xhtml` stays listed as `test-checks-parent-global` because it asks for the caller's realm where `dom/nodes/Document-createElement.html` (240 passing subtests) asks for the frame's. The engine keeps the receiver's, as browsers do; no implementation passes both. `make_dom_exception` asks the node's realm first, then the method's | `src/js/DomBindings.cpp` (`make_dom_exception`), `include/litebird/js/DomBindings.hpp` | [`docs/phases/62-the-realm-of-a-domexception.md`](docs/phases/62-the-realm-of-a-domexception.md); [`docs/findings/68-the-realm-of-a-domexception.md`](docs/findings/68-the-realm-of-a-domexception.md); `tests/wpt/README.md` | Settled by measurement: the candidate change scored 7,759/8,009 with 240 unlisted. Do not re-open without a Chrome, Firefox and Safari run |
| **`TextDecoder` and `TextEncoder` are still not exposed to script**, though there is now an encoder: the WHATWG Encoding Standard's 40 decoders, 228 labels, BOM sniffing and the `<meta charset>` prescan landed in Phase 47, and Phase 110 (`docs/phases/110-the-other-way-through-the-encoding-tables.md`, finding 116) added the other direction — `encode_with_encoding()` and `encode_code_point()` in `src/net/TextEncoding.cpp`, 26 encoders over the same generated tables, no new data. Every byte of all 27 single-byte tables round trips, and 2,310,001 encodings over the whole of Unicode were checked against this engine's decoder and against Python's codecs. **What is left is the interface, and it is blocked on typed arrays**: `TextEncoder.encode()` returns a `Uint8Array`, `encodeInto()` writes into one, and `TextDecoder.decode()` takes an `ArrayBufferView`, so a global without them would return something no browser returns. Also open: the prescan is one pass over the first 1,024 bytes with no streaming | `src/net/TextEncoding.cpp`, `include/litebird/net/TextEncoding.hpp`, `src/js/DomWindowBindings.cpp`, `src/js/DomBindings.cpp` | `docs/phases/110-the-other-way-through-the-encoding-tables.md`, `docs/findings/116-a-decoder-is-not-an-encoder.md`, ARCHITECTURE.md Phase 47, section 5; AUDIT.md, finding 53 | The encoder is done; the WPT `encoding/` pages that need no globals are vendored and pass. `src/js/TypedArray.cpp` is PR #132's file — do not start the globals beside it |
| **Several built-ins still do not make the subclass they were called on.** With `class List extends Array {}`, `List.from([1])`, `List.of(1)` and `new List().map(f)` are plain arrays; `P.resolve(1)` for a subclass of `Promise` is a plain promise. There is no `Symbol.species` (`typeof Array[Symbol.species]` is `undefined`; `species_constructor` exists only for RegExp). (The *construction* is right: a subclass constructor's `super()` hands on what the parent built, holes in an array included — Phase 96. And `instanceof` now asks `Symbol.hasInstance`, since Phase 114 — `docs/phases/114-instanceof-asks-symbol-hasinstance.md`.) | `src/js/BuiltinsArray.cpp`, `src/js/Promise.cpp` | ARCHITECTURE.md, Phase 34, section 9; three failing cases of `prototypes.dat` | The Map/Set constructor slice landed in Phase 75 / finding 81 (`docs/phases/75-map-set-constructor-dispatch.md`, `docs/findings/81-map-set-constructor-dispatch.md`): it now makes the new.target object first, gets `set`/`add` once, calls it for every input and closes an iterator if it throws. The remaining Array/Promise/species work is separate, and is the one row left after Phase 114 took the `instanceof` half |
| **`length` is not inherited from an array prototype.** `Object.create([7, 8, 9]).length` is `undefined` where the standard has `3`. The indices are inherited since Phase 98 (`Object.create([7, 8, 9])[1]` is `8`), and since Phase 108 an accessor on an index of an array is the index's property, which was the other half of this row | `src/js/JsValue.cpp` (`get_property`, `lookup_chain`) | [`docs/phases/98-a-hole-is-a-place-the-prototype-can-fill.md`](docs/phases/98-a-hole-is-a-place-the-prototype-can-fill.md), section 4; [`docs/phases/108-an-accessor-on-an-array-index.md`](docs/phases/108-an-accessor-on-an-array-index.md), section 4 | `length` is computed from `array_elements` for a cell that is an array and is not stored, so a prototype's `length` is not reached. The suite `a_hole_is_a_place_the_prototype_can_fill` asserts the answer as it is, so that a change shows up: change the assertion with it. It is a read, so time it before and after |
| **A write to an index of an array does not ask the prototype for a setter, and `sort` does not read and write in the standard's order.** Phase 116 ([`docs/phases/116-the-methods-that-move-places.md`](docs/phases/116-the-methods-that-move-places.md), finding 125) did what this row used to ask for: `reverse`, `sort`, `shift`, `unshift`, `splice`, `fill` and `copyWithin` go place by place through an accessor on an index, `toReversed` and `toSorted` write `undefined` where there was a hole, and `toReversed` reads from the last place down (the row's examples, run on `main` on 20 November, print what Node.js prints). Its section 4 leaves two things that are no other row's. With `Object.defineProperty(Array.prototype, "0", { set(v) { … }, get() { return "p" } })` and `var a = []`, `a[0] = 5` writes the element: the setter is not called, `a.hasOwnProperty(0)` is `true` and `a[0]` is `5`, where the language calls the setter and `a` has no such property (`a[0]` is `"p"`); the write-up names two test262 cases that need it, `shift/set-length-array-is-frozen.js` and `unshift/set-length-array-is-frozen.js`. And `sort` does not do its `Get`, `Set` and `DeleteProperty` in the standard's order when `Array.prototype` has accessors: `sort/precise-prototype-accessors.js`, `precise-comparefn-throws.js`, `precise-prototype-element.js` | `src/js/JsValue.cpp` (`set_property`), `src/js/BuiltinsArray.cpp` | [`docs/phases/116-the-methods-that-move-places.md`](docs/phases/116-the-methods-that-move-places.md), section 4; [`docs/findings/125-reverse-ran-over-the-accessor.md`](docs/findings/125-reverse-ran-over-the-accessor.md) | The first is on the path of every `a[i] = v`, not only of the methods: time a loop of writes with both builds, because an array with nothing on its prototype chain must not pay for the question. Section 4 names three more things, and they are other rows or no row: an index that is not writable ("A data property on an index of an array has no attributes of its own"), `Symbol.species` in `splice` ("Several built-ins still do not make the subclass they were called on"), and the copying methods taking the place-by-place form for every array with a hole, which costs time and changes no answer |
| **Array destructuring reads the places since Phase 137** ([`docs/phases/137-a-pattern-reads-the-place-not-the-vector.md`](docs/phases/137-a-pattern-reads-the-place-not-the-vector.md), finding 142): `var [x] = a` took the elements of an array whose iterator is the installed one straight from `array_elements`, so a hole was `undefined` to it whatever the index read as — with an accessor on index 0 whose getter returns `"g"`, `var [x] = a` bound `undefined` where `for (x of a)` and `[...a]` gave `"g"` since Phase 108; with `Array.prototype[1] = "p"`, `var [x, y] = [1, , 3]` bound `undefined` to `y` where the language binds `"p"`. `IterationCursor::pull` now reads each place with `js_array_get`, which is what `a[i]` reads: an element costs what it did, and a hole asks `js_array_holes_read_undefined` before looking an index up by name (ten-place patterns 200 → 197 ms over 200,000 iterations on a dense array, 190 → 201 ms where every place is a hole). String and protocol sources were already right, and a replaced `Symbol.iterator` is still asked | `src/js/JsEval.cpp` (`IterationCursor::pull`, the `Mode::Array` case) | [`docs/phases/108-an-accessor-on-an-array-index.md`](docs/phases/108-an-accessor-on-an-array-index.md), section 4, which left it open; the suite is `array_destructuring_reads_the_place_not_the_vector` | Done: 13 checks, 9 of them failing on `main` at `e1414f6`; 297/297 unit suites, and no corpus moved — js-conformance 1143/1158, DOM 678/678, trees 1973/1973, URL 1188/1188, WPT 9567/9824 |
| **A data property on an index of an array has no attributes of its own.** `Object.defineProperty(a, "0", { value: 1, writable: false, enumerable: false, configurable: false })` leaves index 0 writable, enumerable and configurable: the descriptor reads `true` three times, `a[0] = 9` writes, `Object.keys(a)` lists it and `delete a[0]` deletes it. An element is a value and nothing else | `src/js/BuiltinsObject.cpp` (`define_from_descriptor`), `src/js/JsValue.cpp` | [`docs/phases/108-an-accessor-on-an-array-index.md`](docs/phases/108-an-accessor-on-an-array-index.md), section 2, for where such a property can be kept | Phase 123's strict `delete` is refused for every non-configurable property except one of these, because the flags are not where `[[Delete]]` looks: `'use strict'; delete a[0]` on it is `false` and not the `TypeError` it should be. Phase 108 made the place: an accessor on an index is kept in the property map with the place a hole, and a data property written over one is kept there with its flags (`set_property` asks whether it is writable, `own_keys` whether it is enumerable, a shorter `length` whether it is configurable). A data descriptor with an attribute that is false can go the same way instead of taking the dense shortcut. The whole of test262 with two builds says what it is worth and what it costs |
| **The refused `delete` and its message are done since Phase 123** (`docs/phases/123-a-strict-delete-refuses.md`, finding 128): a property that is not configurable — and an array's `length`, which `[[Delete]]` used to answer `true` for — answers `false` in sloppy code and is a `TypeError` in strict code with V8's words; `delete name` is answered (a `var` `false`, a configurable global property removed, an unresolvable name `true`, `this` `true`) and is the language's early `SyntaxError` in strict code. Two consequences of it stay open: an index of an array defined through a descriptor keeps no attributes, so deleting one does not throw (the row above), and `arguments`' prototype is `Array.prototype` here, so its message says `#<Array>` where V8 says `#<Object>` | `src/js/JsEval.cpp` (`case JsTokenType::Delete`) | [`docs/phases/108-an-accessor-on-an-array-index.md`](docs/phases/108-an-accessor-on-an-array-index.md), section 4, where it was met | Found while checking `pop` of an accessor that is not configurable, which does throw. Whether the code is strict is known where the operator is evaluated |
| **The four `__defineGetter__` relatives do not exist** (the `Reflect` half of this row landed in Phase 124 / finding 129, [`docs/phases/124-reflect-describes-and-answers-extensibility.md`](docs/phases/124-reflect-describes-and-answers-extensibility.md)). `Object.prototype.__defineGetter__`, `__defineSetter__`, `__lookupGetter__` and `__lookupSetter__` are `undefined`; `Reflect.getOwnPropertyDescriptor`, `Reflect.isExtensible` and `Reflect.preventExtensions` now exist and are held to Node.js by `make test` and to `built-ins/Reflect` by test262 (113/153, from 95) | `src/js/BuiltinsObject.cpp` | test262 `built-ins/Object/prototype/__defineGetter__` and its three neighbours, `built-ins/Reflect/getOwnPropertyDescriptor`, `isExtensible`, `preventExtensions`; [`docs/phases/108-an-accessor-on-an-array-index.md`](docs/phases/108-an-accessor-on-an-array-index.md), section 4 | The four are Annex B: each is `define_from_descriptor`/the property map read back, which exists since Phase 108; they are the same file as the `Reflect` work, so they are named here rather than left in a row that is now half done |
| **An array method no longer copies its receiver, since Phase 129** ([`docs/phases/129-an-array-method-must-not-copy-its-receiver.md`](docs/phases/129-an-array-method-must-not-copy-its-receiver.md), finding 134): `receiver()` used to read index 0 up to `length` before the method looked at an argument, so `{ 0: 9, length: Infinity }` was `std::bad_alloc` or SIGKILL and `{ length: 2**32 }` was an allocation where the standard wants a `RangeError`. test262 `built-ins/Array/prototype` went 1,391 → 1,523 of 1,632, 22 crashes → 0, 132 gained and none lost. **Still open:** 109 tests in those directories fail; `find`/`findLast` do not walk a run of missing places above a 16-million bound (stated in the code); and the methods that *move* elements — `reverse`, `sort`, `splice`, `fill`, `copyWithin`, `shift`, `unshift`, `with`, `toSpliced` — and the iterator objects were not audited and some still loop to `length_of` | `src/js/BuiltinsArray.cpp` | [`docs/phases/129-an-array-method-must-not-copy-its-receiver.md`](docs/phases/129-an-array-method-must-not-copy-its-receiver.md), [`docs/findings/134-an-array-method-copied-what-it-could-not-hold.md`](docs/findings/134-an-array-method-copied-what-it-could-not-hold.md) | Adding a method is one `walk_present_indices` call; the crashes were all in the one `receiver()` helper |
| **What a function is, in small**: nothing tells a built-in that is a constructor from one that is not (`new Math.max()` makes an object; `class X extends f.bind(null)` and `Reflect.construct(F, [], Date.now)` are accepted); `Object.getOwnPropertyNames(f)` lacks `length` and `name`, and `f.name = 'x'` takes effect; async and generator functions have `Function.prototype` itself as their prototype, where the language has an object for each kind between the two (three test262 tests, `AsyncFunctionPrototype-is-not-callable.js` and its relatives, passed while `Function.prototype` was not callable and fail since Phase 34); a property of a prototype that cannot be written does not stop an assignment to the object that inherits it | `src/js/JsOps.cpp`, `src/js/JsValue.cpp`, `src/js/BuiltinsObject.cpp` (`bind`), `include/litebird/js/Heap.hpp` (`JsObject::not_constructor` is the bit to set) | ARCHITECTURE.md, Phase 34, section 9; four failing cases of `prototypes.dat` | Phase 114 made `instanceof` ask `Symbol.hasInstance` and made a script function with no object `prototype` throw the language's `TypeError`, which is what `x instanceof` a bound function could not be asked before (a bound function delegates to its target now). Six host constructors still have no `prototype` (`Response`, `Headers`, `Worker`, `WebSocket`, `IntersectionObserver`, `ResizeObserver`), and `r instanceof Response` is `false` where a browser says `true`: a *native* function with no `prototype` is left answering `false`, so `1 instanceof Math.max` is `false` too. Give those six a `prototype` and the operator can throw for every function that has none |
| **A script can read and write the engine's own slots**: they are properties whose names begin with `%`, hidden from enumeration and from nothing else. (A private name used to be the same kind of hole and is not any more: since Phase 88 it is an identity in a table of its own, which no string key reaches.) After `el.classList` was read, `el['%classList'] = 5` makes `el.classList` a number; `method['%home']` is the object `super` is looked up from, and assigning to it changes what `super` finds. The other side of it: a property of the script's own whose name begins with `%`, `#` or `@@`, or has the shape `__x__`, is left out of `Object.keys`, `JSON.stringify`, `for…in`, `Object.assign` and spread, so `var m = {}; m['#/route'] = 'home'; JSON.stringify(m)` is `{}` | `src/js/DomBindings.cpp` (`slot`, `set_slot`, `clear_slot`), `src/js/JsEval.cpp` (`%home`, `%fn`, `%new.target`), `src/js/JsValue.cpp` (`is_hidden_key`) | AUDIT.md, finding 41 | Move the slots off the property table (Phase 34 moved one: `JsObject::not_constructor`), so that no name is special; refusing such a key from a script would only trade one fault for the other. Not beside a row that changes `src/js/Dom*Bindings.cpp` |
| **`for…of` takes an object that only has `next`.** Array destructuring asked for its iterator from Phase 102 (`docs/phases/102-a-pattern-asks-for-the-iterator.md`), so a pattern now refuses `var [x] = { next() { … } }` as the language does; **`js_iterate()` still accepts it** (`method_is_installed()` falls back to a bare `{ next }`), where Node.js answers `TypeError`. The same fallback is what makes an object with no `Symbol.iterator` at all iterable in a `for…of` here and not in a browser | `src/js/JsOps.cpp` (`iterate_with`, `method_is_installed`), `src/js/JsEval.cpp` (the pattern side, done) | `docs/phases/102-a-pattern-asks-for-the-iterator.md`, 'What is still open'; `docs/findings/108-a-pattern-did-not-ask.md`; two cases of `tests/js-conformance/iteration.dat` | Drop the `{ next }` fallback in `js_iterate()` and make it the GetIterator every other caller makes. Check the corpus first: `iteration.dat` has a case that records the old behaviour of a *pattern* only, so nothing there depends on the loop keeping it |
| **`Array.from` asks the source, and maps as the values arrive.** Phase 94 landed this (`docs/phases/94-array-from-asks-the-source.md`): GetMethod(`@@iterator`) is asked of everything, an array and a string included, and only a source that has none is read as array-like (ToLength of `length`, then each index); the mapper is called as each value arrives, inside the visitor of `js_iterate`, so a mapper that throws closes the iterator and an iterator with no end costs one value. What is still open: the result is always a plain array, whatever `this` is — `List.from([1])` for `class List extends Array {}` is an `Array` (that is the row on the subclass a built-in is called on), and `Array.from` reads `length` and the indices through the ordinary property lookup with no `Array.prototype` fast path | `src/js/BuiltinsArray.cpp` (`Array.from`) | Phase 63, section 6; Phase 94 / finding 100 (`docs/findings/100-array-from-decided-what-was-iterable.md`) | `js_iterator_method(source, method)` is the language's GetMethod, and `js_iterate` closes the iterator when the visitor throws. `tests/js-conformance/iteration.dat` has the four cases this phase added, recorded from Node.js |
| **The default constructor of a subclass spreads its arguments.** For a class that has no constructor the parser writes `constructor(...args) { super(...args); }`, and since Phase 63 a spread asks the array for its iterator. So a script that replaced `Array.prototype[Symbol.iterator]` is called by `new Derived(1, 2)`, once for each default constructor on the way up, where the language hands the arguments on without iterating ("does not use argument evaluation", in the words of the test); a replacement that throws makes every such `new` throw. test262's `language/statements/class/subclass/default-constructor-spread-override.js` passed before Phase 63, when nothing asked an array, and fails now | `src/js/JsParser.cpp` (where the default constructor is made), `src/js/JsEval.cpp` (`eval_arguments`) | ARCHITECTURE.md, Phase 63, section 6; one failing case of `iteration.dat` | Mark the spread the parser makes (a flag on its `SpreadExpr`) and, for a marked one, copy the rest array's elements in `eval_arguments` without `js_iterate`. A constructor that a script writes with the same text has to keep asking. Not done in Phase 63 because both files belonged to an open pull request then (#68) |
| **What an iterator still is not**: a symbol is a string in this engine, so a property key cannot be one. `Symbol.iterator` is the key `"@@iterator"`, and `"@@iterator" in []` is `true` where the case of `iteration.dat` asks for `false`; `BuiltinsArray.cpp` registers the array's iterator under `"Symbol.iterator"` as well, for the same reason. Phase 92 gave each kind a prototype between it and `%IteratorPrototype%`, with the kind's one `next` and a `Symbol.toStringTag`, so `Object.getPrototypeOf(it).next` is a function a script can replace and `Object.prototype.toString.call(it)` names the kind; the loop fast paths now ask whether that `next` is still the installed one (`holds_installed_next`) before reading elements directly, and take the long way when it is not. What is left: the symbol keys above, `%IteratorPrototype%` helpers (`map`, `filter`, `take`, `drop`), a `%GeneratorPrototype%` per kind (a generator's `next` is still its own), and `return` on `RegExp String Iterator` | `src/js/JsValue.hpp` (`PropertyMap`, the part that would become a key), `src/js/JsOps.cpp`, `src/js/Generator.cpp` | Phase 63, section 6; Phase 92, finding 98; one failing case of `iteration.dat` (`no property is named 'Symbol.iterator' or '@@iterator'`) | The one thing not to undo: `next` belongs to the prototype, and the fast path's permission to skip it is a question asked of the prototype, not an assumption made about the iterator |
| **What the DOM corpus cannot ask**: `getComputedStyle(el).width` is still `auto` (used values are available via `getBoundingClientRect()`/`offset*`/`client*` since Phase 36), and the SVG DOM (`className.baseVal`, `viewBox`, `getBBox()`: `tests/dom-conformance/open/dates-and-svg.svg`) is not there. The style sheet objects landed in Phase 72 (`style.sheet`, `document.styleSheets`, `CSSStyleSheet.cssRules`/`insertRule`/`deleteRule`/`disabled`, `CSSRuleList`, `CSSRule`/`CSSStyleRule`), `CSSMediaRule` in Phase 84, and `CSSKeyframesRule`/`CSSKeyframeRule` in Phase 112 (`docs/phases/112-keyframes-in-the-cssom.md`): `cssRules` now lists every rule the sheet holds, in source order, and `@keyframes` blocks are editable through `name`, `cssRules`, `appendRule`, `deleteRule` and `findRule`. What is still missing: `CSSImportRule` (an `@import` is dropped when a sheet's text is read), a script-visible `CSSFontFaceRule`, `replace` (the promise, which waits on `@import`), `CSSKeyframesRule` feeding the animation engine (which still reads `css::StyleSheet::keyframes`, not the CSSOM's model), and used values for `getComputedStyle(el).width`. `new CSSStyleSheet()` and `replaceSync` exist since Phase 111 ([`docs/phases/111-a-sheet-a-script-constructs.md`](docs/phases/111-a-sheet-a-script-constructs.md), finding 126); a sheet a script builds is not in the cascade | `src/js/Dom*Bindings.cpp`, `src/main.cpp`, `src/shell/` | `docs/phases/72-cssom.md`, `docs/phases/112-keyframes-in-the-cssom.md`; ARCHITECTURE.md, Phase 27, section 12 & Phase 36 | jsdom does no layout, so the first needs a reference of its own before it is built; the DOM corpus cannot record the CSSOM either, so it is asserted in `tests/test_suite.cpp` (`cssom_sheets_rules_insert_amounts_and_fonts`, `cssom_media_rules_and_the_cascade_inside_them`, `keyframes_are_rules_of_the_cssom`) |
| **The margin of `body` makes the page narrower and moves nothing.** `<body><div style="background:#f00;width:100px;height:20px"></div></body>` puts the box at (0, 0): `--render-png` paints it from the corner pixel and `div.getBoundingClientRect()` has `left` 0 and `top` 0, where a browser has 8 and 8, the default margin of `body`. With `body { margin: 40px }` the box is still at (0, 0) and belongs at (40, 40). The width is right both times (`document.body.getBoundingClientRect().width` is 784 and 720 in a viewport of 800), so the margin is taken off the width and not added to the position. With `body { margin: 40px; padding: 5px; border: 3px solid }` the body's own rectangle begins at (−8, −8): its border and its padding are outside the picture. Every page with the default style sheet is drawn 8 px up and to the left of where a browser draws it | `src/layout/LayoutTree.cpp`: for a document `build_boxes` begins at `body`, so `html` makes no box (a margin on `html` does nothing: with `<html style="margin:30px">` the body is still 800 wide at 0, 0), and `LayoutTree::build` sets the content box of the root box, which is `body`'s, to 0, 0 | CSS 2 §8.3 and §10.1; measured on `main` at `00d80a7` on 20 November, while looking at what #140 draws | Nothing measures it: each of the ten pages of `tests/render/reftests.json` begins with `body { margin: 0 }`. A page without that line is the first test to write, and it fails today. What is positioned from the root moves with the fix, so run `make dom-conformance`, `make wpt` and `make render-conformance` before and after and read what changed |
| **Memory is reclaimed only when a script calls `gc()`**, and no leak audit has been done. One leak is known: a function bound to an object that holds it (`o.b = f.bind(o)`, 2,000 times: 2,000 cells left after `gc()`), and what a pending timer holds. The collector cannot see into what a native function captured. What it costs meanwhile: `for (i = 0; i < n; i++) o = { a: i }` keeps about 1 KB for each object it has dropped (433 MB of resident memory after 400,000), so a loop of three million takes more than 3 GB; a benchmark written for Phase 43 brought a 4 GB machine to a stop that way until it called `gc()` between its loops | `src/js/Heap.cpp`, and everything that allocates script objects | Known gaps, 4; ARCHITECTURE.md, Phase 34, section 9; one failing case of `tests/js-conformance/prototypes.dat` | Touches everything that makes script objects, the DOM bindings included: not beside a row that changes `src/js/Dom*Bindings.cpp` |
| **HTTPS certificate test coverage remains partial.** Phase 89 fixed IP-literal identity checks and Phase 100 (`docs/phases/100-https-certificate-chains-dates-and-ipv6.md`) grew the fixtures to twenty, cross-checked against Python ssl: a chain through an intermediate (trusted, intermediate missing, intermediate `CA:FALSE`), dates (expired leaf, not-yet-valid leaf, expired intermediate), IPv6 literals in compressed and expanded form with the wrong address family written both ways, and SNI (a name for a DNS host, none at a literal URL — the fixture asserts what the engine sent). No engine change was needed: those checks are OpenSSL's and Phase 89's parameters were already right. What remains open is revocation (OCSP/CRL), pinning, TLS 1.3 resumption, and the fact that the oracle is the same OpenSSL library, not an independent TLS stack | `tools/security/verify_https.py` | `docs/phases/89-https-certificate-identity.md`, `docs/findings/95-https-ip-identity.md`, `docs/phases/100-https-certificate-chains-dates-and-ipv6.md`, `docs/findings/106-the-tls-fixtures-never-chained.md` | Python ssl is an independent client using the same OpenSSL library, not an independent TLS implementation. A non-self-signed certificate in the trust store is rejected, as Python rejects it; browsers accept it with `X509_V_FLAG_PARTIAL_CHAIN`, and that divergence is not taken |
| **WebSocket `ArrayBuffer`/`Blob` binary exposure and extensions remain open.** Phase 90 landed authenticated `wss://` TLS (OpenSSL 3 CA/SNI/DNS+IP SAN), Fetch `Origin` & mixed-content enforcement, relative URL resolution, live JS accessors, synchronous validation, CSPRNG keys/masks, RFC 6455/3629 framing/UTF-8 checks, `poll()` + wake-pipe I/O, and exit thread join; Phase 117 added the upgrade's cookies — the request carries what `CookieJar` holds for the URL (`ws` matched as `http`, `wss` as `https`, which is the whole of the `Secure` question) with the page URL as the site for cookies, and a `Set-Cookie` on the `101` is stored before the upgrade is validated (`make security-websocket`, 32/32). Open: `send(ArrayBuffer/TypedArray/Blob)` and `binaryType = "arraybuffer"` message delivery (blocked on Wave 2 `ArrayBuffer`), HTTP proxies, `permessage-deflate`, and standalone `CloseEvent`/`MessageEvent` constructors (`WebSocket.prototype` is tracked in the host-constructor row above) | `src/net/WebSocket.cpp`, `include/litebird/net/WebSocket.hpp` | Phase 90 / finding 96 (`docs/phases/90-websocket-tls-fetch-policy-and-frames.md`, `docs/findings/96-websocket-tls-origin-and-framing.md`), Phase 117 / finding 122 (`docs/phases/117-cookies-on-the-websocket-upgrade.md`, `docs/findings/122-a-websocket-had-no-cookies.md`) | Binary frame JS exposure should land after `ArrayBuffer`/typed arrays exist rather than inventing a custom buffer type |
| **What is left of `DataView` and `%TypedArray%.prototype` after Phase 126.** Phase 106 supplied `ArrayBuffer` and the nine typed arrays; Phase 126 (`docs/phases/126-a-dataview-and-the-iterating-half.md`, finding 131) added `DataView` — `buffer`/`byteOffset`/`byteLength` and `get`/`set` for 8, 16, 32 and 64 bits with the `littleEndian` argument, `RangeError` past the end and `TypeError` on a non-buffer — and the thirteen methods that were missing: `at`, `map`, `filter`, `forEach`, `some`, `every`, `find`, `findIndex`, `reduce`, `reduceRight`, `sort` (numeric by default, `NaN` last, stable), `toSorted`, `toReversed`, `with`. It also made `view.buffer === buf` true without asking the view first and made `join` print `NaN`. **Still open:** `BigInt64`/`BigUint64` on a `DataView` (needs `BigInt`), `Symbol.species` (the receiver's prototype stands in, so a subclass is right and an explicit species override is ignored), `toLocaleString` on a view, and `transfer()`/`SharedArrayBuffer` — the detached flag is checked by every `DataView` method and nothing can set it | `src/js/TypedArray.cpp`, `include/litebird/js/TypedArray.hpp`, `tests/test_suite.cpp` | `docs/phases/106-typed-arrays-and-arraybuffer.md` section 4, `docs/phases/126-a-dataview-and-the-iterating-half.md`, `docs/findings/131-a-buffer-had-one-way-to-be-read.md` | Additive: the file is new in Phase 106 and no other open pull request names it. The byte order is asserted against the bytes another view sees, never against the getter that wrote them |
| **The four interfaces typed arrays unblocked.** Phase 106 made `ArrayBuffer` exist, so each of these is now writable and none of them is written: `fetch()` has no way to hand back a binary body (`Response.arrayBuffer()`, and `arrayBuffer()` on a `Blob`, which `DomWindowBindings.cpp` still answers with `null` and a comment saying why), `WebSocket` cannot send `ArrayBuffer`/`TypedArray`/`Blob` or deliver a binary frame under `binaryType = "arraybuffer"`, `CanvasRenderingContext2D` has no `ImageData` (`createImageData`, `getImageData`, `putImageData`), and `TextDecoder`/`TextEncoder` are still not exposed to script, which is now the *only* thing missing in that direction: the 40 decoders landed in Phase 47 and the 26 encoders in Phase 110, so both directions exist in C++ and `decode()`/`encode()` have a `BufferSource` to take as of Phase 106 — what is left is the binding, not a codec | `src/net/Fetch.cpp`, `src/net/WebSocket.cpp`, `src/js/DomWindowBindings.cpp`, `src/render/Canvas2D.cpp`, `src/js/JsInterpreter.cpp` | `ROADMAP.md` row 6; `AGENTS.md` row on WebSocket binary exposure; `docs/phases/106-typed-arrays-and-arraybuffer.md`, section 4 | One row per interface, not one for all four: they share no file except `src/js/JsInterpreter.cpp`, where each installs its globals. `TextDecoder`/`TextEncoder` is the cheapest and reads `src/net/TextEncoding.cpp` without changing it. The row above and this one share `src/js/TypedArray.cpp` only if both are taken at once: whoever takes one first holds that file, and the other waits or takes `DataView` after it |
| **AVIF** | `src/render/ImageDecoder.cpp` | ARCHITECTURE.md, Phase 36, Phase 38 & Phase 54; [`docs/phases/57-lossy-webp-vp8-key-frame.md`](docs/phases/57-lossy-webp-vp8-key-frame.md), [`docs/phases/79-animated-webp-frames-on-a-canvas.md`](docs/phases/79-animated-webp-frames-on-a-canvas.md); AUDIT.md, findings 43, 44, 60 & 85; [`docs/findings/63-lossy-webp-vp8-refused.md`](docs/findings/63-lossy-webp-vp8-refused.md) | PNG, JPEG, GIF, BMP, PPM, **lossless WebP** (`VP8L`, all four transforms, meta prefix codes, colour cache), **lossy WebP** (a VP8 key frame: the boolean decoder, the 16x16/8x8/4x4 intra predictors, the 4x4 IDCT and the Walsh–Hadamard transform, the loop filter, plus the `VP8X` container and an `ALPH` plane in either compression method and all three spatial filters), **animated WebP** (`ANIM`/`ANMF`: the frame walk, the key-frame rule, blending and disposal on the canvas — `<img>` shows the first frame, and `WebPDecoder::decode_frames()` hands back every one), and SVG are wired through `ImageDecoder::decode()`. Canvas 2D text is done (Phase 46, finding 52). SVG text is done: Phase 82/finding 88 added `<text>` / `<tspan>` with inherited solid paint and first-run positioning, and Phase 107/finding 113 added the rest — `x`/`y`/`dx`/`dy`/`rotate` as one value per character, `text-anchor` measured over the whole text chunk across `<tspan>`s, `<textPath>` with `startOffset` and per-glyph tangent rotation, and `url(#…)` paint servers (gradient, or the fallback colour) on glyph fill and stroke ([`docs/phases/107-svg-text-positions-chunks-textpath-and-paint.md`](docs/phases/107-svg-text-positions-chunks-textpath-and-paint.md), [`docs/findings/113-svg-text-positioned-one-chunk-at-a-time.md`](docs/findings/113-svg-text-positioned-one-chunk-at-a-time.md)). What is left here: **AVIF**, an AV1 intra-frame decoder ([`docs/phases/107-…`](docs/phases/107-svg-text-positions-chunks-textpath-and-paint.md) §4 lists what Phase 107 did not take — `textLength`, letter/word-spacing, baselines below `alphabetic`, `<tref>`, bidi, a path's own transform) |
| ~~**`Object.isFrozen` and `Object.isSealed` answer `false` for an object that is not extensible and has no properties.**~~ **Fixed in Phase 125 (finding 130).** Both are ECMA-262 §7.3.15 TestIntegrityLevel — not extensible, then every own property asked whether it is writable (frozen only) or configurable — and now say so. The engine had kept `frozen`/`sealed` as bits only `freeze()`/`seal()` set, so `preventExtensions({})`, `seal({})` and `preventExtensions` over fully-read-only properties all answered `false` where the language has `true`. test262 `built-ins/Object/isFrozen` + `isSealed`: 85/92 → 88/92 | `src/js/BuiltinsObject.cpp` (`test_integrity_level`) | ECMA-262 §20.1.2.13, §20.1.2.15, §7.3.15 | Done; the four remaining failures are the `Proxy` row and the `not-a-constructor` row |
| ~~**A `String` object's own index properties are not described.**~~ **Fixed in Phase 132 (finding 137).** A String exotic object has its UTF-16 code-unit indices as own properties: non-writable, enumerable, non-configurable. `descriptor_for` handled the primitive `JsType::String` and its wrapper's `length`, but skipped the `ObjectClass::String` wrapper's virtual indices; it now asks the wrapped string before the ordinary property map. `Object.getOwnPropertyDescriptor`, `Reflect.getOwnPropertyDescriptor`, `Object.getOwnPropertyDescriptors` and `propertyIsEnumerable` agree with Node.js v22.22.3, including a surrogate-pair code unit | `src/js/BuiltinsObject.cpp` (`descriptor_for`) | ECMA-262 §10.4.3, §20.1.2.9, §28.1.6 | Closed; an index past the wrapped length remains an ordinary property |
| ~~**An array that is sealed or non-extensible still grows.**~~ **No longer true — fixed by #146 (Phase 116, finding 125) before this row was written.** Found while probing Phase 125 on a tree that predated that merge: `push` tested `object_val->frozen` rather than `[[IsExtensible]]`, so `Object.seal([1,2]).push(3)` grew the array where the language throws, and the same held for `unshift`, a growing `splice`, `sort` and `pop` on a sealed or `preventExtensions` array. Re-probed after merging #146: all eight cases throw exactly where Node v22 throws, so the row is closed with no code change of its own. Recorded rather than deleted because the probe and the reasoning are in finding 130 §4 | — | ECMA-262 §7.3.3 Set, §7.3.4 CreateDataPropertyOrThrow | Closed by #146; verified against Node in Phase 125 |
| **The early errors a class body still does not check.** Phase 128 (`docs/phases/128-a-class-body-only-knew-three-early-errors.md`) added the six regular rules of §15.7 — Contains SuperCall in a field initialiser and a static block, `super()` outside a derived constructor, an instance `constructor` that is an accessor, async or a generator, a field named `constructor`, a static element named `prototype` — for +173 cases of test262 and 0 lost. What is still open in that family: **136 more early-error failures** outside the class-body rules (60 in `language/expressions/object`, 64 across the two `function` directories, 12 in assignment and delete), and **`class/dstr`'s 144**, which is destructuring in parameter position and a parser area of its own. Beyond the early errors, `Proxy` (96 cases) and `$262` (53, the suite's own host object: `detachArrayBuffer`, `createRealm`, `gc`) are whole features rather than repairs | `src/js/JsParser.cpp` (`parse_class`, `SuperExpr`) | Phase 128 / finding 133 (`docs/findings/133-five-hundred-failures-one-question.md`) | The two rules that look alike are not: `super()` in a method is a *grammar* absence (a FunctionBody does not contain it, so Node refuses to parse rather than throwing), while a missed runtime `TypeError` is a different and much larger set of failures. Grading a parser change needs the two-binary run — a narrowing change can raise the pass count while widening a bug |
