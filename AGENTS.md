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
   empty commit needs no 28 minutes of runner time.

## The files every pull request changes

`README.md`, `ARCHITECTURE.md`, `AUDIT.md`, `tests/test_suite.cpp` (one file,
with the suites registered in a list at its end), `Makefile`,
`.github/workflows/ci.yml` and the table below. Two pull requests written at
the same time will both have changed them. So:

1. Add a block of your own: a section, a group of tests. Do not re-wrap or
   reorder what is there.
2. **Before merging, merge `main` into your branch.** Resolve, correct the
   numbers that count things (the unit suites in README.md, the floors in
   `ci.yml` and the Makefile), run the checks again, push, and let CI pass on
   that commit. CI that passed on a branch behind `main` says nothing about
   the merge.

## What "done" means

- The checks: `make test`, `make conformance`, `make js-conformance`,
  `make dom-conformance`, `make url-conformance`, `make xml-conformance`,
  `make wpt`, `make diff-trees`, `make diff-xml`,
  `make fuzz`, `make deep`, `make asan`. The last two take
  about eight minutes each. CI runs all of them, and one CI run is about 28
  minutes of runner time: run the checks here first and push when they pass,
  not to find out whether they do. `make xml-conformance` downloads the
  W3C suite the first time (`tests/xml/README.md`), and `make diff-xml`
  needs `pip install lxml`.
- A unit suite that passes in the normal build can fail in the sanitizer
  build, and CI's log cannot be read from every sandbox: run `make asan`
  here first. When CI's sanitizer job fails all the same, its last step
  says what failed in the job's annotations, which the API itself serves:
  `gh run view RUN --json jobs` for the job's id, then
  `gh api repos/OWNER/REPO/check-runs/JOB_ID/annotations --jq '.[].message'`.
  (`tools/ci/annotate_failure.py`; any job that keeps its output with
  `tee` can end with the same step.)
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
  afterwards.
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

Everything here happened between 15 and 18 November 2026, to agents that had
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
  but merging them in one branch and running CI once.
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
- **A run is seven jobs, about 28 minutes.** `[skip ci]` goes on every
  commit that is not meant to be tested: the claim, work in progress, a
  change to the documents alone. The commit that is merged has a green run,
  or differs from one that has only in documents, and the pull request says
  so; a pull request of documents alone needs no run at all
  (`check_pr.py merge N` says which it is).

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
| **General ES-module semantics and transferable messaging remain open.** Phase 66 gives module workers a MIME-checked static graph subset (default/named/namespace imports; simple declaration/default/named exports; named re-exports) and gives Worker/Window/same-agent `MessagePort` neutral graph packets for primitives, ordinary objects and arrays. It deliberately has no live bindings, cycles, `export *`, dynamic `import()`, `import.meta`, top-level `await`, module `<script>` support, typed arrays/`ArrayBuffer`, transferable ownership handoff, transferred/cross-agent ports, or `messageerror`; functions, symbols, DOM/host/exotic values and non-empty transfer lists reject with `DataCloneError` | `src/js/Worker.cpp`, `src/js/JsOps.cpp`, `src/js/DomWindowBindings.cpp`, `src/js/DomBindings.cpp` | Phase 66 / finding 72 (`docs/phases/66-es-module-workers-and-structured-clone.md`, `docs/findings/72-es-module-workers-and-structured-clone.md`) | Phase 66 also makes the process-wide symbol/interface registries process-lifetime allocations, retaining Phase 40’s exit-order safety while removing their static-destructor race |
| **The exception a `DOMException` is made in is the receiver's realm**: `domparsing/innerhtml-05.xhtml` stays listed as `test-checks-parent-global` because it asks for the caller's realm where `dom/nodes/Document-createElement.html` (240 passing subtests) asks for the frame's. The engine keeps the receiver's, as browsers do; no implementation passes both. `make_dom_exception` asks the node's realm first, then the method's | `src/js/DomBindings.cpp` (`make_dom_exception`), `include/litebird/js/DomBindings.hpp` | [`docs/phases/62-the-realm-of-a-domexception.md`](docs/phases/62-the-realm-of-a-domexception.md); [`docs/findings/68-the-realm-of-a-domexception.md`](docs/findings/68-the-realm-of-a-domexception.md); `tests/wpt/README.md` | Settled by measurement: the candidate change scored 7,759/8,009 with 240 unlisted. Do not re-open without a Chrome, Firefox and Safari run |
| **`TextDecoder` and `TextEncoder` are not exposed to script**, and there is no encoder: the WHATWG Encoding Standard's 40 decoders and 228 labels landed in Phase 47 (`<meta charset>` prescan, `document.characterSet`, XML `[81] EncName`, `@charset`, `data:` URLs, XHR, subresources, and 2,543 `encoding/` WPT subtests), but a page that constructs `new TextDecoder('shift_jis')` gets a `ReferenceError`, `XMLSerializer` does not consult `document.characterSet` for an output declaration, and the prescan is one pass over the first 1,024 bytes with no streaming | `src/net/TextEncoding.cpp`, `include/litebird/net/TextEncoding.hpp`, `src/js/DomWindowBindings.cpp`, `src/js/DomBindings.cpp` | ARCHITECTURE.md, Phase 47, section 5; AUDIT.md, finding 53 | The decoder side is done and the `encoding/` pages that need no globals are vendored; what is left is the script API and the encoders (Big5's `U+00CA`/`U+00EA`, `gb18030`'s ranges in the other direction) |
| **A private name is a property whose name begins with `#`.** `obj['#p']` reads and writes a private field from outside its class, and `'#p' in obj` is `true`; `o.#p` for an object without the field is `undefined` and assigning to it adds one, where both are a `TypeError`; one class reads another's `#p`; `#p in o` is a `SyntaxError` | `src/js/JsParser.cpp`, `src/js/JsEval.cpp`, `src/js/JsValue.cpp` (`is_hidden_key`) | ARCHITECTURE.md, Phase 43, section 8 | A private name is an identity made when the class is evaluated, not a string: two classes that each have a `#p` have two names. The same question as the row on the engine's `%` slots, and the same files: not beside it |
| **A direct `eval` is wrong in four places that test262 has**, and ten of its tests that passed before Phase 65 do not pass since. (1) `eval?.('a')` is an indirect eval and is run as a direct one: `function fn() { var a = 'local'; return eval?.('a'); }` gives `local`. (2) `eval('super()')` in a method of an object literal is a `SyntaxError` before anything runs, and is a `TypeError` here. (3) A `var` that a direct eval declares inside a block stays in the block: in a function whose `x` is 1, after `for (let [_ = 0] of [[eval('var x = 2')]]) {}` or `try { throw 0 } catch (e) { eval('var x = 2') }`, `x` is still 1. (4) The name of a function expression can be assigned to in its body: `var r = function F() { F = 1; return typeof F; }` gives `number`, with eval or without; the tests that reach it through eval passed while an eval was global code | `src/js/JsEval.cpp`, `src/js/JsParser.cpp`, `src/js/JsInterpreter.cpp` | [`docs/phases/65-direct-eval-scope.md`](docs/phases/65-direct-eval-scope.md) | Found on 18 November by running test262 on `main` before and after #79: `language/eval-code/direct/super-call-method.js`, `language/expressions/optional-chaining/eval-optional-call.js`, the four `named-…reassign-fn-name-in-body-in-eval.js` (function, generator, async function, async generator), `scope-head-var-none.js` under `for-in` and `for-of`, `switch/scope-var-none-case.js`, `try/scope-catch-param-var-none.js` |
| **Several built-ins still do not make the subclass they were called on.** With `class List extends Array {}`, `List.from([1])`, `List.of(1)` and `new List().map(f)` are plain arrays; `P.resolve(1)` for a subclass of `Promise` is a plain promise. There is no `Symbol.species`, and `instanceof` does not ask `Symbol.hasInstance` | `src/js/BuiltinsArray.cpp`, `src/js/Promise.cpp`, `src/js/JsOps.cpp` (`js_instance_of`) | ARCHITECTURE.md, Phase 34, section 9; three failing cases of `prototypes.dat` | The Map/Set constructor slice landed in Phase 75 / finding 81 (`docs/phases/75-map-set-constructor-dispatch.md`, `docs/findings/81-map-set-constructor-dispatch.md`): it now makes the new.target object first, gets `set`/`add` once, calls it for every input and closes an iterator if it throws. The remaining Array/Promise/species/`Symbol.hasInstance` work is separate |
| **What a function is, in small**: nothing tells a built-in that is a constructor from one that is not (`new Math.max()` makes an object; `class X extends f.bind(null)` and `Reflect.construct(F, [], Date.now)` are accepted; `x instanceof` a function with no `prototype` answers `false` where it should throw, and a bound function is not asked for its target); `Object.getOwnPropertyNames(f)` lacks `length` and `name`, and `f.name = 'x'` takes effect; async and generator functions have `Function.prototype` itself as their prototype, where the language has an object for each kind between the two (three test262 tests, `AsyncFunctionPrototype-is-not-callable.js` and its relatives, passed while `Function.prototype` was not callable and fail since Phase 34); a property of a prototype that cannot be written does not stop an assignment to the object that inherits it | `src/js/JsOps.cpp`, `src/js/JsValue.cpp`, `src/js/BuiltinsObject.cpp` (`bind`), `include/litebird/js/Heap.hpp` (`JsObject::not_constructor` is the bit to set) | ARCHITECTURE.md, Phase 34, section 9; four failing cases of `prototypes.dat` | Six host constructors have no `prototype` (`Response`, `Headers`, `Worker`, `WebSocket`, `IntersectionObserver`, `ResizeObserver`): give them one before `instanceof` throws for a missing one |
| **A script can read and write the engine's own slots**: they are properties whose names begin with `%`, hidden from enumeration and from nothing else. After `el.classList` was read, `el['%classList'] = 5` makes `el.classList` a number; `method['%home']` is the object `super` is looked up from, and assigning to it changes what `super` finds. The other side of it: a property of the script's own whose name begins with `%`, `#` or `@@`, or has the shape `__x__`, is left out of `Object.keys`, `JSON.stringify`, `for…in`, `Object.assign` and spread, so `var m = {}; m['#/route'] = 'home'; JSON.stringify(m)` is `{}` | `src/js/DomBindings.cpp` (`slot`, `set_slot`, `clear_slot`), `src/js/JsEval.cpp` (`%home`, `%fn`, `%new.target`), `src/js/JsValue.cpp` (`is_hidden_key`) | AUDIT.md, finding 41 | Move the slots off the property table (Phase 34 moved one: `JsObject::not_constructor`), so that no name is special; refusing such a key from a script would only trade one fault for the other. Not beside a row that changes `src/js/Dom*Bindings.cpp` |
| **Array destructuring reads an array's elements without asking for its iterator.** `var a = [1, 2]; a[Symbol.iterator] = function* () { yield 9; }; var [x] = a` gives 1 where the language gives 9. With the iterator deleted from `Array.prototype`, `var [x] = [1]` still works where it is a `TypeError`. `var [x] = { next() { … } }` takes an object that only has `next` for an iterator. A parameter (`function f([p, q])`) and an assignment pattern go the same way | `src/js/JsEval.cpp` (`IterationCursor`) | ARCHITECTURE.md, Phase 63, section 6; two failing cases of `tests/js-conformance/iteration.dat` | `IterationCursor` has an array mode of its own, chosen by `is_array()`. `js_iterates_as_builtin(value)` (`JsOps.hpp`) is the question to ask in its place: true exactly when reading the elements is what the iterator would give. For the rest, `js_iterator_method(value, method)` and the protocol; a string already goes through `js_iterable_to_vector`. Not done in Phase 63 because `JsEval.cpp` belonged to open pull requests then (#68, #71) |
| **`Array.from` decides for itself what is iterable.** Whatever `is_array()` or is a string it takes for iterable without asking. So with the iterator deleted from `Array.prototype`, `Array.from([1, 2])` is a `TypeError` where the language reads the array as array-like and gives `[1, 2]` (`main` before Phase 63 gave that, by never asking anything), and the same for a string. A number whose prototype was given an iterator is not asked (`Array.from(3)` is `[]`). And it collects every value before it maps the first: a mapper that throws does not close the iterator, and an iterator without an end hangs it where the mapper would have stopped it | `src/js/BuiltinsArray.cpp` (`Array.from`) | ARCHITECTURE.md, Phase 63, section 6; three failing cases of `iteration.dat` | `js_iterator_method(source, method)` is the language's GetMethod: no method means array-like, for an array as for anything else. With a method, map inside the visitor of `js_iterate`, which closes the iterator when the visitor throws. Not done in Phase 63 because `BuiltinsArray.cpp` belonged to an open pull request then (#63) |
| **The default constructor of a subclass spreads its arguments.** For a class that has no constructor the parser writes `constructor(...args) { super(...args); }`, and since Phase 63 a spread asks the array for its iterator. So a script that replaced `Array.prototype[Symbol.iterator]` is called by `new Derived(1, 2)`, once for each default constructor on the way up, where the language hands the arguments on without iterating ("does not use argument evaluation", in the words of the test); a replacement that throws makes every such `new` throw. test262's `language/statements/class/subclass/default-constructor-spread-override.js` passed before Phase 63, when nothing asked an array, and fails now | `src/js/JsParser.cpp` (where the default constructor is made), `src/js/JsEval.cpp` (`eval_arguments`) | ARCHITECTURE.md, Phase 63, section 6; one failing case of `iteration.dat` | Mark the spread the parser makes (a flag on its `SpreadExpr`) and, for a marked one, copy the rest array's elements in `eval_arguments` without `js_iterate`. A constructor that a script writes with the same text has to keep asking. Not done in Phase 63 because both files belonged to an open pull request then (#68) |
| **What an iterator is, in small**: `Map.prototype[Symbol.iterator]` is not `Map.prototype.entries` and `Set.prototype[Symbol.iterator]` is not `Set.prototype.values` (two functions each, and the one under `Symbol.iterator` is named `@@iterator`), and `Set`'s `values` and `keys` are not one function. (For `Array` the two have been one function, named `values`, since #77; the case of `iteration.dat` asks for `Map` and `Set` too, and still fails.) An array iterator has `next` as a property of its own, where the language has one `next` on a prototype that a script can replace, and every kind of iterator has the same prototype (`Object.prototype.toString` gives `[object Object]`). Every built-in iterable also has a property whose name is the text `Symbol.iterator`, which `Object.getOwnPropertyNames` lists; and the symbol itself is a property named `@@iterator`, so `'@@iterator' in []` is true and an object with a property of that name is iterable. `arguments` has no `Symbol.iterator` of its own. An object whose prototype is an array has no `length` and no elements | `src/js/BuiltinsArray.cpp`, `src/js/BuiltinsCollections.cpp`, `src/js/JsValue.cpp` | ARCHITECTURE.md, Phase 63, section 6; five failing cases of `iteration.dat` | The day array iterators take `next` from a prototype they share, `js_iterates_as_builtin` has to compare that function too (the comment in it says so): the direct read is right now because nothing a script can reach stands between the installed method and the elements. Since Phase 63 nothing reads the properties named `Symbol.iterator` except `Array.from` and `IterationCursor` (the rows above) and `has_property` in `JsValue.cpp` |
| **What the DOM corpus cannot ask**: `getComputedStyle(el).width` is still `auto` (used values are available via `getBoundingClientRect()`/`offset*`/`client*` since Phase 36), `document.write`, the SVG DOM (`className.baseVal`, `viewBox`, `getBBox()`: `tests/dom-conformance/open/dates-and-svg.svg`). The style sheet objects landed in Phase 72: `style.sheet`, `document.styleSheets`, `CSSStyleSheet.cssRules`/`insertRule`/`deleteRule`/`disabled`, `CSSRuleList`, `CSSRule`/`CSSStyleRule` (`selectorText`, `style`, `cssText`), and `document.fonts` — but a sheet has no `CSSMediaRule` (an `@media` block is skipped when the sheet is read, so `insertRule('@media …')` is a `SyntaxError`), no `CSSImportRule`/`CSSKeyframesRule`, and no `new CSSStyleSheet()` constructor | `src/js/Dom*Bindings.cpp`, `src/main.cpp`, `src/shell/` | docs/phases/72-cssom.md; ARCHITECTURE.md, Phase 27, section 12 & Phase 36 | jsdom does no layout, so the first needs a reference of its own before it is built; the DOM corpus cannot record the rest either, so the CSSOM is asserted in `tests/test_suite.cpp` (`cssom_sheets_rules_insert_amounts_and_fonts`) |
| **Memory is reclaimed only when a script calls `gc()`**, and no leak audit has been done. One leak is known: a function bound to an object that holds it (`o.b = f.bind(o)`, 2,000 times: 2,000 cells left after `gc()`), and what a pending timer holds. The collector cannot see into what a native function captured. What it costs meanwhile: `for (i = 0; i < n; i++) o = { a: i }` keeps about 1 KB for each object it has dropped (433 MB of resident memory after 400,000), so a loop of three million takes more than 3 GB; a benchmark written for Phase 43 brought a 4 GB machine to a stop that way until it called `gc()` between its loops | `src/js/Heap.cpp`, and everything that allocates script objects | Known gaps, 4; ARCHITECTURE.md, Phase 34, section 9; one failing case of `tests/js-conformance/prototypes.dat` | Touches everything that makes script objects, the DOM bindings included: not beside a row that changes `src/js/Dom*Bindings.cpp` |
| **Five tree-construction cases need three different things.** The parser has had a script hook since a page could be loaded (`HtmlParser::parse_into`, `HtmlTreeBuilder::set_script_handler`), and `JsInterpreter::load_page` runs each script as its end tag is read; `--dump-dom-webplatform` parses statically, so no script runs there. Giving it that path scores **1968/1973**, and what is left is: `document.write` not implemented at all — it is a no-op, so the markup two cases of `scripted_webkit01.dat` write never exists; an insertion point that does not survive `document.documentElement.remove()`, where the two cases of `scripted_foster01.dat` expect the `<b>` or text that follows to be discarded and this engine appends it to the document; and a formatting list that is not re-examined after a script changes a formatting element's attributes, where `scripted_ark.dat` differs by one reconstructed `<font>` | `src/html/`, `src/js/DomBindings.cpp`, `src/shell/` | Phase 76 / finding 82; the cases are the six `scripted_*.dat` | Do not take this row for the hook: there is one, and the shell wires it. `document.write` during parsing is the parser re-entering itself at the insertion point, and that is the row's real size |
| **AVIF, and SVG `<text>`** | `src/render/ImageDecoder.cpp`, `src/render/SvgRenderer.cpp` | ARCHITECTURE.md, Phase 36, Phase 38 & Phase 54; [`docs/phases/57-lossy-webp-vp8-key-frame.md`](docs/phases/57-lossy-webp-vp8-key-frame.md), [`docs/phases/79-animated-webp-frames-on-a-canvas.md`](docs/phases/79-animated-webp-frames-on-a-canvas.md); AUDIT.md, findings 43, 44, 60 & 85; [`docs/findings/63-lossy-webp-vp8-refused.md`](docs/findings/63-lossy-webp-vp8-refused.md) | PNG, JPEG, GIF, BMP, PPM, **lossless WebP** (`VP8L`, all four transforms, meta prefix codes, colour cache), **lossy WebP** (a VP8 key frame: the boolean decoder, the 16x16/8x8/4x4 intra predictors, the 4x4 IDCT and the Walsh–Hadamard transform, the loop filter, plus the `VP8X` container and an `ALPH` plane in either compression method and all three spatial filters), **animated WebP** (`ANIM`/`ANMF`: the frame walk, the key-frame rule, blending and disposal on the canvas — `<img>` shows the first frame, and `WebPDecoder::decode_frames()` hands back every one), and SVG are wired through `ImageDecoder::decode()`. Canvas 2D text is done (Phase 46, finding 52). What is left: AVIF, and SVG `<text>` |
