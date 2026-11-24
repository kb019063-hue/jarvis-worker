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

## Working together: a name, the watch, the landing session (2026-11-23)

Until 22 November every agent landed its own pull request, however often
`main` moved under it. A CI run takes about nineteen minutes and tests a
pull request merged with `main` as it is at that moment; every merge makes
every other finished pull request's green run a run on another tree, and
sends it round again. Of 42 pull requests merged on 20 and 21 November,
half had merged `main` three times or more (#173 sixteen times), and half
waited 80 minutes or more between their first green run and their merge.

On 22 November one session began to land the others' pull requests for
them (a *watch*), at first only those that `main` had beaten twice. That
night showed what the half measure costs. In 3 hours 48 minutes ten pull
requests landed, on 34 CI runs; `main` moved ten times, seven of them by an
agent merging its own. #241 went from its first green run to its merge in
64 minutes and three runs, #238 in 98 minutes and four. Three pull requests
that were finished and green stood unmerged at the end of it, one for an
hour and three quarters: nobody was to take them before their agents had
tried twice and then asked. And the landing session needed six runs for the
three pull requests it landed: three times an agent's own merge moved
`main` under what it was testing.

So since 23 November, **while a watch is on, one session merges and nobody
else does**. It takes every pull request that is finished and green,
without being asked, tests those that are waiting together in one run, and
merges each as it is. No test is given up for that: a pull request still
passes its own CI run before it is taken, and `main` still only ever gets a
tree that a whole run passed on, file for file. What is given up is the
runs that tested a tree nobody was going to merge.

The first watch under that rule began at 05:28 on 23 November. In its
first 67 minutes eight pull requests landed, on two CI runs: six together
(pushed at 05:56, all twelve jobs passed by 06:15, the six merges took a
minute) and two more (pushed at 06:17, passed by 06:34). From the green
run on its own head to its merge a pull request took between 22 and 56
minutes, four of the eight 32 or less, and none of them merged `main` or
ran CI again in that time. `main` did not move under either lot: one agent
merged its own pull request 22 seconds before the first look, and none
after it. What went wrong is in points 2 and 4: three pull requests that
had put their `RUN_TEST` line at the end of `main()` were in conflict
within the hour, and the first lot was started again four times.

1. **Take a name.** One made-up word, yours for as long as your chat lasts.
   A branch is new after every Continue and a pull request's number says
   nothing about who wrote it; the person has many chats and has to know
   which one is speaking, and which one a note is for. Not a model's name:
   two agents can be the same model. See that it is free
   (`gh pr list --state all --search '"Agent: Heron" in:body'` finds
   nothing), and then:
   - `Agent: Heron` is the fourth line of every pull request you open, under
     the three of "Before starting";
   - your name is the first word of every reply to the person.
2. **Leave the suite count of `README.md` alone.** Nearly every pull request
   adds a suite, so nearly every pull request rewrote the same six places:
   each merge put the other finished ones in conflict there, and four times
   git merged two that had written the same number and `main` failed on the
   count (#91, #150, #146, #170). Now `make check-numbers` notes a count that
   is behind and fails nothing, `make metrics` leaves the count as it is
   written, and the landing session refreshes it at the end of a watch. Add
   your suite; do not touch the number. If your branch already changed it
   and now conflicts there, take `main`'s lines.

   **And write your suite as a file of its own:
   `tests/suites/218-a-few-words.cpp`, the phase and what it is about, with
   one `TEST(name) { … }` in it.** Nothing else: no line in
   `tests/test_suite.cpp`, no `RUN_TEST`, no `#include`. The suite puts
   itself into a list, and `make test` runs the files of that directory
   first, then the old suites (`tests/suites/README.md` has the form and
   what the file may use; `python3 tools/ci/check_suites.py` says when a
   file is not in it). Until 23 November every suite was written into the
   one file, in front of `main()`, with a `RUN_TEST` line at the end of
   `main()`, and two pull requests that do that change the same two
   places. In the 90 pull requests before that day their branches merged
   `main` 285 times; 120 of those merges were in conflict, 44 of them in
   that file, at the end of `main()` or in front of it or where two suites
   of one subject had gone to the same place. That morning #244, #264,
   #267 and #273 had each put a line at the end: #264 landed, and within
   the hour the other three were in conflict with it, one sent back out of
   a lot and two caught by `main`. A file that only your pull request has
   is in conflict with nothing. The suites that are in
   `tests/test_suite.cpp` stay there, each with its line, and an assertion
   more for one of them is still written there. (Two pull requests that
   rewrote neighbouring rows of "Open tasks" were in conflict too, as #244
   and #257 were. Since 24 November an empty row stands between the rows:
   "The files every pull request changes", point 4.)
3. **When your pull request is finished, ask whether a watch is on.**
   Finished is: the checks of "What "done" means" pass, `WIP:` is off the
   title, and CI is green on the head commit (wait for it with
   `gh pr checks N`, once a minute; not with `gh run watch`). Then run
   `python3 tools/ci/check_pr.py merge N` and do what its last lines say.
   - **`do not merge: a watch is on …`** The landing session takes #N as it
     is. Push nothing more to it and do not merge `main` into it, however
     far `main` has moved: either would start a run of your own and move
     the head under the one that is testing it. Wait with
     `python3 tools/ci/check_pr.py landed N`, which looks for up to four
     minutes and says one of four things:
     - it has landed: tell the person, and end the turn. Your next task
       begins with their Continue;
     - it is yours again, and what the landing session wrote about it: it
       conflicts, or the run on the lot failed because of it. Do what that
       says and push. When CI is green on the new head it is taken again.
       **When the line says the conflict is in documents only** (a row of
       `AGENTS.md`, a line of `AUDIT.md`): merge `main`, resolve it as the
       line says, and put `[skip ci]` in the message of the merge commit.
       That head is taken at once, with no run of its own: its code is
       the code that was green, and the run on the lot tests the merge;
     - no watch is on any more: go on as in the next point;
     - nothing yet: run it again. After an hour of that, end the turn and
       tell the person that #N is finished and with the landing session.
   - **`ready: gh pr merge N --merge`** No watch is on. Merge it yourself.
   - **`not ready`**, because `main` has moved (it says so only when no
     watch is on): merge `main`, push, and wait for CI, twice at the most.
     The third time leave #N as it is, finished and green, end the turn and
     tell the person: the next watch takes it.

   A title without `WIP:` on a green pull request says "land this", to
   whoever is landing. Keep `WIP:` on until the work is finished, and put
   it back if you have to go on: a pull request with `WIP:` is left alone.
4. **The landing session lands it.** One session does this, for a watch
   that the person starts with a word. It promises no length: the login to
   GitHub can end under it at any moment ("The turn, and what a merge does
   to it", below). Its commands are `tools/watch/watch.sh` (`look`, `st`,
   `cands`, `build`, `push`, `wait`, `land`, `once`, `taken`, `landed`,
   `sendback`; the file's first lines say what each does). At every look,
   five minutes apart or less:
   - `python3 tools/ci/check_pr.py watch --beat "YYYY-MM-DD HH:MM"` writes
     the time of the look into the status comment of the board;
   - `python3 tools/ci/check_pr.py watch` lists what is finished, green and
     mergeable, and for the rest why not;
   - **a pull request with `main` in its head is merged at once**, as it
     is: its own run was a run on what the merge puts on `main`
     (`python3 tools/ci/check_pr.py merge N --landing` says "ready", then
     `gh pr merge N --merge`). Not while a lot is being tested: the merge
     would move `main` under the lot;
   - **the others are a lot.** The landing session's branch, whose pull
     request is titled `WATCH:`, is made `main` with their heads merged
     in, in the order of their numbers, and nothing else: `git merge-tree`
     of `main` and the heads, one after the other, gives the same tree.
     One push, one CI run. `make -j2 all && make -j2 test` in the sandbox
     says in about six minutes what the run will say in twenty about the
     unit suites;
   - **the run passes and `main` has not moved:** each of them is merged as
     it is, in that order, and `main` has, file for file, the tree the run
     passed on. The command is `gh pr merge N --merge --match-head-commit
     SHA --body-file FILE`: the SHA is the head the lot holds, so a head
     that moved a second ago is not merged; and the body of every merge
     but the last has a paragraph that begins `[skip ci]`, so CI runs on
     `main` once for the lot and not once for each (five runs fewer for
     the six of 23 November). Before the last one, `git merge-tree
     --write-tree origin/main` with its head must be the lot's tree. If
     `main` has moved all the same (an agent that did not know of the
     watch), `main` is merged into the lot and the run starts again;
   - **the run fails:** the landing session finds the pull request (the
     job that failed, the sandbox, or half the lot at a time), builds its
     branch again from `main` without that head, and tests the rest. A
     head leaves the lot by a forced push of the landing session's own
     branch, never by a revert: a revert leaves the head commit in the
     history, and GitHub would show #N as merged with nothing of it on
     `main`;
   - **what it cannot land goes back**, with one line on the board and the
     same in #N: `Lander: #N is yours again (head abc1234): why`. That is
     a conflict with `main` or with a pull request before it in the lot,
     or a lot that fails with it. #N is then left alone until its head is
     another commit. Its agent is waiting (point 3) and reads the line
     within a minute. If nobody has answered after half an hour, its turn
     ended before the watch did (it had left #N finished, for a watch to
     take): then it is carried, if it can be (below), or it waits for the
     person, who learns of it from the report at the end;
   - **a pull request that went back for a conflict in documents comes
     back without a run.** Its new head is `main` merged into the head
     that was green, with `[skip ci]`. The landing session checks that
     (the first parent is the head it sent back, the second is on `main`,
     and the merge differs from the one git makes alone only in the
     documents that were in conflict) and takes it into the next lot. On
     23 November four pull requests went back for one row or one line
     each and landed 46 to 80 minutes later, most of that a CI run on a
     merge that had changed no code of theirs;
   - a pull request that becomes finished while a run is going waits for
     the next lot, unless the run is less than five minutes old: then it
     joins, and the run starts again. Twice for one lot at the most: on
     23 November the first lot took a newcomer four times in 28 minutes
     (the runners were busy, and each run had finished two jobs of
     twelve when it was stopped), and #257, green since 05:20, landed at
     06:16 for it, 48 minutes into the watch. The last lot of a watch has
     no next one, so it takes a newcomer while more than half of its run
     is still to come: #267 joined one that was nine minutes old, and
     landed nineteen minutes later;
   - nothing is pushed to anybody's branch, and no pull request is closed;
   - **the end** comes when nothing is finished and nothing is in test,
     not only at the hour the status comment names: a watch that goes on
     with nothing to take keeps the person from the report, and the
     report is how they learn which chats wait for a word from them.
     `python3 tools/ci/check_pr.py watch --off "…"` says what
     landed and what went back, and then the landing session merges its
     own pull request, which holds what no other one does: the suite count
     (`make metrics-suites`) and whatever it carried or wrote. That merge
     ends its access within minutes. Until then, what it writes itself is
     kept out of the lot's tree: committed in a second worktree, merged
     into the branch with `git merge -s ours` so that it is pushed, and
     brought into the tree at the end with `git cherry-pick`;
   - **a watch can pause.** Merging its own pull request ends the landing
     session's turn, and need not end the watch. When it has something
     that should be on `main` now (a pull request it carried, a change to
     these rules or to CI), it merges its own pull request without
     `watch --off`: the status comment goes on saying ON, an agent that
     is finished waits with `landed N` as before, and the person's next
     "watch" in the landing session's chat takes up where it stopped. If
     that word does not come, half an hour after the last look every
     agent is told that no watch is on (point 5), and merges its own.

   **Carried.** A finished pull request whose agent is gone, and whose
   conflict needs no judgement (the suite count of `README.md`; a row
   somebody else put where its row is; a `RUN_TEST` line at the end of
   `main()`, where another's now is, in a pull request from before suites
   were files of their own: #244's was moved under that of the suite its
   own is defined after; a number that is on `main` already:
   #223 was opened as Phase 179 and landed as Phase 191), is resolved in
   the landing session's branch and said in #N. It lands when the watch's
   own pull request is merged, and not before: GitHub shows #N as merged
   because its head commit is in what was merged. A conflict in code is
   never the landing session's: #226 and #211 had changed the same four
   files of the JS engine, and which of the two stays was not for it to
   say.

   **Two pull requests that each pass can fail together**, which is what
   the run on the lot is for. On 22 November #176 filled the answer of
   `map` with a `Set` and #193 made a `Set` on a hole ask the prototype;
   with both, `[7].map(f)` called a setter on `Array.prototype`, and a
   suite of Phase 98 failed. The later of the two went back, with what was
   found.
5. **The board is #219**, a pull request that is never merged (an agent
   can comment on a pull request, and can open an issue but not comment on
   it). It holds:
   - the status comment, which the landing session rewrites at every look:
     `Lander: WATCH ON until 2026-11-23 12:00 UTC. Last look 2026-11-23
     05:00 UTC.`, and at the end `Lander: WATCH OFF since …`. A watch is on
     when the last such comment says ON, its end has not come, and its last
     look is at most 30 minutes old. A landing session that has died stops
     looking, and half an hour later every agent is told that no watch is
     on, without anybody having had to say so. (On 23 November the login
     ended in the middle of a watch, and the board went on saying "until
     06:30" for an hour.) Nobody reads that comment by eye:
     `python3 tools/ci/check_pr.py watch` does;
   - the lines that send a pull request back;
   - whatever else one agent has to tell another: one line, your name
     first.

`python3 tools/ci/check_pr.py` shows each claim's name, notes a pull request
without one, and leaves the board and a watch out of the claims.

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
   Agent: <the name you gave yourself; "Working together", above>
   ```

   `P` and `F` come from `python3 tools/next_ids.py`, which reads the
   documents, `docs/`, `docs/state.json` and the open pull requests and prints
   the next free number of each; `--pr-body` prints the four lines above with
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

`README.md`, `ARCHITECTURE.md`, `AUDIT.md`, `Makefile`,
`.github/workflows/ci.yml` and the table below. Two pull requests written at
the same time will both have changed them. (`tests/test_suite.cpp` was the
one they met in most often, until 23 November: a new suite is a file of its
own now, "Working together", point 2.) So:

1. Add a block of your own: a section, a group of tests. Do not re-wrap or
   reorder what is there.
2. **Before merging, merge `main` into your branch.** Resolve, run the
   checks again, push, and let CI pass on that commit. CI that passed on a
   branch behind `main` says nothing about the merge. (`make metrics`,
   below, when your pull request moved a score or a corpus; never for the
   suite count, which is not yours: "Working together", above.)
3. **Add no line to the list "Resolved findings" at the end of
   `AUDIT.md`.** Your finding's file in `docs/findings/` is its entry. The
   list has 80 of the 227 findings: the ones that were moved out of
   `AUDIT.md`, and the ones agents have added by hand since, each at the
   end, so that two pull requests which each added one were in conflict
   there. On 23 November #254 was finished and green and could not be
   merged for that line alone, and went back once more for it during the
   watch. `python3 tools/ci/check_docs.py` notes a line your branch adds.
4. **In the table "Open tasks" an empty row, `| | | | |`, stands between
   every two rows. Leave those rows where they are.** Git merges two
   changes to a file only when a line that neither of them changed stands
   between the two, and a row of that table is one line: until 24
   November two pull requests whose rows touched were in conflict, and in
   the watch of the evening before that held up six of the twenty-one
   that landed (#261, #281, #292, #293, #296, #298), each for another
   round of CI or to the end of the watch. So:
   - rewrite your own row and no other line;
   - a new row goes directly under the row of the task it came from, as
     two lines: an empty row, then the new row. The empty row that stood
     under your row then stands under the new one, between it and the
     next task;
   - `python3 tools/ci/check_rows.py` has the picture, and
     `python3 tools/ci/check_docs.py` notes two rows that touch. The
     landing session puts in a missing one at the end of a watch
     (`check_rows.py --write --open`, which stays clear of the rows that
     open pull requests hold).

### The numbers are generated, not copied

The counts and scores a document publishes are not typed into it by hand any
more. A pull request that moved a score or a corpus refreshes them with one
command:

```bash
make metrics     # python3 tools/ci/check_numbers.py --write
```

Every live figure in `README.md` and `ROADMAP.md` carries an invisible marker
naming it — `**1137 / 1156 (98.4%)**<!--metrics:js/frac_pct-->` — and `make
metrics` rewrites the text in front of each marker from `docs/metrics.json` and
from the tree. Counted things are derived where they are checked: the suite
count is the `RUN_TEST` list of `tests/test_suite.cpp` and the files of
`tests/suites/`, the corpus totals are
the cases on disk. Measured things are in `docs/metrics.json` with the command
that produced them and the day it did, so a figure can be argued with instead
of trusted. `make check-numbers` fails when any marked figure disagrees
(but for the suite count, which it only notes when it is behind, and which
`make metrics` does not write: that is `make metrics-suites`, the landing
session's), and
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
- The `WIP:` comes off the title, and CI is green on the head commit. Then
  `python3 tools/ci/check_pr.py merge N` says who merges ("Working
  together", point 3, above): the landing session while a watch is on, you
  when none is. When it is you: with a merge commit
  (`gh pr merge N --merge`), and the merge comes last: minutes after it
  GitHub is out of reach until the person presses Continue, so CI on `main`
  is looked at when the next turn begins, by whoever has one ("The turn,
  and what a merge does to it", below).
- `python3 tools/ci/check_pr.py merge N` says "ready" right before a merge
  of your own. It asks what is easy to believe and was wrong more than
  once: that `main` is in the head commit, that CI passed on that commit,
  that the numbers are still free. When it has said "not ready" twice
  because `main` had moved, the pull request is left as it is, finished
  and green, for the next watch, not merged into a third time.
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
  Since 22 November one session does the landing, and without merging a
  pull request of its own for each lot ("Working together", above); what
  is said here is still how a pull request that conflicts is carried.
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
  writing one. (Since 21 November the agent that merged is cut off from
  GitHub within minutes and cannot watch: "Nobody sees CI on `main` after
  their own merge", below, says what is left.)
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
- **A run is twelve jobs and about an hour of runner time** (how long it
  takes is two points down).
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

  Until 24 November they started when "Build & unit tests" had passed, and
  each of the eleven jobs that build compiled the whole engine from
  nothing. A run on `main` took 17.6 minutes on the night of the 23rd (run
  45293924439): 5.1 in which ten jobs waited for that one, then the
  longest job, 12.5, of which its sanitizer build was 6.3. The compiling
  was 37 of the run's 75 minutes of runner time.

  Now **no job waits for another**, and **every job that builds compiles
  through ccache**, with the objects of the last run on `main`
  (`.github/actions/ccache`): an object is taken from there only when
  compiling would have made the same one, so a job compiles what its pull
  request changed. The first run without the wait, with nothing to take
  yet, was 13.8 minutes (run 45307390078). A run on `main` saves the
  objects for everybody else's runs, which is one more reason for the
  last merge of a lot to start one. A job that cannot fetch ccache
  compiles everything, as before. Three rules keep it the same checks:
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
- **The merge is the last thing an agent does with GitHub.** Minutes after
  `gh pr merge` the platform cuts the branch off, in the middle of the
  turn. #175 was merged on 21 November at 03:50:48. `gh` answered until
  03:51:29, and the next command, at most four minutes later, was refused
  before any of it ran: "Remote GitHub access is blocked for this branch
  because its pull request was merged or closed. If the Continue button is
  available, use it to start a new branch in this session." The turn had
  not ended, and it had begun with the branch called `active`. After #171,
  the same morning, everything that worked fell in the first 52 seconds,
  and the turn ended there. It has not always come so soon: on 19 November
  #122 was opened from the branch of #118 twenty-nine minutes after that
  was merged, and thirteen branches have a pull request opened after one
  of theirs was merged, the last of them on 20 November. Count on no time
  at all. Everything that needs GitHub comes before the merge: the notes
  in the pull requests you carried, closing what you leave open, the look
  at CI on `main`. After it comes the reply.
  #175 put the opposite here ("GitHub still answers until the turn ends"),
  written from those 52 seconds; its own merge refuted it within five
  minutes, and #179 took it out. If you read it, it was wrong.
- **Nobody sees CI on `main` after their own merge.** A run takes
  fourteen minutes at the least, and the agent that merged is cut off
  before that. #170 was merged on 21 November at 05:07:54, two minutes
  after its CI had passed, on a head without #174, merged at 04:55:49.
  Each had added a unit suite and each README.md said 300, so git merged
  the two without a conflict into a tree with 301 suites and a README.md
  that says 300, and `main` failed at 05:09:56 ("Merged with `WIP:` in the
  title, on a head without `main`", above, is the same accident). It was
  repaired in #179 by an agent continued at 05:12 for something else,
  because the first thing it did was look at `main`. So:
  `python3 tools/ci/check_pr.py merge N` in the minute before the merge,
  since you will not be there to repair what it would have told you; the
  reply says that the run on `main` has not been seen; and every turn
  begins with `gh run list --branch main --limit 3`. Whoever finds `main`
  red repairs it before anything else: look for a repair that exists, and
  open yours as a pull request at once, with "main is red" in its title,
  so that the next agent finds it.
- **A branch that is `pr_merged` stays so until the person presses
  Continue.** A message to it comes with a note from the platform: edit no
  file, commit nothing, and pushes, pull requests and every `gh` call that
  reaches GitHub "will fail and must not be attempted"; files can be read
  and questions answered. (21 November, 03:40, the first message after
  #171. Nothing was tried, as the note says.) It ends when the person
  presses Continue beside the pull request's status, or starts a new
  session where there is no such button, so that is the first thing to
  tell a person who writes to such a branch. And an agent that has merged
  cannot be reached through GitHub by anyone: a note on its pull request
  waits until it is continued, and what it should do next has to be said
  to it by the person.
- **Continue is a new branch, cut from `main` as it is then.** It has
  another name (`arena/` and another ID), and the old one is no longer
  yours to push to. One session has had twelve names since 15 November, so
  an ID is not an agent: two pull requests with different IDs can be one
  agent's, and GitHub does not say. A pull request still open on the old
  branch cannot be pushed to any more. It can be merged as it is, when
  `check_pr.py merge N` says "ready"; or closed; or carried, its head
  merged into the new branch and landed from there ("A finished pull
  request whose session has gone quiet", above). #134 was opened a minute
  after #130 had been merged from the same branch, the turn ended, and it
  was carried in #141; #164, opened a minute after #162, was closed
  unmerged. So no second pull request is opened from a branch whose first
  is merged: one branch, one pull request, one merge.
- **A second task needs a Continue.** Whether another task follows the
  merge is for the person to say, and the agent that merged cannot claim
  one: a claim is a push and a pull request. So the reply after a merge
  says what is merged, and that the next task begins when they press
  Continue.
- **Merging somebody else's pull request does not cut a session off.** On
  22 November a session whose own branch had no pull request merged #201 at
  15:22:43; `gh` and `git push --dry-run` answered at every look for the 27
  minutes it went on looking, and it watched CI on `main` for that merge
  to its end. This is what the landing session of "Working together" rests
  on: it merges the others' pull requests and, until its watch ends, not
  its own.
- **Closing a pull request costs its session the branch, even for two
  seconds.** Closing and reopening a pull request starts a CI run on its
  head merged with `main` as it is at that moment, with no push (#201,
  twice that day; the run's record names the base:
  `gh api repos/OWNER/REPO/actions/runs/RUN --jq '.pull_requests[0].base.sha'`).
  But #219 was closed at 15:41:53 and reopened at 15:41:55 by its own
  session, which went on working for eight minutes, and the person then
  found its chat marked "pr closed" and pressed Continue. So nobody closes
  another agent's pull request to have it tested. Two more limits of that
  way: a head commit with the skip marker gets no run when it is reopened
  either, and a pull request that conflicts with `main` gets none at all.
- **The login to GitHub is the person's, one for all sessions, and it
  ends without warning.** A turn can last longer than it: one waited, in
  calls of ten minutes, for 8 hours 14 minutes on 21 November and ended
  itself; its last write that worked was 7 hours 59 minutes in, and ten
  minutes later every write and every push was refused ("Requires
  authentication"). That looked like eight hours from the start of a turn.
  It is not: on 23 November at 02:46:40 every command of every session
  failed at once (`Bad credentials`, HTTP 401, from `gh`, reading included;
  `could not read Username` from `git`), 3 hours 48 minutes into the
  landing session's turn. No session wrote anything for 22 minutes (the
  repository's events), until the person had logged in to GitHub again. No
  session can see how long the login still has. When it ends: stop, tell
  the person that the connection to GitHub has to be made again, and say
  what is left undone; what was pushed is safe. (The record of 21 November
  is a comment on #179.) A single call that waits more than five minutes
  is sometimes cut short without the turn ending: twice in some fifty.
- **An agent can open an issue and can neither comment on it nor edit
  it** (#196: the session that opened it could not correct it). It can
  comment on a pull request and rewrite its own comment
  (`gh api -X PATCH repos/OWNER/REPO/issues/comments/ID -F body=@FILE`),
  so what agents have to tell each other is said on one: the board.
- **All sessions are one GitHub user, and that user's REST limit is 5,000
  requests an hour for all of them together.** On 23 November at 02:30,
  with nine agents' pull requests open and most of them waiting for CI, it
  was used up (`gh api rate_limit`: `core`, 5000 of 5000). For the sixteen
  minutes to the reset, every `gh run view`, `gh run list` and
  `gh api repos/…` of every session answered `HTTP 403: API rate limit
  exceeded`. That is not the block a merged pull request brings ("Remote
  GitHub access is blocked for this branch"), and it says nothing about the
  pull request. What goes through GraphQL has a limit of its own (23 of
  5,000 were used at that moment) and went on working: `gh pr checks N`,
  `gh pr view N --json statusCheckRollup`, `gh pr list`, `gh pr comment`
  and `gh pr merge`; so did `git fetch` and `git push`. So wait for CI with
  `gh pr checks N`, once a minute: not with `gh run watch`, which asks
  again every few seconds, and not with a loop over `gh run view`.
  `gh api rate_limit` says what is left and costs nothing.

**The sandbox**

- **It can be replaced in the middle of the work.** What comes back is the
  branch's base with the files of the last turn on top, `/tmp` and the build
  directories gone, and some binary test files damaged (seven under
  `tests/wpt/encoding/`). Do not commit that tree: `git fetch origin
  '+refs/heads/*:refs/remotes/origin/*'`, then `git reset --hard
  origin/BRANCH`. So commit and push after every step, and keep nothing that
  matters only in `/tmp`.
- **The clone is shallow: `git fetch --unshallow origin` before a merge, a
  `git merge-base` or a `git log`.** It takes seconds. This file used to say
  `git fetch --deepen=60 origin main`, and sixty commits do not reach the
  place where a branch left `main` once `main` has moved further than that.
  Git then cannot find the common commit, and says so in words that mean
  something else: "refusing to merge unrelated histories", or that a commit
  of `main` "is not an ancestor". On 22 November the agent of #226, 83
  commits behind, read that as `main` having been rewritten, and said so on
  the board. It had not been: the commit it missed, `e959811`, is the merge
  of #221 and is on `main` (`git log --first-parent origin/main`). When
  `main` looks rewritten, look at `git rev-parse --is-shallow-repository`
  first.
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
- **When you merge, correct what counts things**: the floors, and the rows
  of the table below that say your pull request holds a file. Not the suite
  count of README.md any more: since 22 November it is the landing
  session's ("Working together", above).

## Reading the queue

`ROADMAP.md` lists the arc from here to a Chromium-class engine in priority
(dependency) order, with rough sizes. **Priority is information, not force:
nothing in it makes any item compulsory, and agents pick whatever they like
from this table or from that file.** This table is the atomic claim-now queue;
that file is the map. If the two ever disagree about what is open, this table
wins — fix that file in the same PR.

## Open tasks

Rows that share files should not be in work at the same time.

An empty row stands between every two rows, and stays there: it is what
lets two pull requests change neighbouring rows without a conflict ("The
files every pull request changes", point 4).

| Task | Files | Described in | Note |
| :--- | :--- | :--- | :--- |
| **Pixels are measured since Phase 120** (`docs/phases/120-pixels-nobody-measured.md`, finding 121): `tools/conformance/render_check.py` renders each page in `tests/render/reftests.json` and decodes the PNG with **its own reader** (all five scanline filters, per-chunk CRC) so the engine is not asked to grade its own writer; `make render-conformance`, and a CI job. 10 pages, 30 assertions, exact equality because box edges are not anti-aliased. The harness self-tests first and was shown to fail: a one-column bug in `BitmapRenderer::fill_rect` reddened 9 of 10. Extended by Phase 122 (`docs/phases/122-the-oracle-reaches-layout.md`, finding 127) to **36 pages, 133 assertions** (Phase 134 added six and fixed three; Phase 135 added two and fixed two): flex main axis (`justify-content` `space-between`/`center`/`flex-end`, `flex-direction: column`, `flex-wrap`), grid tracks and `gap`, floats and `clear`, absolute positioning, `overflow` clipping, a data-URI image, `border-radius` (anti-aliased, so the corners are asserted `not-colour`) and `transform` (the ink moves, the layout does not). Each page may carry a `known_failure` string; the run prints `KNOWN` with the reason and does not count it as a pass, and an entry on a page that **passes** prints `FIXED` and exits 1, so the corpus shrinks rather than accumulating excuses. Phase 193 adds three inline-image reftests, with 47/48 passing and one known failure: `white-space: nowrap` still allows inline replaced elements to wrap. Chromium 153 agrees with all 11 new assertions. Phase 211 (`docs/phases/211-table-and-inline-pixel-oracle.md`, finding 215) adds 15 CSS 2.1 reftests (65 assertions) for table-cell backgrounds/layers/spacing/spans (6 pass, 3 `known_failure`: `tbody` and `<col>` backgrounds unrendered, shorter sibling cell does not stretch to row height) and inline formatting (`text-align`, `line-height`, `inline-block`, `vertical-align`, `white-space: pre`, inline `<span>` horizontal margin/padding: 2 pass, 4 `known_failure`), bringing `tests/render/reftests.json` to **55/63 passing, 8 known** (104 pages / 386 assertions across both corpora). Phase 238 (`docs/phases/238-css-tables-and-inline-formatting.md`, finding 242) fixes `tbody`/`thead`/`tfoot` row-group box sizing, `rowspan >= 1` table cell height stretching, `white-space: nowrap` & `pre`/`pre-wrap`/`pre-line` parsing/wrapping, `vertical-align: top`/`bottom`/`text-top`/`text-bottom`, `display: inline-block` horizontal IFC flow & atomic painting, and inline horizontal `margin`/`padding`/`border` cursor advance, clearing 6 `known_failure` reftests (`inline_nowrap_keeps_replaced_elements_on_one_line`, `table_layers_cell_over_row_over_tbody_over_table`, `table_cell_background_fills_row_height_of_taller_sibling`, `inline_block_boxes_flow_horizontally_and_wrap`, `inline_vertical_align_top_and_bottom_on_replaced_elements`, `inline_span_horizontal_margin_and_padding_offset_child`) and bringing `tests/render/reftests.json` to **62/66 passing, 4 known** and `tests/render/body-origin.json` to **36/41 passing, 5 known** (clearing `inline_body_honors_its_default_horizontal_margin`, 107 pages / 395 assertions across both corpora; only `<col>` backgrounds, `white-space: pre` newline preservation in `LayoutTree.cpp`, AVIF picture decode, and WebGL remain known in `reftests.json`). Paint order left the list in Phase 135, which is the ledger working: the float and `transform` pages had carried reasons since Phase 122 and passed the moment the display list started emitting in Appendix E order. Still open: text-glyph line breaking / bidi inline runs, collapsed table borders, SVG and canvas visual comparisons are incompletely measured; **no WPT reftests are vendored** (`find tests/wpt -name '*-ref*'` is 0); glyph *shape* is not compared, only ink position; Phase 207 (finding 211) adds `make diff-render` to compare full RGBA screenshots between two builds for either corpus or a single page, without making a conformance claim | `tools/conformance/render_check.py`, `tests/render/`, `Makefile`, `.github/workflows/ci.yml` | `docs/phases/120-pixels-nobody-measured.md`, `docs/findings/121-a-pixel-was-never-compared.md` | Adding a reftest is one entry in `tests/render/reftests.json` and needs no engine change, so this row is a good one to extend rather than a claim to take |
| **Paint order is Appendix E since Phase 135** (`docs/phases/135-paint-order-is-appendix-e.md`, finding 140): the row above was added by Phase 122 because `DisplayList::build_recursive` painted in document order, so a float came out under the background of the block after it and a transformed element under a later sibling's ink. `src/render/PaintCommand.cpp` now walks the seven steps of CSS 2.1 Appendix E — own chrome and ink, negative `z-index` stacking contexts, in-flow block backgrounds, floats, inline content, then the box's own stacking contexts ascending by `z-index` — with one function per step (`paint_atom`, `paint_block_backgrounds`, `paint_floats`, `paint_inline_contents`, `paint_stacking_contexts`). **Still open:** a box that clips or establishes a stacking context is painted as one atom, because a flat display list cannot interleave two phases across a clip push/pop, so phases are honoured inside such a box and not across it (finding 140 §4); `inline-block`/`inline-table` internal phases are not separated from their parent's; negative `z-index` is hoisted to the nearest atom rather than the nearest stacking context; `overflow: auto` still scrolls the viewport. The engine's own suite found the one regression (`canvas2d_and_html_paint_arabic_cursive_and_contextual_glyphs` counted two identical `DrawText` commands), and the pixel oracle printed `FIXED` against the float page the moment it started passing | `src/render/PaintCommand.cpp`, `tests/render/reftests.json` | `docs/phases/135-paint-order-is-appendix-e.md`, `docs/findings/140-the-display-list-had-no-phases.md` | Done. The four remaining `known_failure` pages are the cross-axis row and the rounded clip |
| | | | |
| ~~**`align-self` and `align-content` are not read, and `justify-content` is ignored in a column container**~~ **Done in Phase 180 (`docs/phases/180-flex-and-grid-box-alignment.md`, finding 184):** added `AlignSelf` plus `align_self`, `justify_items`, `justify_self` in the 3 padding bytes of `ComputedStyle` (zero size growth), parsed them alongside `place-items`/`place-self`/`place-content` and `-webkit-*` aliases, fixed `ComputedStyle::hash()` to include grid and item alignment, implemented `align-self` + multi-line `align-content` + `row-reverse`/`wrap-reverse` in row flex, full main-axis `justify-content` + vertical `flex-grow` + wrapping + `align-content` + `align-self` + `column-reverse` in column flex, and `align-self`/`justify-items`/`justify-self`/`justify-content`/`align-content` in grid. Cleared the final two `known_failure` entries in `tests/render/reftests.json` (`flex_column_justify_content_flex_end`, `flex_align_self_overrides_align_items`) and added 7 new alignment reftests (**45/45 pages, 179 assertions, 0 known failures**). **Still open:** `baseline` alignment, `safe`/`unsafe` overflow fallback, vertical column `flex-shrink`, and `auto` margin absorption along flex axes | `include/litebird/css/StyleResolver.hpp`, `src/css/StyleResolver.cpp`, `src/layout/LayoutBox.cpp`, `tests/render/reftests.json`, `tests/test_suite.cpp` | `docs/phases/180-flex-and-grid-box-alignment.md`, `docs/findings/184-align-self-align-content-and-column-justify-content.md` | Closed; the original render reftest corpus now has 0 `known_failure` entries |
| | | | |
| **HTTP authentication is whole except for the parts that need a prompt or a proxy.** Phase 113 read the challenges (RFC 7617 `Basic`, RFC 7616 `Digest`) and Phase 115 (`docs/phases/115-the-rest-of-http-authentications-challenge.md`, finding 120) finished the challenge side: `SHA-512-256` (FIPS 180-4 §6.7, a different function from SHA-512 truncated), `auth-int` over the request body (§3.4.3), `userhash` (§3.4.4, the username field is `H(username ":" realm)` with the algorithm in use), `Authentication-Info`/`nextnonce` (§3.5, with `nc` counted per nonce), `407`/`Proxy-Authenticate` reported as `proxy_auth_schemes`/`proxy_auth_realm`, and a refused credential is **deleted** on a non-stale 401 after an authorized attempt. Phase 162 / finding 167 honors a same-realm Digest `stale=true` challenge: the replacement nonce is retried once without discarding the credential; a repeated stale response is bounded and preserves it. Two bugs in what Phase 113 had already shipped were fixed: `-sess` folded A1 with MD5 whatever the algorithm was (`SHA-256-sess` answered `1dfbe580…` where it should be `1eba72f3…`), and the loader ranked `SHA-512-256` as if it were `SHA-256` by prefix match. Phase 184 ([`docs/phases/184-a-digest-credential-is-digested-in-nfc.md`](docs/phases/184-a-digest-credential-is-digested-in-nfc.md), finding 188) normalizes the username and password to NFC before they are digested, which RFC 7616 §3.4.1 requires and this client had not done: one credential typed precomposed and typed with a combining accent computed two different responses (`2248d1d2…` and `f2c94816…`), and now computes one. `auth_nfc` runs inside `auth_digest_build`, so A1, `userhash` and the `username` field all see the same bytes, and RFC 7616 §3.9.1's published example still answers `8ca523f5…`. **Still open:** no prompt (credentials come from `ResourceLoader::set_auth_credentials`), no proxy support (`Proxy-Authorization` is never sent, so a 407 is reported and the load fails), and the store holds UTF-8 octets with any `charset` other than `UTF-8` refused; `Basic` is not normalized, because RFC 7617 asks for none | `src/net/ResourceLoader.cpp`, `include/litebird/net/ResourceLoader.hpp`, `tests/test_suite.cpp` | `docs/phases/113-http-authentication-challenges.md`, `docs/phases/115-the-rest-of-http-authentications-challenge.md`, `docs/phases/162-digest-stale-retries.md`, `docs/findings/117-a-401-was-just-a-failed-load.md`, `docs/findings/120-a-published-vector-can-be-wrong.md`, `docs/findings/167-stale-digest-nonce-is-not-a-refusal.md` | The digests are held to published vectors and to an independent implementation, and the wire is a loopback server this repository starts. **RFC 7616 §3.9.2 as printed is wrong** — erratum 4897; the suite asserts the corrected pair and says why |
| **General ES-module semantics and transferable messaging remain open.** Phase 66 gives module workers a MIME-checked static graph subset (default/named/namespace imports; simple declaration/default/named exports; named re-exports) and gives Worker/Window/same-agent `MessagePort` neutral graph packets for primitives, ordinary objects and arrays. It deliberately has no live bindings, cycles, `export *`, dynamic `import()`, `import.meta`, top-level `await`, module `<script>` support, typed arrays/`ArrayBuffer`, transferable ownership handoff, transferred/cross-agent ports, or `messageerror`; functions, symbols, DOM/host/exotic values and non-empty transfer lists reject with `DataCloneError` | `src/js/Worker.cpp`, `src/js/JsOps.cpp`, `src/js/DomWindowBindings.cpp`, `src/js/DomBindings.cpp` | Phase 66 / finding 72 (`docs/phases/66-es-module-workers-and-structured-clone.md`, `docs/findings/72-es-module-workers-and-structured-clone.md`) | Phase 66 also makes the process-wide symbol/interface registries process-lifetime allocations, retaining Phase 40’s exit-order safety while removing their static-destructor race | **`export * from './mod.js'` done in Phase 105 / finding 111** (`docs/phases/105-export-star-from-a-module.md`): the star is recorded in `ModuleTransform::star_exports`, the source module's namespace is republished beneath the module's own names, `default` is not republished and a nearer declaration wins. A namespace property is now an enumerable getter that resolves when it is read, not a copy made when the wrapper finished. Still open: **two evaluations of one module are two instances**, so an imported function increments its own copy of an exporter's variables (`import { n, bump }` answers 1, 1 where the language has 1, 2); that is asserted in `module_worker_static_graph_loading_and_refusal` and is Phase 66's missing module-record machinery, not the star's. `export * as ns from` has its own message and is still refused |
| **The exception a `DOMException` is made in is the receiver's realm**: `domparsing/innerhtml-05.xhtml` stays listed as `test-checks-parent-global` because it asks for the caller's realm where `dom/nodes/Document-createElement.html` (240 passing subtests) asks for the frame's. The engine keeps the receiver's, as browsers do; no implementation passes both. `make_dom_exception` asks the node's realm first, then the method's | `src/js/DomBindings.cpp` (`make_dom_exception`), `include/litebird/js/DomBindings.hpp` | [`docs/phases/62-the-realm-of-a-domexception.md`](docs/phases/62-the-realm-of-a-domexception.md); [`docs/findings/68-the-realm-of-a-domexception.md`](docs/findings/68-the-realm-of-a-domexception.md); `tests/wpt/README.md` | Settled by measurement: the candidate change scored 7,759/8,009 with 240 unlisted. Do not re-open without a Chrome, Firefox and Safari run |
| **`TextDecoder` and `TextEncoder` reach script, and the decoder reads in pieces since Phase 163.** Phase 127 (`docs/phases/127-textdecoder-and-textencoder-reach-script.md`, finding 132) installed the two globals over the tables Phase 47 and Phase 110 had already built: all 40 decoders by their 228 labels, `decode()` taking an `ArrayBuffer` or any view, `fatal`/`ignoreBOM`, BOM stripping, `encode()` answering a real `Uint8Array`, and `encodeInto()` writing into one. Phase 163 ([`docs/phases/163-textdecoder-in-pieces.md`](docs/phases/163-textdecoder-in-pieces.md), finding 168) added `{stream: true}`: the bytes a chunk ends inside of are held back and put in front of the next chunk, the BOM is sniffed once per stream (a second one is data, a split one is waited for), a fatal stream that ends inside a character throws on the flush, and ISO-2022-JP's mode — chosen by an escape sequence — crosses the chunk through `decode_stream_chunk`. Phase 194 ([`docs/phases/194-the-byte-order-mark-belongs-to-a-utf-decoder.md`](docs/phases/194-the-byte-order-mark-belongs-to-a-utf-decoder.md), finding 198) took the BOM back out of the byte layer, where it had been a prefix check run for every encoding: §7.1 skips the first *scalar value* when it is U+FEFF, and only a UTF-8 or UTF-16 decoder can produce one, so `FE FF` is U+FFFE to a UTF-16LE decoder and `windows-1252`'s `EF BB BF` is `ï»¿`. `strip_byte_order_mark` now runs on the decoded string and `do_not_flush`/`restart_decoder` are §7.2's flag and first step, so a one-shot call sniffs its BOM again, a fatal error resets the decoder, and ISO-2022-JP's escape mode no longer outlives the call it was set in. What is held back is decided by scanning the chunk *forward*, one character at a time: a backwards walk over trailing bytes is wrong for EUC-KR and Big5, where a trailing byte can be a lead. Phase 209 (`docs/phases/209-the-cut-that-was-two-cuts.md`, finding 213) found the streaming oracle itself failing: `pending_tail` decided how many bytes to hold back by scanning from ASCII on every call, discarding the escape-chosen mode that `decode_iso2022_jp` had correctly carried, so a chunk cut between a double-byte pair's lead and trail byte after an earlier cut had left the decoder in the double-byte mode assembled the pair from the wrong two bytes — 596 of 2450 partitions of 19 byte strings differed from the one-shot answer, now 0, and the suite walks every partition (it fails with `got 596, expected 0` on the unfixed decoder). **Still open:** Phase 206 ([`docs/phases/206-the-codecs-inside-a-worker-scope.md`](docs/phases/206-the-codecs-inside-a-worker-scope.md), finding 210) settled the last of it: both globals were already in a worker's scope, because the install runs from `JsInterpreter`'s setup and a worker builds its own interpreter, and `text_codecs_are_installed_in_a_worker_scope` now runs nineteen probes inside a classic worker and answers them with `postMessage` — guarding the install with `if (m_document)` makes every one of the nineteen differ, which no suite did before. **Still open:** `TextEncoder` has no `stream` option either, and would gain nothing from one (`encode()` answers UTF-8 only, where a lone surrogate is always `U+FFFD`); the `<meta charset>` prescan is still one pass over the first 1,024 bytes; `TextDecoderStream`/`TextEncoderStream` exist in neither scope; and no suite runs the codecs in a module or a nested worker | `src/js/TextCodec.cpp`, `src/net/TextEncoding.cpp`, `include/litebird/net/TextEncoding.hpp` | `docs/phases/110-the-other-way-through-the-encoding-tables.md`, `docs/findings/116-a-decoder-is-not-an-encoder.md`, `docs/phases/127-textdecoder-and-textencoder-reach-script.md`, `docs/phases/163-textdecoder-in-pieces.md`, `docs/findings/168-a-decoder-could-not-be-fed.md`, `docs/phases/194-the-byte-order-mark-belongs-to-a-utf-decoder.md`, `docs/findings/198-the-bom-was-a-byte-prefix.md`, `docs/phases/206-the-codecs-inside-a-worker-scope.md`, `docs/findings/210-the-worker-case-was-unasserted.md`, ARCHITECTURE.md Phase 47, section 5; AUDIT.md, finding 53 | The encoder is done and the interface is done. The stream's oracle is the engine's own whole decode — every **partition** of a byte string must answer what one call answers, and one cut is not every partition: the defect Phase 209 fixed needed two cuts, in a particular order, and was invisible to all of the single ones — because Node's ICU tables differ from the standard for some Big5 and gb18030 pointers, so another engine is not a reference for *those*, only for the BOM, error-mode and cut-invariance rules. |
| **Big5's single bytes are errors, and the sweep that said otherwise was wrong since Phase 245** ([`docs/phases/245-the-sweep-read-three-decoders-as-one.md`](docs/phases/245-the-sweep-read-three-decoders-as-one.md), [finding 249](docs/findings/249-big5-0x80-is-an-error-and-the-sweep-was-wrong.md)): a byte sweep over `0x80..0xFF` using Node's `TextDecoder` as the reference reported that `decode_big5` wrongly maps `0x80` and `0xFF` to U+FFFD, and a `0x80 -> U+0080` branch was written to match. It was checked against `whatwg/encoding`'s `encoding.bs` and is false: Big5's decoder has **three** steps where the sweep assumed four — ASCII is itself, `0x81..0xFE` is a lead byte, and **everything else returns error**. 0x80 is neither, so U+FFFD is the standard's answer and the engine was right all along. The clause the sweep expected lives in the **neighbouring** decoders: Shift_JIS step 4 says "ASCII byte **or 0x80**", gb18030 step 5 says "If byte is 0x80, then return U+20AC (€)" — **both already correct in this engine**, which is exactly why a sweep across the three manufactures a defect in the one that is right. Two more witnesses agree: the vendored corpus is silent (`big5_errors.html` has eight byte sequences and **no 0x80 at all**, so `make wpt` cannot move and its not moving proves nothing), and the Big5 **encoder** refuses U+0080 (encoder step 2 gives a byte only for an ASCII code point, and U+0080 has no index entry) — a decoder answering it would emit a character this engine cannot write. **No engine change:** `src/net/TextEncoding.cpp` is untouched and the drafted branch was reverted. **Still open:** the table row that asked for the fix describes correct behaviour and should be struck; a byte sweep should compare against the index tables already vendored here (`kSingleByte`, `kIndexBig5`, …) and not against ICU, which is also wrong on `koi8-u` (`index-koi8-u.txt` pointers 46/62 give U+045E/U+040E, Node gives U+255D/U+256C) and `windows-1253` (no pointers 42 or 82, so 0xAA and 0xD2 are errors, which Node accepts); and whether some *other* decoder disagrees with its own index while both ICU and the corpus are silent is a real question this does not answer. | `tests/suites/245-big5-single-bytes.cpp` (no `src/` change) | [`docs/phases/245-the-sweep-read-three-decoders-as-one.md`](docs/phases/245-the-sweep-read-three-decoders-as-one.md), [`docs/findings/249-big5-0x80-is-an-error-and-the-sweep-was-wrong.md`](docs/findings/249-big5-0x80-is-an-error-and-the-sweep-was-wrong.md); WHATWG Encoding §11.1.1 (Big5), §12.3.1 (Shift_JIS), §10.2 (gb18030) | The suite fails in **three** wrong directions, which is what a guard for this mistake needs: the task's own fix (`0x80 -> U+0080`), ICU's answer for 0xFF (`0xFF -> U+F8F8`), and a "unified" dispatcher passing 0x80 through — the first two fail the same assertion, so a check that only covers one is satisfied by the other being absent. **When a corpus has no case for a byte, a green run is silence, not agreement** |

| ~~**`Promise` still does not make the subclass it was called on.**~~ **Done in Phase 196 ([`docs/phases/196-a-promise-subclass-makes-its-own-promises.md`](docs/phases/196-a-promise-subclass-makes-its-own-promises.md), finding 200 [`docs/findings/200-a-subclass-promise-had-no-state.md`](docs/findings/200-a-subclass-promise-had-no-state.md)):** `NewPromiseCapability` (§27.2.1.5) with `GetCapabilitiesExecutor`, `SpeciesConstructor` asked by `then`, `PromiseResolve` (§27.2.4.7.1) answering a promise of the same constructor unchanged, and the promise's state reachable from the **cell** as well as from the value (`JsObject::internal` on `ObjectClass::Promise`), because `super(...)` hands the native parent the object `new` made and `take_over` copies what a cell holds. `new P(function(){}).then` works, `P.resolve`/`P.reject`/`P.all`/`P.allSettled`/`P.any`/`P.race`/`P.withResolvers` and `p.then`/`catch`/`finally` all answer `P`, and `Promise[Symbol.species]` has a reader. `built-ins/Promise` in test262 went **470 → 546 of 732**. Three engine bugs surfaced behind it and are fixed: `current_this()` is a reference into the call context, so a native that hands it to a call had its receiver overwritten (the class constructor's `%fn` became an object and `super(...)` threw *'super' keyword unexpected here*); `settle_through` settled a rejection through `[[Resolve]]`, so a rejection passing through a `then` with no handler arrived at the next `then` as a value; and a capability's derived promise shared the source's `PromiseData`. `tools/conformance/verify_promise_subclass.py` (CI-wired) runs 22 programs through the engine and Node.js and requires the two to agree. **Still open:** `Promise.try` (15 cases) and `allKeyed`/`allSettledKeyed` (89) are absent features; `await` and async functions do not consult a subclass's species; and the collector's `use_count() > 3` baseline for a promise cell's reactions is a conservative heuristic, not a proof (finding 200 §4) | `src/js/Promise.cpp`, `include/litebird/js/Promise.hpp`, `src/js/BuiltinsObject.cpp` | [`docs/phases/140-a-subclass-is-made-by-the-builtin-it-was-called-on.md`](docs/phases/140-a-subclass-is-made-by-the-builtin-it-was-called-on.md), [`docs/findings/145-species-was-never-asked.md`](docs/findings/145-species-was-never-asked.md), [`docs/findings/200-a-subclass-promise-had-no-state.md`](docs/findings/200-a-subclass-promise-had-no-state.md) | Closed for the subclass question; the reaction leak in the row below is still the GC row's |
| **A write to an index of an array does not ask the prototype for a setter, and `sort` does not read and write in the standard's order.** Phase 116 ([`docs/phases/116-the-methods-that-move-places.md`](docs/phases/116-the-methods-that-move-places.md), finding 125) did what this row used to ask for: `reverse`, `sort`, `shift`, `unshift`, `splice`, `fill` and `copyWithin` go place by place through an accessor on an index, `toReversed` and `toSorted` write `undefined` where there was a hole, and `toReversed` reads from the last place down (the row's examples, run on `main` on 20 November, print what Node.js prints). Its section 4 leaves two things that are no other row's. With `Object.defineProperty(Array.prototype, "0", { set(v) { … }, get() { return "p" } })` and `var a = []`, `a[0] = 5` writes the element: the setter is not called, `a.hasOwnProperty(0)` is `true` and `a[0]` is `5`, where the language calls the setter and `a` has no such property (`a[0]` is `"p"`); the write-up names two test262 cases that need it, `shift/set-length-array-is-frozen.js` and `unshift/set-length-array-is-frozen.js` — Phase 249 found the same gap in `push` and `unshift`, whose four `set-length-array-*` siblings install the setter mid-call so that it makes the array's `length` read-only, and those four are still the open half of that phase ([`docs/phases/249-array-writes-that-must-refuse.md`](docs/phases/249-array-writes-that-must-refuse.md), §3). And `sort` does not do its `Get`, `Set` and `DeleteProperty` in the standard's order when `Array.prototype` has accessors: `sort/precise-prototype-accessors.js`, `precise-comparefn-throws.js`, `precise-prototype-element.js` | `src/js/JsValue.cpp` (`set_property`), `src/js/BuiltinsArray.cpp` | [`docs/phases/116-the-methods-that-move-places.md`](docs/phases/116-the-methods-that-move-places.md), section 4; [`docs/findings/125-reverse-ran-over-the-accessor.md`](docs/findings/125-reverse-ran-over-the-accessor.md) | The first is on the path of every `a[i] = v`, not only of the methods: time a loop of writes with both builds, because an array with nothing on its prototype chain must not pay for the question. Section 4 names three more things, and they are other rows or no row: an index that is not writable ("A data property on an index of an array has no attributes of its own"), `Symbol.species` in `splice` ("Several built-ins still do not make the subclass they were called on"), and the copying methods taking the place-by-place form for every array with a hole, which costs time and changes no answer |
| **Array destructuring reads the places since Phase 137** ([`docs/phases/137-a-pattern-reads-the-place-not-the-vector.md`](docs/phases/137-a-pattern-reads-the-place-not-the-vector.md), finding 142): `var [x] = a` took the elements of an array whose iterator is the installed one straight from `array_elements`, so a hole was `undefined` to it whatever the index read as — with an accessor on index 0 whose getter returns `"g"`, `var [x] = a` bound `undefined` where `for (x of a)` and `[...a]` gave `"g"` since Phase 108; with `Array.prototype[1] = "p"`, `var [x, y] = [1, , 3]` bound `undefined` to `y` where the language binds `"p"`. `IterationCursor::pull` now reads each place with `js_array_get`, which is what `a[i]` reads: an element costs what it did, and a hole asks `js_array_holes_read_undefined` before looking an index up by name (ten-place patterns 200 → 197 ms over 200,000 iterations on a dense array, 190 → 201 ms where every place is a hole). String and protocol sources were already right, and a replaced `Symbol.iterator` is still asked | `src/js/JsEval.cpp` (`IterationCursor::pull`, the `Mode::Array` case) | [`docs/phases/108-an-accessor-on-an-array-index.md`](docs/phases/108-an-accessor-on-an-array-index.md), section 4, which left it open; the suite is `array_destructuring_reads_the_place_not_the_vector` | Done: 13 checks, 9 of them failing on `main` at `e1414f6`; 308/308 unit suites, and no corpus moved — js-conformance 1143/1158, DOM 678/678, trees 1973/1973, URL 1188/1188, WPT 9567/9824 |
| **A global declaration is a property of the global object, and now has a descriptor, since Phase 199** ([`docs/phases/199-a-global-declaration-has-a-descriptor.md`](docs/phases/199-a-global-declaration-has-a-descriptor.md), finding 203): §9.1.1.4 makes the global declarations non-configurable data properties of the global object, but this engine keeps them in the global environment and lets the object reach them through `lookup_fallback`. `has_own_property` asks the hook; `descriptor_for` did not, so **39 names** were simultaneously an own property and not one — every constructor, the error family, `eval`/`parseInt`/`parseFloat`/`isNaN`/`isFinite`, `JSON`, `Math`, `Reflect`, `Promise`, the collections, `ArrayBuffer`, `DataView`, `Date`, `RegExp`, the typed arrays, `globalThis`, `undefined`, `NaN`, `Infinity` and the URI functions all answered `Object.getOwnPropertyDescriptor(this, name) === undefined` while `hasOwnProperty` said `true`. That is the exact pair `propertyHelper.js` tests first, which is why 35 test262 cases failed with *Cannot read properties of undefined (reading 'enumerable')* — a message naming neither the property nor the defect. The descriptor is now derived from the binding: a library global is `writable`, non-enumerable, configurable; a script's `var` or function declaration is writable, **enumerable**, non-configurable; `undefined`/`NaN`/`Infinity` are writable-false **and** configurable-false (§9.1.1.4's one non-configurable kind); and `let`/`const`/`class` stay *not* properties of the global object, which `is_lexical` decides. test262 14,969 → **14,980** of 15,686, 0 lost; js-conformance 1,145 → **1,146**; dom-conformance 678/678. **Still open in this family, both diagnosed in the phase document:** `__proto__` has no descriptor because it is not in any property table at all (`get_property` synthesises it) — 12 cases in `built-ins/Object/prototype/__proto__/` plus 7 elsewhere, and giving it a real accessor changes how assignment to `__proto__` resolves; and the 190 remaining `defineProperty`/`defineProperties` cases are **undiagnosed** — the symptom is measured (`verifyProperty` reports *"0 descriptor should not be writable"* and *"`arr.hasOwnProperty("0")` !== true"*), but the first explanation, that an index has no property entry of its own, is contradicted by the source and must not be repeated as fact | `src/js/BuiltinsObject.cpp` (`descriptor_for`), `include/litebird/js/JsInterpreter.hpp` (`Binding::is_lexical`), `src/js/DomWindowBindings.cpp` (the Window exotic that claims the name) | Phase 199 / finding 203; `built-ins/Object/getOwnPropertyDescriptor/*`, `built-ins/*/prop-desc.js`, `tools/conformance/run_test262.py`; suite `global_declarations_have_descriptors` | Two answers to "is this an own property" live in this engine — the property map and the `lookup_fallback` hook — so a third place that asks must be checked for both. **Put a new block before the exotic lookup, not after it**: the Window exotic answers `has()` true for every global name and then describes none of them, so anything appended to the end of `descriptor_for` is silently swallowed (the first version of this fix changed nothing for that reason). A `let` in the global scope must never get a descriptor, or it becomes a property the standard says it is not |
| ~~**A data property on an index of an array has no attributes of its own.**~~ **Fixed in Phase 249** ([`docs/phases/249-array-writes-that-must-refuse.md`](docs/phases/249-array-writes-that-must-refuse.md), [finding 253](docs/findings/253-an-attribute-defined-on-an-array-was-forgotten.md)). `Object.defineProperty(a, "0", { writable: false })` now keeps all three attributes: the element's value and flags are captured before the place is made a hole, the map entry holds them, `has_accessors` makes the map visible to reads and to `hasOwnProperty`, and `Object.getOwnPropertyDescriptor` answers `writable: false`. `length` is the same story — the count stays the value, the map holds the attributes, a `{ writable: false }` definition makes `a.length = 0` and `push` throw, and a `length` value that is negative, fractional or above 2^32−1 is the ArraySetLength `RangeError`. The same phase gave the seven result-building array methods `CreateDataPropertyOrThrow` (§7.3.6) instead of `[[Set]]`, so a species result with a non-writable-but-configurable place is replaced and one with a non-configurable place is a TypeError. **Still open:** the four `set-length-array-is-frozen`/`-length-is-non-writable` cases in `push`/`unshift` need a setter on `Array.prototype[0]` reached through `[[Set]]`, which `set_property`'s array branch does not walk (the row above, and finding 254's work) | `src/js/BuiltinsObject.cpp` (`define_from_descriptor`, `descriptor_for`), `src/js/JsValue.cpp` (the `length` read), `src/js/BuiltinsArray.cpp` (`create_data_property_or_throw`, `fill_species_result`, `copy_places_into`, `dense_path`, `push`, `unshift`) | [`docs/phases/249-array-writes-that-must-refuse.md`](docs/phases/249-array-writes-that-must-refuse.md), [`docs/findings/253-an-attribute-defined-on-an-array-was-forgotten.md`](docs/findings/253-an-attribute-defined-on-an-array-was-forgotten.md), Phase 108, Phase 123, Phase 116 §4 | Eight descriptor probes match Node field for field; `built-ins/Array` 2750 → 2767 with 17 fixed and 0 broken, all sixteen `target-array-with-non-writable-property`/`-non-extensible` cases among them; `make test` 393/393. A splice writes its *receiver* with Set and only its returned array with CreateDataProperty — a blanket rewrite broke two cases and had to be reverted |
| | | | |
| **The refused `delete` and its message are done since Phase 123** (`docs/phases/123-a-strict-delete-refuses.md`, finding 128): a property that is not configurable — and an array's `length`, which `[[Delete]]` used to answer `true` for — answers `false` in sloppy code and is a `TypeError` in strict code with V8's words; `delete name` is answered (a `var` `false`, a configurable global property removed, an unresolvable name `true`, `this` `true`) and is the language's early `SyntaxError` in strict code. Two consequences of it stay open: an index of an array defined through a descriptor keeps no attributes, so deleting one does not throw (the row above), and `arguments`' prototype is `Array.prototype` here, so its message says `#<Array>` where V8 says `#<Object>` | `src/js/JsEval.cpp` (`case JsTokenType::Delete`) | [`docs/phases/108-an-accessor-on-an-array-index.md`](docs/phases/108-an-accessor-on-an-array-index.md), section 4, where it was met | Found while checking `pop` of an accessor that is not configurable, which does throw. Whether the code is strict is known where the operator is evaluated |
| | | | |
| ~~**The four `__defineGetter__` relatives do not exist**~~ **Fixed in Phase 136 (finding 141).** The `Reflect` half of this row landed in Phase 124; Annex B.3.3's `Object.prototype.__defineGetter__`, `__defineSetter__`, `__lookupGetter__` and `__lookupSetter__` are now installed. Definition uses `define_from_descriptor`, preserves the other half of an existing accessor, checks callability before converting the key, and returns `undefined`; lookup walks own descriptors up the prototype chain and stops at the first own property. Node.js v22.22.3 agrees; test262's four directories are 44/54, with all ten remaining cases requiring the still-absent `Proxy` | `src/js/BuiltinsObject.cpp`, `tests/test_suite.cpp` | ECMA-262 Annex B.3.3; test262 `built-ins/Object/prototype/__defineGetter__` and its three neighbours; [`docs/phases/136-legacy-accessor-reflection.md`](docs/phases/136-legacy-accessor-reflection.md), [`docs/findings/141-legacy-accessors-were-invisible.md`](docs/findings/141-legacy-accessors-were-invisible.md) | Closed; remaining test262 failures are the separate `Proxy` feature |
| | | | |
| **An array method no longer copies its receiver, since Phase 129** ([`docs/phases/129-an-array-method-must-not-copy-its-receiver.md`](docs/phases/129-an-array-method-must-not-copy-its-receiver.md), finding 134): `receiver()` used to read index 0 up to `length` before the method looked at an argument, so `{ 0: 9, length: Infinity }` was `std::bad_alloc` or SIGKILL and `{ length: 2**32 }` was an allocation where the standard wants a `RangeError`. test262 `built-ins/Array/prototype` went 1,391 → 1,523 of 1,632, 22 crashes → 0, 132 gained and none lost. **Still open:** 109 tests in those directories fail; `find`/`findLast` do not walk a run of missing places above a 16-million bound (stated in the code); and the methods that *move* elements — `reverse`, `sort`, `splice`, `fill`, `copyWithin`, `shift`, `unshift`, `with`, `toSpliced` — and the iterator objects were not audited and some still loop to `length_of`. **`reduce` and `reduceRight` walk the places as they reach them since Phase 213** ([`docs/phases/213-reduce-asks-for-each-place.md`](docs/phases/213-reduce-asks-for-each-place.md), finding 217, [`docs/findings/217-reduce-listed-the-places-before-it-walked.md`](docs/findings/217-reduce-listed-the-places-before-it-walked.md)): both listed the indices that were present before the first callback ran, so a place a callback created ahead of itself was never visited — `[1, 2, , 4, '5']` with a callback that writes `arr[2]` answered `"75"` where Node.js v22.22.3 answers `"105"` — and `reduceRight`, which §23.1.3.22 walks downwards, could not see a place created *below* itself at all: `[9, , , 9]` filling 1 and 2 from index 3 answered 18 where Node answers 21. `reduce` now uses the same `walk_present_indices` walk as `forEach` and `map`, and `reduceRight` walks `k` down from `length - 1` asking `HasProperty` at each step. `built-ins/Array` **2,726 → 2,745 / 3,083 (+19 / -0)**, `make test` **350 / 350** | `src/js/BuiltinsArray.cpp` | [`docs/phases/129-an-array-method-must-not-copy-its-receiver.md`](docs/phases/129-an-array-method-must-not-copy-its-receiver.md), [`docs/findings/134-an-array-method-copied-what-it-could-not-hold.md`](docs/findings/134-an-array-method-copied-what-it-could-not-hold.md) | Adding a method is one `walk_present_indices` call; the crashes were all in the one `receiver()` helper |
| | | | |
| **A definition names the function it makes, and a spread of a method definition's `name` was `[object Object]`.** Phase 159 (`docs/phases/159-a-definition-names-the-function-it-makes.md`, finding 164) gave §15.4.4/§15.4.5 SetFunctionName its three forms: a symbol key contributes `[` + its description + `]` (and nothing when there is none), an accessor contributes `get `/`set `, and a grouped assignment target (`(f) = function () {}`) is *not* a named evaluation while the initialiser of a pattern/parameter is. **Still open in this family:** an anonymous callable in a *default export* — `export default function () {}` is named `default` by §16.2.3.6, and modules are not parsed here; and `Symbol.prototype.description` on an undescribed symbol (`Symbol()` answers `""` here, the language `undefined` — one line in `BuiltinsObject.cpp`, and test262's `Symbol/prototype/description/*` is the oracle)  | `src/js/JsEval.cpp` (`defined_name`, `name_function`), `src/js/JsParser.cpp` (`explicitly_grouped`), `src/js/BuiltinsObject.cpp` (the symbol description getter) | Phase 159 / finding 164; `language/expressions/object/fn-name-*`, `language/statements/class/definition/fn-name-*` | `defined_name` is the one place the rule lives now; a new definition site calls `name_function` rather than passing a hint to `make_function`, because the parser pre-names every method and a hint only reaches a function with no name. Check the two `evaluate_named` callers in `JsEval.cpp` (`eval_assign` and the default-value paths) before touching it: the assignment half is gated on `explicitly_grouped`, and the parameter/pattern half deliberately is not |
| | | | |
| ~~**`Object.assign` boxes, throws, and copies every own key since Phase 167**~~ **Family closed in Phase 198 (finding 202).** `Object.assign` ([`docs/phases/167-object-assign-boxes-throws-and-copies-every-own-key.md`](docs/phases/167-object-assign-boxes-throws-and-copies-every-own-key.md), finding 169) boxes its target with `ToObject`, throws on refused strict `Set`, and copies own enumerable symbol keys via `assign_keys` (`built-ins/Object/assign` 17/38 → 34/38, remaining 4 need `Proxy`). `Reflect.ownKeys` (Phase 175, finding 179) and `Object.getOwnPropertyDescriptors` (Phase 181, finding 185) report every own string and symbol key without the enumerable filter. **Done in Phase 198 (`docs/phases/198-object-spread-and-rest-copy-symbols.md`, finding 202):** object spread (`{...source}`) and object rest destructuring (`{...rest} = source`) in `src/js/JsEval.cpp` now share `copy_data_properties` (ECMA-262 §7.3.24), snapshotting own string and symbol keys in `[[OwnPropertyKeys]]` order, checking live own enumerability per key, excluding destructured string and symbol keys, and writing via `define_property` (`CreateDataPropertyOrThrow`) instead of `set_property` so target/prototype setters are not invoked. `tools/conformance/verify_symbol_copy_paths.py` 7/18 → **18/18** matching Node.js v22.22.3; test262 default suite 14,969 → **14,978** (+9, 0 lost) and all 17 spread/destructuring directories 14,076 → **14,094** (+18, 0 lost), leaving 0 non-`Proxy` `spread`/`obj-rest` failures | `src/js/BuiltinsObject.cpp` (`assign`, `assign_keys`), `src/js/JsEval.cpp` (`copy_data_properties`) | Phase 167 / finding 169; Phase 175 / finding 179; Phase 181 / finding 185; Phase 198 / finding 202; `tools/conformance/verify_symbol_copy_paths.py` | Closed; remaining test262 `CopyDataProperties` failures require `Proxy`. Do not make `own_keys` report symbols — `Object.keys`, `for...in` and `JSON` would inherit the change |
| | | | |
| **An abrupt completion still finishes the assignment, since Phase 176** ([`docs/phases/176-an-abrupt-completion-still-finishes-the-assignment.md`](docs/phases/176-an-abrupt-completion-still-finishes-the-assignment.md), finding 181): two defects that are one sentence of the standard each. (1) `base.prop = count += 1` with `base` null/undefined threw **before** the right-hand side ran, leaving `count` at 0 — the reference is resolved first, but `PutValue` is what refuses a nullish base, so `resolve_reference` now takes `throw_on_nullish` (false for a plain `=`) and `check_reference_base` makes the same call once the value exists; the eager refusal stays for compound assignment and `++`/`--`, where the base must be usable to read the current value. (2) A pattern that threw while binding left the iterator unclosed — `[ y = thrower() ] = iterable` called `return` zero times — where 7.4.11 requires IteratorClose on an existing abrupt completion, with the close's own throw discarded because the pattern's throw wins. A `next` that throws is **not** closed (7.4.8 step 7), which is why the first attempt at this lost seven cases. `language/expressions/assignment` 435 → **441 of 485**, the whole suite 14,889 → **14,895**, 0 lost; js-conformance 1145/1158 and dom-conformance 678/678 unchanged. **Still open in this family:** most of the remaining `dstr` cases need `generator.return()` to deliver a completion into a **suspended** frame so the frame's own abrupt-completion handling closes the pattern (`function* g() { var v; [ v = yield ] = iterable; }` — `it.return(777)` must call `return` once); that is the generator frame in `src/js/JsEval.cpp`, not the binder, and it is the next step for this row. `array-elem-iter-rtrn-close-null.js` is the same frame problem, and the two `iterator-return-is-not-callable` cases need `BigInt` to parse. | `src/js/JsEval.cpp` (`eval_assign`, `resolve_reference`, `check_reference_base`, `IterationCursor`, `bind_pattern`), `include/litebird/js/JsInterpreter.hpp` | Phase 177 / finding 181; `language/expressions/assignment/*`, `dstr/*`; `tools/conformance/run_test262.py language/expressions/assignment` | `resolve_reference`'s default is the eager refusal, so a new caller that wants the spec's order has to pass `false` and call `check_reference_base` itself. Do not move the check into `write_reference`: a pattern target (`Pattern::Kind::Target`) resolves and writes in one step and there is no value to protect. For the iterator, `close()` reports a bad `return` and `close_quietly()` swallows it — which one a new call site wants depends on whether it is already throwing |
| **What a function is, in small**: ~~nothing tells a built-in that is a constructor from one that is not (`new Math.max()` makes an object)~~ **No longer true — fixed by Phase 158 (finding 163, `docs/phases/158-a-method-is-not-a-constructor.md`).** `js_define_method` now makes every built-in method (and `Math.max`'s own installer, which builds its values first) with `js_make_method`, so a method has no `[[Construct]]`: the 51 test262 `not-a-constructor` cases that ask `isConstructor(f)` passed, and `new Object.keys([])` is a `TypeError`. Both of the examples that named a *method* went with it: `class X extends Math.max {}` and `Reflect.construct(F, [], Date.now)` now throw the language's `TypeError`. **Not open any more (Phase 208): a bound function reached only as a prototype keeps its target.** `Object.create(f.bind(target))` went on calling `f` with the wrong `this` after a collection: the collector counts a payload's captures and bound target, so a payload one handle holds is no root, and `Heap::mark_value` marked the payload's cell and closure but not the two vectors' contents, so the target was freed under a live function. `prototypes.dat`'s "reachable only as a prototype" case (recorded from Node.js) had answered `undefined` since it was written and now answers `bound kept`: js-conformance **1,146 → 1,147 / 1,158**, `prototypes.dat` 70/76 → 71/76, `make test` 355/355. Still open in this row: `class X extends f.bind(null) {}` is accepted where the language refuses it — a bound function is a constructor but has no `prototype` to extend; ~~`Object.getOwnPropertyNames(f)` lacks `length` and `name`, and `f.name = 'x'` takes effect~~ **also no longer true — Phase 146 (`docs/phases/146-a-functions-name-and-length-are-its-own.md`) made both own non-writable properties**; async and generator functions have `Function.prototype` itself as their prototype, where the language has an object for each kind between the two (three test262 tests, `AsyncFunctionPrototype-is-not-callable.js` and its relatives, passed while `Function.prototype` was not callable and fail since Phase 34); a property of a prototype that cannot be written does not stop an assignment to the object that inherits it | `src/js/JsOps.cpp`, `src/js/JsValue.cpp`, `src/js/BuiltinsObject.cpp` (`bind`), `include/litebird/js/Heap.hpp` (`JsObject::not_constructor` is the bit to set) | ARCHITECTURE.md, Phase 34, section 9; four failing cases of `prototypes.dat` | Phase 114 made `instanceof` ask `Symbol.hasInstance` and made a script function with no object `prototype` throw the language's `TypeError`, which is what `x instanceof` a bound function could not be asked before (a bound function delegates to its target now). Four host constructors still have no `prototype` (`Worker`, `WebSocket`, `IntersectionObserver`, `ResizeObserver`; `Headers`, `Response`, and `Request` gained per-realm `.prototype` objects in Phase 173, finding 177): a *native* function with no `prototype` is left answering `false`, so `1 instanceof Math.max` is `false` too. Give those four a `prototype` and the operator can throw for every function that has none |
| **A script can read and write the engine's own slots**: they are properties whose names begin with `%`, hidden from enumeration and from nothing else. (A private name used to be the same kind of hole and is not any more: since Phase 88 it is an identity in a table of its own, which no string key reaches.) After `el.classList` was read, `el['%classList'] = 5` makes `el.classList` a number; `method['%home']` is the object `super` is looked up from, and assigning to it changes what `super` finds. The other side of it: a property of the script's own whose name begins with `%`, `#` or `@@`, or has the shape `__x__`, is left out of `Object.keys`, `JSON.stringify`, `for…in`, `Object.assign` and spread, so `var m = {}; m['#/route'] = 'home'; JSON.stringify(m)` is `{}` | `src/js/DomBindings.cpp` (`slot`, `set_slot`, `clear_slot`), `src/js/JsEval.cpp` (`%home`, `%fn`, `%new.target`), `src/js/JsValue.cpp` (`is_hidden_key`) | AUDIT.md, finding 41 | Move the slots off the property table (Phase 34 moved one: `JsObject::not_constructor`), so that no name is special; refusing such a key from a script would only trade one fault for the other. Not beside a row that changes `src/js/Dom*Bindings.cpp` |
| | | | |
| ~~**`for…of` takes an object that only has `next`.**~~ **The row was never true at this commit — probed and retired in Phase 210 (finding 214, [`docs/phases/210-a-loop-was-never-iterable-through-next.md`](docs/phases/210-a-loop-was-never-iterable-through-next.md)).** Every iteration path in the engine asks the value for its `@@iterator` (`js_iterator_method` throws for a missing or non-callable one), and no fallback to a bare `{ next }` exists in `method_is_installed()`: probed against Node.js v22.22.3 for spread, `for…of`, `yield*`, `Set`/`Map`/`WeakSet`, a pattern and an assignment pattern, an object with no symbol key at all, and a `null`/non-function `Symbol.iterator` — both engines answer `TypeError` to all of them, and `Array.from` reads the source as array-like and answers `0` in both. The two `iteration.dat` cases the row named pass; the three that fail there (the getter-reads-once case, the `@@iterator`-is-a-string case, and the `arguments` own iterator) are separate rows. Pinned by the unit suite `an_object_without_an_iterator_method_is_not_iterable`. | — | `docs/phases/210-a-loop-was-never-iterable-through-next.md`, `docs/findings/214-a-loop-was-never-iterable-through-next.md` | Closed. Probing it found a different iterator-lookup defect, filed as its own row below: `Array.from` reads an iterator getter **twice** per call — retired below, fixed in Phase 225 |
| | | | |
| **`Array.from` asks the source once, and maps as the values arrive.** Phase 94 landed this (`docs/phases/94-array-from-asks-the-source.md`): GetMethod(`@@iterator`) is asked of everything, an array and a string included, and only a source that has none is read as array-like (ToLength of `length`, then each index); the mapper is called as each value arrives, so a mapper that throws closes the iterator and an iterator with no end costs one value. Phase 225 (`docs/phases/225-array-from-reads-the-method-it-has.md`, [finding 229](docs/findings/229-the-iterator-method-was-read-twice.md)) made it ask **once**: the iterable path used to read `@@iterator`, then hand the source to `js_iterate()`, which read it again — a getter for it ran twice for one iteration where a spread and a `for…of` run it once (`iteration.dat` recorded `1,2 1 2 3 4` and answered `1,2 1 2 4 5`). `iterate_with_method` in `src/js/BuiltinsArray.cpp` now runs the protocol with the method already in hand. What is still open: that loop is a copy of `iterate_with` in `src/js/JsOps.cpp` and the two belong together as one exported function (#261 held that file); `js_iterates_as_builtin()` is asked twice on the direct path; the result is always a plain array when `this` is not a constructor, and `Array.from` reads `length` and the indices through the ordinary property lookup with no `Array.prototype` fast path | `src/js/BuiltinsArray.cpp` (`Array.from`, `iterate_with_method`) | Phase 63, section 6; Phase 94 / finding 100 (`docs/findings/100-array-from-decided-what-was-iterable.md`); Phase 225 / finding 229 | `js_iterator_method(source, method)` is the language's GetMethod. `iteration.dat` has the four cases Phase 94 added and the once-per-iteration case this phase fixed, all recorded from Node.js. The new suite is `array_from_reads_the_iterator_method_it_already_has`, and it fails on the tree without the fix |
| **The default constructor of a subclass no longer iterates its arguments** (Phase 160, `docs/findings/165-a-default-constructor-asked-an-iterator-it-never-had.md`). For a class with no constructor the parser writes `constructor(...args) { super(...args); }`, and since Phase 63 a spread asked the array for its iterator, so a script that replaced `Array.prototype[Symbol.iterator]` was called by `new Derived(1, 2)` — once for each default constructor on the way up — where the language hands the arguments on without iterating. `SpreadExpr` now carries a `synthesised` flag, set only where `parse_class` builds the implicit constructor, and `eval_arguments` copies the elements of a marked spread instead of calling `js_iterate`. The flag records where the spread came from rather than what it looks like: a fix keyed on the constructor's shape also silences a `constructor(...args) { super(...args); }` the *script* wrote, which must keep asking, and `tools/conformance/verify_default_constructor.py` fails on exactly that (`FAIL script-written super spread still asks`). Re-measured: `default-constructor-spread-override.js` fails on the previous binary with `Uncaught Error: @iterator invoked` and passes now; test262 two-binary 14856 → 14857 pass, 0 lost; js-conformance 1145/1158 against 1144/1158; `make test` 317/317 | `include/litebird/js/JsParser.hpp`, `src/js/JsParser.cpp`, `src/js/JsEval.cpp`, `tools/conformance/verify_default_constructor.py` | ARCHITECTURE.md, Phase 63, section 6; one failing case of `iteration.dat`; `docs/findings/165-a-default-constructor-asked-an-iterator-it-never-had.md` | Closed, not open. `src/js/JsEval.cpp` is the file several open rows share — a change here wants a test262 two-binary run, which is what caught nothing regressing |
| **What an iterator still is not**: a symbol is a string in this engine, so a property key cannot be one. `Symbol.iterator` is the key `"@@iterator"`, and `"@@iterator" in []` is `true` where the case of `iteration.dat` asks for `false`; `BuiltinsArray.cpp` registers the array's iterator under `"Symbol.iterator"` as well, for the same reason. Phase 92 gave each kind a prototype between it and `%IteratorPrototype%`, with the kind's one `next` and a `Symbol.toStringTag`, so `Object.getPrototypeOf(it).next` is a function a script can replace and `Object.prototype.toString.call(it)` names the kind; the loop fast paths now ask whether that `next` is still the installed one (`holds_installed_next`) before reading elements directly, and take the long way when it is not. What is left: the symbol keys above, `%IteratorPrototype%` helpers (`map`, `filter`, `take`, `drop`), a `%GeneratorPrototype%` per kind (a generator's `next` is still its own), and `return` on `RegExp String Iterator` | `src/js/JsValue.hpp` (`PropertyMap`, the part that would become a key), `src/js/JsOps.cpp`, `src/js/Generator.cpp` | Phase 63, section 6; Phase 92, finding 98; one failing case of `iteration.dat` (`no property is named 'Symbol.iterator' or '@@iterator'`) | The one thing not to undo: `next` belongs to the prototype, and the fast path's permission to skip it is a question asked of the prototype, not an assumption made about the iterator |
| | | | |
| **What the DOM corpus cannot ask**: `getComputedStyle(el).width`/`height` are used values since Phase 188. The SVG DOM is still partial: Phase 214 ([finding 218](docs/findings/218-an-svg-class-name-was-a-string.md)) makes `SVGElement.className` a live `SVGAnimatedString` whose `baseVal` reflects the class attribute; 20/20 answers match Chromium (1/20 before), with identity, mutation, branding and invalid-receiver checks. `animVal` follows the base value because SVG class animation is not implemented. Phase 236 adds `SVGSVGElement.viewBox` as a live `SVGAnimatedRect` (34/34 Chromium probes; [phase](docs/phases/236-svg-viewbox-is-a-live-rectangle.md), [finding](docs/findings/240-svg-viewbox-was-not-an-object.md)); without animation `animVal` follows the base. Other SVG viewBox-bearing interfaces, `createSVGRect()`, `getBBox()` and other SVG animated attributes remain open (`tests/dom-conformance/open/dates-and-svg.js`). The style sheet objects landed in Phase 72 (`style.sheet`, `document.styleSheets`, `CSSStyleSheet.cssRules`/`insertRule`/`deleteRule`/`disabled`, `CSSRuleList`, `CSSRule`/`CSSStyleRule`), `CSSMediaRule` in Phase 84, and `CSSKeyframesRule`/`CSSKeyframeRule` in Phase 112 (`docs/phases/112-keyframes-in-the-cssom.md`): `cssRules` now lists every rule the sheet holds, in source order, and `@keyframes` blocks are editable through `name`, `cssRules`, `appendRule`, `deleteRule` and `findRule`. Phase 226 exposes `@font-face` as `CSSFontFaceRule` with live descriptors (25/25 Chromium probes; [phase](docs/phases/226-font-face-rules-in-the-cssom.md), [finding](docs/findings/230-font-face-rules-disappeared-from-the-cssom.md)); descriptor grammars are partial and CSSOM edits do not feed font loading. What is still missing: `CSSImportRule` (an `@import` is dropped when a sheet's text is read), `replace` (the promise, which waits on `@import`), and `CSSKeyframesRule` feeding the animation engine (which still reads `css::StyleSheet::keyframes`, not the CSSOM's model). **`getComputedStyle(el).width`/`height` are used values since Phase 188** ([`docs/phases/188-the-width-nobody-could-read.md`](docs/phases/188-the-width-nobody-could-read.md), [finding 192](docs/findings/192-the-width-was-a-promise-the-layout-never-made.md)): CSSOM's resolved value for those two, and it was the computed one — `width:auto` read `auto` where the block is 784 wide, `width:50%` read `50%` where the box is 392, `max-width` was ignored and `box-sizing: border-box` reported the wrong box. The box is resolved from the layout (which is forced, as a browser does), the reported box follows `box-sizing`, and `display:none`/`contents`, a non-replaced inline box and an element with no box keep the computed value because CSSOM says they should. **A `calc()` percentage is a share of the declaration, done for the two properties that keep their basis in the cascade (Phase 195, [finding 199](docs/findings/199-a-calc-percentage-was-a-share-of-the-viewport.md)).** `font-size` percentages are of the inherited font size and `line-height` percentages of the element's own, and both were being made a share of the **viewport**: `font-size: calc(50% + 2px)` in a 20px paragraph computed to 402px and laid out a 502px line box for one character while `getComputedStyle` (which had the right basis) said `12px`; `line-height: max(100%, 20px)` at 10px laid out an 800px line and `font-size: max(50%, 30px)` fell back to 16px. The basis is now chosen per property in `apply_longhand`, and the reporting side answers the length the layout used instead of quoting the declaration. **Still open:** every *box* length (`width`, `height`, `min-/max-*`, `margin-*`, `padding-*`, the insets) still resolves a `calc()` percentage against the viewport, so `calc(100% - 40px)` in a 600px block lays out 760 wide; a `calc()` of percentages only is already right, because it lands on the plain percentage path layout resolves. Fixing the mixed case means letting a `calc()` keep its percentage through to `LayoutBox::resolve_percentages`, which the ~100 places that read those fields as pixels would have to be taught. A `calc()` nested past the resolver's depth limit still keeps its text (and the previous declaration's box). `new CSSStyleSheet()` and `replaceSync` exist since Phase 111 ([`docs/phases/111-a-sheet-a-script-constructs.md`](docs/phases/111-a-sheet-a-script-constructs.md), finding 126); a sheet a script builds is not in the cascade | `src/js/Dom*Bindings.cpp`, `src/main.cpp`, `src/shell/` | `docs/phases/72-cssom.md`, `docs/phases/112-keyframes-in-the-cssom.md`; ARCHITECTURE.md, Phase 27, section 12 & Phase 36 | jsdom does no layout, so the first needs a reference of its own before it is built; the DOM corpus cannot record the CSSOM either, so it is asserted in `tests/test_suite.cpp` (`cssom_sheets_rules_insert_amounts_and_fonts`, `cssom_media_rules_and_the_cascade_inside_them`, `keyframes_are_rules_of_the_cssom`) |
| | | | |
| **A keyword CSS property checks its grammar since Phase 235** ([`docs/phases/235-a-keyword-with-no-grammar.md`](docs/phases/235-a-keyword-with-no-grammar.md), [finding 239](docs/findings/239-a-keyword-property-was-free-text.md)): a property with no rule in `property_rules()` was free text — `setProperty('cursor','pointy')` stored it and `getPropertyValue`, `cssText` and `getComputedStyle` all handed it back, where a browser drops the declaration. Thirty-five keyword properties now have a `ValueKind::Keyword` rule (`table-layout`, `writing-mode`, `mix-blend-mode`, `object-fit`, `white-space`, `cursor`, `list-style-type`, `justify-content`, `align-items`, `will-change` and the rest), `font-weight` is keywords *or* a number so `700` still passes, and an invalid value is refused at the door. **Done in Phase 244** ([`docs/phases/244-a-function-is-a-grammar-too.md`](docs/phases/244-a-function-is-a-grammar-too.md), [finding 248](docs/findings/248-a-function-value-was-free-text.md)): each of the four has its function grammar — `none | strict | content | [ size || inline-size || layout || style || paint ]` for `contain`, `none | <filter-function>+` for `filter` (blur/hue-rotate/the `<number-percentage>` family/`drop-shadow`/`url(…)`), a comma-separated layer list with a required `<mask-reference>` for `mask`, and `none | url(…) | [ <basic-shape> || <geometry-box> ]` with validated shape arguments for `clip-path`; `will-change` is no longer a keyword rule but takes a comma-separated list of custom idents, so `will-change: transform, opacity` is kept. **Still open:** a `<style>` sheet's declarations are checked by the cascade rather than by this table. | `src/js/DomStyleBindings.cpp`, `tests/suites/235-css-keyword-values.cpp` | [`docs/phases/235-a-keyword-with-no-grammar.md`](docs/phases/235-a-keyword-with-no-grammar.md), [`docs/findings/239-a-keyword-property-was-free-text.md`](docs/findings/239-a-keyword-property-was-free-text.md); CSS Values 4, CSS Display, CSS Text | The suite sets one keyword CSS allows and one value nothing allows for each of 35 properties, and asserts `font-weight: 700` survives |
| | | | |
| **An outline is painted since Phase 202** ([`docs/phases/202-the-ring-nobody-drew.md`](docs/phases/202-the-ring-nobody-drew.md), [finding 206](docs/findings/206-an-outline-was-only-a-report.md)): `outline` was in the CSSOM's shorthand and initial-value tables and in no field of `ComputedStyle`, so `getComputedStyle(el).outlineWidth` answered `5px` for `outline: 5px solid` and the display list held only the box's own rectangle — the report was right and the ink was missing, which is why nothing caught it. The four longhands are fields of the style now (hashed with it), the cascade stores the shorthand and each longhand (`thin`/`medium`/`thick` are 1/3/5px, `currentcolor` is `color`, `outline-style: none` keeps the width and clears the drawn flag), and `emit_chrome` draws one ring over the border box grown by `outline-offset + outline-width`, after the background and the border. Note the geometry: an outline is *outside* the border box, so `outline-offset: 0` shows only the band beyond the edge. **Still open:** `dashed`/`dotted`/`double`/`groove`/`ridge`/`inset`/`outset` paint solid (the border painter's limit too), and the ring is square on a rounded box. | `include/litebird/css/StyleResolver.hpp`, `src/css/StyleResolver.cpp`, `src/render/PaintCommand.cpp`, `tests/test_suite.cpp` | [`docs/phases/202-the-ring-nobody-drew.md`](docs/phases/202-the-ring-nobody-drew.md), [`docs/findings/206-an-outline-was-only-a-report.md`](docs/findings/206-an-outline-was-only-a-report.md); CSS 2.1 §16.3 | The suite renders to a `BitmapRenderer` and reads pixels: no room taken, offset and `none` honoured, and the same four declarations read back through `getComputedStyle` as what was painted |
| | | | |
| **All three `text-decoration` lines are painted since Phase 217** ([`docs/phases/217-three-lines-one-drawn.md`](docs/phases/217-three-lines-one-drawn.md), [finding 221](docs/findings/221-the-third-decoration-had-nowhere-to-go.md)): `line-through` and `overline` were parsed, carried on every `InlineFragment` and reported by `getComputedStyle`, and drawn by nothing — the inline painter asked only `has_underline()`, and finding 77 had recorded it as open since Phase 71. The two lines were skipped because their *y* had no honest source: the underline sat at `rect.height - 0.08em`, a fraction of the line box rather than of the face. `FontRenderer::decoration_lines` now rasterises `'x'` and `'H'` at the pixel size the painter will use and returns the face's x-height and cap height, so `line-through` runs through the x-height, `overline` at the cap height, and the underline half a descent under the baseline. Both text paths call the one helper, including the stand-alone text node that held no decoration at all. **Still open:** `text-decoration-style`/`-color`/`-thickness`/`text-underline-position`/`-offset` are unparsed, decoration does not propagate from a block to inline descendants, and adjacent fragments of one line get separate rectangles rather than a merged one. | `src/render/PaintCommand.cpp`, `src/render/FontRenderer.cpp`, `include/litebird/render/FontRenderer.hpp`, `tests/test_suite.cpp` | [`docs/phases/217-three-lines-one-drawn.md`](docs/phases/217-three-lines-one-drawn.md), [`docs/findings/221-the-third-decoration-had-nowhere-to-go.md`](docs/findings/221-the-third-decoration-had-nowhere-to-go.md), CSS Text Decoration 3 §2, finding 77 | The suite counts decoration rectangles in the display list (three, not one) and checks each against its own box's baseline arithmetic and against `decoration_lines`; it fails at `lines.size() == 3` with the painter reverted |
| | | | |
| **Root layout, background images and general margin collapse remain open.** Phase 152 positions the body and handles a limited nonempty first-child top-margin chain; Phase 166 makes HTML root margins move the body even though the root still has no full layout box. **Phase 170** (`docs/phases/170-canvas-background-from-the-root.md`, finding 175) propagates a solid root/body color to the canvas, including alpha, scrolling and transparent iframes. The 41-page body corpus has 131 independently Chromium-verified assertions: 35 pages match and six carry reasons. **Phase 190** (`docs/phases/190-display-contents-has-no-box.md`, finding 194) implements `display:contents` — the element makes no box and its children splice into the parent with the parent's style — and closes the row's one **regression**: a boxless body no longer colours the canvas, because the canvas source must now own a box. A `display:contents` flex/grid child and a spliced table are implemented but unmeasured against Chromium. Root background images should prevent body propagation but are not represented, root opacity groups and hidden descendants are not painted correctly, and inline-body default margins, translucent nested inline backgrounds and empty/sibling/bottom margin collapse remain open. A prior general-collapse attempt was reverted because it broke the passing first-child chain | `src/layout/LayoutTree.cpp`, `src/layout/LayoutBox.cpp`, `src/render/PaintCommand.cpp`, `include/litebird/css/StyleResolver.hpp` | Phase 152 / finding 157; Phase 166 / finding 171; Phase 170 / finding 175 | Treat full root layout, background images and general collapse as separable work. Preserve the 131 reference assertions and remove a `known_failure` when it starts passing (Phase 190 removed one; 35 match now). The UA body-white normalizer in the resolver header should be removed together with the original declaration once `src/css/StyleResolver.cpp` is no longer claimed |
| **Terminal viewport clipping and ink** (Phase 183, finding 187): borders, shadows and text honor rectangular clips; borders, focus outlines and shadows no longer clamp wholly offscreen geometry onto the viewport, and per-side colors and alpha are retained. `tools/conformance/verify_terminal_clipping.py` decodes the public ANSI output and checks 26 hand-specified grids (2/26 before, 26/26 after), including an empty viewport that used to crash. **Phase 203** (`docs/phases/203-page-text-cannot-command-the-terminal.md`, finding 207) ensures page-text cell output cannot emit control bytes; **Phase 241** (`docs/phases/241-a-terminal-cell-is-a-character.md`, finding 245) made that rule a character rule rather than a byte rule: a cell holds a UTF-8 grapheme cluster and its column width, a combining mark or zero-width joiner joins the cell before it (UAX #29), a wide character (UAX #11 W/F, from `tools/gen_terminal_width_tables.py`) takes two columns and its second column is not written because the terminal advances over it, and C0/DEL/C1/U+2028/U+2029 plus malformed UTF-8 become U+FFFD one per undecodable byte instead of `?` one per byte. The conformance file now checks 31 hand-specified grids (26/31 before, 31/31 after), and an 80-column render is exactly 80 columns per row by Python's independent East_Asian_Width count. **Phase 212** (`docs/phases/212-shell-frame-text-cannot-command-the-terminal.md`, finding 216) sanitizes URL, focused tag/ID and status text at the shell ANSI frame boundary, keeping stored navigation and DOM data intact. **Phase 248** ([finding 252](docs/findings/252-worker-error-terminal-control.md)) sanitizes the default unhandled worker-error diagnostic, while preserving the ErrorEvent message; the real CLI verifier passes 3/3, up from 2/3. **Still open:** ambiguous-width characters take one column (CJK ambiguous mode not modelled), flag pairs and sequences needing the full UAX #29 class table are drawn as separate cells, other direct terminal writes and terminal input/prompt echo, cell-origin rather than pixel clipping, rounded corners, shadow blur, and focus-ring marks being painted before later content can overwrite them | `src/render/TerminalRenderer.cpp`, `include/litebird/render/TerminalRenderer.hpp`, `src/render/TerminalWidthTables.cpp`, `tools/gen_terminal_width_tables.py`, `tools/conformance/verify_terminal_clipping.py`, `tools/conformance/terminal_clipping_fixture.cpp` | `docs/phases/183-terminal-ink-stays-in-the-viewport.md`, `docs/findings/187-terminal-ink-crossed-the-clip.md` | Terminal cell checks are not a browser screenshot oracle; retain the two pre-existing rectangle/image controls and exercise the real HTML/display-list path |
| **GC leak audit & native-capture tracing** (automatic safe-point scheduling landed in Phase 145, finding 150 — churn loops now stay flat at ~18 MiB RSS without calling `gc()`). **A bound function no longer leaks its target:** Phase 168 (finding 173) found that `bind` kept the target, the bound `this` and the bound arguments inside the `std::function` the collector cannot look into, plus a fourth copy in `Extra::bound_target` that was never counted, so every bound function's payload read `owners` greater than `seen` and was rooted for ever. What an entry point needs is now `Extra::captured`, counted and marked like any other reference and read through `JsValue::current_captured()`; `o.b = f.bind(o)` and `o.b = f.bind(null)` are both flat over 2,000 iterations. **Phase 227 (`docs/phases/227-the-gc-row-counted-a-queue.md`, finding 231) found the three claims this row used to carry were all false.** The row said a promise reaction was "still two cells an iteration, and `then(f).then(g)` four". Both numbers reproduce, and neither is a leak: a queued reaction is work outstanding, rooted by the microtask queue on purpose, and counting `alive_cells` at the bottom of the loop measures the queue's depth. 2,000 iterations measure +8,000 cells mid-loop and **+1** after one task turn, and the drain is what does it — a leak cannot release 8,000 cells, a queue must. `tools/conformance/verify_gc_residue.py` now runs fifteen churn shapes (reactions, chains, `bind`, pending/fired/cleared timers and intervals, listener pairs, a settled promise with no handler) at 200 and at 2,000 iterations and requires the **residue to be the same at both sizes** — drift is 0 cells on every case; the checker was first run against an injected leak and caught it at 1,800 cells of drift. The row's second claim, that pending timers were suspect, is answered by the same run: a pending timer roots its callback because that is what a pending timer is, and both firing and clearing give the cells back. The third, the native-capture audit, is done statically: of 181 `make_native_function` sites every by-value `JsValue` capture goes through `set_payload_captured` except `map_inst`/`set_inst` in `src/js/VM.cpp`, where the captured instance and its accessor are built together and share a lifetime by construction. **Still open:** the `holders > 3` heuristic in `Promise::trace_extra` guesses that a promise with native owners must keep its reactions as roots; it can only over-retain, the `promise-settled-from-timer` case is flat, and a resolver deliberately held by native code is reachable by design and is not measured. | `src/js/Heap.cpp`, `src/js/Promise.cpp`, `src/js/EventLoop.cpp`, `tools/conformance/verify_gc_residue.py`, and native-function / binding captures | Known gaps, 4; ARCHITECTURE.md, Phase 34, section 9; one failing case of `tests/js-conformance/prototypes.dat`; Phase 145, finding 150; [Phase 168](docs/phases/168-a-capture-the-collector-could-not-see.md), [finding 173](docs/findings/173-a-bound-function-made-its-own-target-immortal.md), [Phase 227](docs/phases/227-the-gc-row-counted-a-queue.md), [finding 231](docs/findings/231-the-leak-was-the-queue-s-depth.md) | Touches native function captures, the DOM bindings included: not beside a row that changes `src/js/Dom*Bindings.cpp`. Measure residue at two sizes, never at the bottom of a loop: a mid-loop count reads the queue |
| **HTTPS certificate test coverage remains partial.** Phase 148 (`docs/findings/153-every-certificate-refusal-said-the-same-thing.md`) fixed the part of this that hid the rest: the engine discarded `SSL_get_verify_result`, so every refusal read `TLS handshake or certificate verification failed` and a fixture that failed for the wrong reason passed as any other rejection. Both harnesses now hold each fixture to the reason it was built with, and the loader and the `wss://` path print `certificate verification failed (code N, name)`. Each net was run against the unfixed engine before the fix went in, and both went red (14 of 20 HTTPS cases, 3 of 32 WebSocket checks). Phase 89 fixed IP-literal identity checks and Phase 100 (`docs/phases/100-https-certificate-chains-dates-and-ipv6.md`) grew the fixtures to twenty, cross-checked against Python ssl: a chain through an intermediate (trusted, intermediate missing, intermediate `CA:FALSE`), dates (expired leaf, not-yet-valid leaf, expired intermediate), IPv6 literals in compressed and expanded form with the wrong address family written both ways, and SNI (a name for a DNS host, none at a literal URL — the fixture asserts what the engine sent). No engine change was needed: those checks are OpenSSL's and Phase 89's parameters were already right. What remains open is revocation (OCSP/CRL), pinning, TLS 1.3 resumption, and the fact that the oracle is the same OpenSSL library, not an independent TLS stack | `tools/security/verify_https.py` | `docs/phases/89-https-certificate-identity.md`, `docs/findings/95-https-ip-identity.md`, `docs/phases/100-https-certificate-chains-dates-and-ipv6.md`, `docs/findings/106-the-tls-fixtures-never-chained.md`, `docs/findings/153-every-certificate-refusal-said-the-same-thing.md` | Python ssl is an independent client using the same OpenSSL library, not an independent TLS implementation. Phase 258 enables partial-chain trust for HTTPS: a trusted intermediate or leaf can anchor a chain, but identity and dates still apply (27 HTTPS cases, 6 Node comparisons; [phase](docs/phases/258-an-https-trust-anchor-need-not-be-a-root.md), [finding](docs/findings/262-a-trusted-intermediate-was-not-a-trust-anchor.md)). WSS has a separate TLS setup and still rejects non-self-signed trust anchors |
| **WebSocket binary frames, `permessage-deflate`, typed event delivery, and HTTP CONNECT tunneling for `wss://` are done.** Phase 90 landed authenticated `wss://` TLS (OpenSSL 3 CA/SNI/DNS+IP SAN), Fetch `Origin` & mixed-content enforcement, relative URL resolution, live JS accessors, synchronous validation, CSPRNG keys/masks, RFC 6455/3629 framing/UTF-8 checks, `poll()` + wake-pipe I/O, and exit thread join; Phase 117 added upgrade cookies (`ws` matched as `http`, `wss` as `https`, `Set-Cookie` on `101` stored); Phase 161 (`docs/phases/161-websocket-binary-frames.md`, finding 166) landed binary frame exposure (`send(ArrayBuffer | TypedArray | DataView | Blob | File)`, live `binaryType` `"blob"`/`"arraybuffer"`, `new Blob([BufferSource])`). **Phase 169 (`docs/phases/169-websocket-permessage-deflate.md`, finding 174)** landed RFC 7692 `permessage-deflate`: zero-dependency streaming `Deflate::DeflateStream` / `Deflate::InflateStream` (`window_bits` 9..15 enforcement, context takeover & reset, stored-block fallback, decompression-bomb cap), `Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits` offer + fail-closed §7.1 validation reflected on `ws.extensions`, I/O-thread compression/decompression for text/binary/fragmented frames (`RSV1=1`, control frames `RSV1=0`), `1009` on >16 MiB bombs, `1002` on invalid `RSV1`/DEFLATE, and `1007` on post-inflate invalid UTF-8 (`make security-websocket`, 38/38 including 3 `wss://` Python `zlib` checks; `websocket_permessage_deflate_rfc7692`). **Phase 187** ([`docs/phases/187-websocket-message-and-close-events.md`](docs/phases/187-websocket-message-and-close-events.md), finding 191) gives `message` callbacks a real `MessageEvent` and `close` callbacks a `CloseEvent`, with trusted target/currentTarget state. The interface constructors already existed; WebSocket delivery had bypassed them and built plain objects. Worker message delivery and WebSocket `open`/`error` events were not changed by Phase 187. **Phase 205** ([`docs/phases/205-websocket-open-and-error-events.md`](docs/phases/205-websocket-open-and-error-events.md), finding 209) constructs trusted generic `Event` instances for WebSocket `open` and `error` callbacks, with target/currentTarget, AT_TARGET phase and a one-target composed path during dispatch; the existing `error.message` diagnostic expando is preserved. **Phase 242** ([`docs/phases/242-websocket-connect-tunneling.md`](docs/phases/242-websocket-connect-tunneling.md), finding 246) routes `wss://` through an HTTP CONNECT tunnel selected from `HTTPS_PROXY` / `https_proxy` (falling back to `ALL_PROXY` / `all_proxy`), honors host/domain/port `NO_PROXY` / `no_proxy` bypasses, and starts the existing target-host-verified TLS handshake only after a valid 2xx response; invalid proxy configuration and refusals such as 407 fail closed. Plain `ws://`, proxy authentication, and TLS to the proxy are out of scope. **Still open:** `WebSocket.prototype` remains tracked in the host-constructor row above | `src/net/WebSocket.cpp`, `include/litebird/net/WebSocket.hpp`, `src/net/Deflate.cpp`, `include/litebird/net/Deflate.hpp`, `tools/security/verify_websocket.py`, `tests/suites/242-websocket-connect-tunneling.cpp` | Phase 90 / finding 96 (`docs/phases/90-websocket-tls-fetch-policy-and-frames.md`, `docs/findings/96-websocket-tls-origin-and-framing.md`), Phase 117 / finding 122 (`docs/phases/117-cookies-on-the-websocket-upgrade.md`, `docs/findings/122-a-websocket-had-no-cookies.md`), Phase 161 / finding 166 (`docs/phases/161-websocket-binary-frames.md`, `docs/findings/166-websocket-discarded-binary-frame-type.md`), Phase 169 / finding 174 (`docs/phases/169-websocket-permessage-deflate.md`, `docs/findings/174-websocket-rejected-permessage-deflate.md`), Phase 187 / finding 191 (`docs/phases/187-websocket-message-and-close-events.md`, `docs/findings/191-websocket-events-were-plain-objects.md`), Phase 205 / finding 209 (`docs/phases/205-websocket-open-and-error-events.md`, `docs/findings/209-websocket-open-and-error-events-were-plain-objects.md`), Phase 242 / finding 246 (`docs/phases/242-websocket-connect-tunneling.md`, `docs/findings/246-websocket-could-not-use-a-proxy.md`) | Binary frames, `permessage-deflate`, typed `message`/`close`/`open`/`error` event delivery, and `wss://` HTTP CONNECT tunneling are done; proxy authentication and ordinary `ws://` proxying remain out of scope |
| **What is left of `DataView` and `%TypedArray%.prototype` after Phases 126, 157 and 177.** Phase 106 supplied `ArrayBuffer` and the nine typed arrays; Phase 126 (`docs/phases/126-a-dataview-and-the-iterating-half.md`, finding 131) added `DataView` and the thirteen iterating `%TypedArray%.prototype` methods; Phase 157 (`docs/phases/157-arraybuffer-transfer-and-resize.md`, finding 162) added ES2024 `transfer()`, `transferToFixedLength()`, `detached`, `{ maxByteLength }`, `resizable`, `maxByteLength`, `resize()`, prototype accessors, and length-tracking / out-of-bounds views (+287 test262 passes, 0 lost); Phase 177 ([`docs/phases/177-the-element-was-never-asked.md`](docs/phases/177-the-element-was-never-asked.md), finding 181) made `%TypedArray%.prototype.toLocaleString` ask every element for its own `toLocaleString` with the boxed element as `this` (it printed the numbers itself, so `Number.prototype.toLocaleString = () => "X"` left `new Uint8Array([1,2]).toLocaleString()` as "1,2"), kept it a separate receiver-validating function as §23.2.3.29 requires, read `[[ArrayLength]]` rather than a redefinable `length` property, and made `%TypedArray%.prototype.toString` *be* `Array.prototype.toString` (§23.2.3.30) (+14 over all of `built-ins/TypedArray`+`built-ins/Array`, 0 lost). **Still open:** `BigInt64`/`BigUint64` on a `DataView` (needs `BigInt`), `SharedArrayBuffer`, and `transferToImmutable`. **Not a gap: `Symbol.species`.** The 2025 edition removed `@@species` from `%TypedArray%` (tc39/ecma262#3450), so `TypedArray[Symbol.species]` is `undefined` and `.map`/`.slice` build from the receiver's own constructor; Node v22.22.3 does the same, which was measured rather than assumed. | `src/js/TypedArray.cpp`, `include/litebird/js/TypedArray.hpp`, `tests/test_suite.cpp` | `docs/phases/106-typed-arrays-and-arraybuffer.md` section 4, `docs/phases/126-a-dataview-and-the-iterating-half.md`, `docs/phases/157-arraybuffer-transfer-and-resize.md`, `docs/findings/162-a-buffer-checked-detached-and-could-never-be-detached.md`, `docs/phases/177-the-element-was-never-asked.md` | Additive: the byte order and detachment rules are asserted against Node.js v22.22.3 and test262 (`2e58f87d8350`) |
| | | | |
| ~~**The four interfaces typed arrays unblocked.**~~ **All four are now implemented.** Phase 106 made `ArrayBuffer` exist; all four interfaces it unblocked have landed: (1) `TextDecoder`/`TextEncoder` in Phase 127 (`docs/phases/127-textdecoder-and-textencoder-reach-script.md`, finding 132), (2) `CanvasRenderingContext2D`'s `ImageData` in Phase 139 (`docs/phases/139-image-data-is-an-interface.md`, finding 144; `toDataURL()` in Phase 144, while `toBlob()`, `ImageBitmap` and `OffscreenCanvas` are separate), (3) Fetch/XHR binary bodies (`Response.arrayBuffer()`, `Blob.arrayBuffer()`, XHR `responseType = "arraybuffer"`) in Phase 150 (`docs/phases/150-binary-response-bodies.md`, finding 155), and (4) **WebSocket binary frames (`send(ArrayBuffer | TypedArray | DataView | Blob | File)` and `binaryType = "arraybuffer"` / `"blob"` delivery) in Phase 161** (`docs/phases/161-websocket-binary-frames.md`, finding 166) | `src/net/Fetch.cpp`, `src/js/DomFileBindings.cpp`, `src/js/DomWindowBindings.cpp`, `src/net/WebSocket.cpp`, `src/render/Canvas2D.cpp`, `src/js/JsInterpreter.cpp` | `ROADMAP.md` row 6; [`docs/phases/150-binary-response-bodies.md`](docs/phases/150-binary-response-bodies.md), finding 155; [`docs/phases/161-websocket-binary-frames.md`](docs/phases/161-websocket-binary-frames.md), finding 166 | Done across all four interfaces; remaining per-interface extensions live in their own rows |
| **A canvas has an origin-clean flag since Phase 237, and `drawImage` is what can taint it.** `HTMLCanvasElement.toDataURL()` (Phase 144) hands a script every pixel the canvas holds, which the standard allows only while the bitmap is origin-clean (HTML §4.12.5.1). The row used to say the flag had "no input" because `drawImage`, `ImageBitmap`/`createImageBitmap` and `Response.arrayBuffer()` did not exist; two of those had since landed, and the input it had not considered was `<img>` — the renderer was already fetching and decoding image pixels under a CORS-aware request context and recording whether the response crossed an origin. **Phase 237** (`docs/phases/237-the-canvas-that-drew-a-cross-origin-image.md`, finding 241) binds `drawImage` (3-, 5- and 9-argument forms, sources: canvas, `ImageBitmap`, decoded `<img>`) and writes the flag the row refused to fake: drawing a source that is not CORS-same-origin clears it, `toDataURL()` then throws `SecurityError`, and setting `width`/`height` gives the canvas a new bitmap and with it a clean flag. The origin is carried fetch → element (`Element::m_image_source_cross_origin`, set once by `load_image_for_element` from `ResourceResponse::cross_origin`) → `ImageBitmap` → destination, so a tainted canvas cannot be laundered through a bitmap. Two bugs came out with it: `createImageBitmap(canvas)` built a bitmap of zeroed pixels instead of copying the canvas, and nothing connected the decoded cross-origin pixels to the serializer. The suite serves one PNG from two origin roots and requires **opposite** answers on the two pages — `SecurityError` from the cross-origin one, real pixels from the same-origin one — because a flag that is always true and a flag that is never true both pass a single-page test; both were injected and both fail it. **Phase 262** (`docs/phases/262-the-canvas-makes-its-blob.md`, finding 266) then added the second door: `HTMLCanvasElement.toBlob(callback, type, quality)` serializes the same bitmap into a `Blob` (`image/png` only, like `toDataURL()`), throws `SecurityError` synchronously on an unclean bitmap, fires the callback in its own task with `null` whenever the encoding fails, and `TypeError` for a missing or non-callable callback. The Phase 153 write-up and finding 158 describe a landing that was not in the tree (no binding, no `canvas_to_blob_is_the_png_it_painted` suite) and are left as written; 262 is the one that landed. **Still open:** `drawImage` of an `<img>` needs the layout pass to have decoded it, so a page that draws before layout gets a `TypeError` rather than an on-demand fetch; `createImageBitmap`'s crop/resize options and an `<img>`-to-`ImageBitmap` path; and `OffscreenCanvas` transfer. | `src/render/Canvas2D.cpp`, `src/js/DomHtmlBindings.cpp`, `src/js/DomWindowBindings.cpp`, `src/layout/LayoutTree.cpp`, `include/litebird/render/Canvas2D.hpp`, `include/litebird/dom/Element.hpp`, `include/litebird/js/ImageBitmap.hpp`, `tests/suites/237-the-canvas-that-drew-a-cross-origin-image.cpp`, `tests/suites/262-canvas-to-blob.cpp` | `docs/phases/144-the-canvas-hands-back-its-png.md`, [Phase 237](docs/phases/237-the-canvas-that-drew-a-cross-origin-image.md), [finding 241](docs/findings/241-the-pixels-had-an-origin-and-nobody-asked.md), [Phase 262](docs/phases/262-the-canvas-makes-its-blob.md), [finding 266](docs/findings/266-the-canvas-could-not-export-a-blob.md) | The flag is not a follow-up to `drawImage`, it is part of it: adding `drawImage` without it would have created a real cross-origin read in one commit. Any new pixel source must set the flag where it is decoded, not where it is serialized; both serializers (`toDataURL()`, `toBlob()`) must refuse the unclean bitmap |
| **AVIF, still: an AV1 intra-frame decoder.** The container is read since Phase 172 ([`docs/phases/172-the-avif-container-read-and-the-picture-still-open.md`](docs/phases/172-the-avif-container-read-and-the-picture-still-open.md), finding 172): `AvifContainer` parses `ftyp`, `pitm`, `iloc`, `iinf`/`infe`, `iprp`/`ipco`/`ipma`, `ispe`, `pixi`, `av1C` and `colr`, bounds-checks every length and offset against its parent, and hands back the primary `av01` item's bytes, its size, its bit depths and its decoder configuration. 200,000 mutated files through it under ASan and UBSan: no crash. **Phase 218** ([`docs/phases/218-avif-picture-and-container-oracle.md`](docs/phases/218-avif-picture-and-container-oracle.md), [finding 222](docs/findings/222-avif-picture-decoding-unmeasured.md)) adds the CI-enforced differential oracle `make verify-avif` (`tools/conformance/verify_avif.py`, `tests/media/avif/corpus.json`, `media_check --probe-avif`/`--decode-avif`/`--fuzz-batch avif`, also wired into `asan-verify`) with an independent ISO-BMFF + AV1 §5.3 OBU parser, 6 committed libheif-verified AVIF fixtures + 3 live libheif patterns, 8 hostile/malformed/unsupported refusals, 200 deterministic fuzz mutations, 5 negative controls, and an end-to-end `<img src="data:image/avif;base64,...">` `known_failure` reftest in `tests/render/reftests.json` — grading any future AV1 decoder pixel-by-pixel against libheif RGBA (`KNOWN` -> `FIXED` gate). What is still missing is the picture: there is no AV1 decoder, so `ImageDecoder::decode` returns `nullptr` for an AVIF and both the unit test and `verify-avif` / `reftests.json` assert that on purpose, so the day one lands they flip and say the row is done. AV1 is not VP8: `Vp8Decoder.cpp` is 1,451 lines and 583 of tables for a much smaller codec, so this is several sessions, not one. The AV1 specification's own §9.4 lists the default CDF tables normatively, which is where `Vp8Tables.cpp` took VP8's from RFC 6386 — take them from there, not from libaom. Alpha is a second `av01` item marked by an `auxl` reference (`iref`/`auxC`, not read here); a tiled image's primary is a `grid` item (refused, `Unsupported`); a sequence (`avis`) keeps frames in a track, not in `meta`; `irot`, `imir` and `clap` change presentation and are not read | `src/render/AvifContainer.cpp`, `include/litebird/render/AvifContainer.hpp`, `src/render/ImageDecoder.cpp`, `tools/media_check.cpp`, `tools/conformance/verify_avif.py`, `tests/media/avif/corpus.json` | [`docs/phases/172-the-avif-container-read-and-the-picture-still-open.md`](docs/phases/172-the-avif-container-read-and-the-picture-still-open.md), section 4; [`docs/findings/172-the-box-was-read-as-a-picture.md`](docs/findings/172-the-box-was-read-as-a-picture.md); [`docs/phases/218-avif-picture-and-container-oracle.md`](docs/phases/218-avif-picture-and-container-oracle.md), [finding 222](docs/findings/222-avif-picture-decoding-unmeasured.md); the AV1 specification §9.4 (default CDF tables), ISO/IEC 14496-12 (the box structure), ISO/IEC 23000-22 and the AV1 Image File Format specification | The row used to be one word, which read as one task and is several. The oracle (`make verify-avif`) is now in CI and ready to grade any AV1 intra-frame decoder pixel-by-pixel against libheif | `src/render/AvifContainer.cpp` (`Walk`) |
| **Audio/Video (`HTMLMediaElement`, `MediaSource`, Web Audio), WebCodecs, and WebGL 1/2 — measured by CI oracle since Phase 229** ([`docs/phases/229-media-webgl-and-codecs-oracle.md`](docs/phases/229-media-webgl-and-codecs-oracle.md), [finding 233](docs/findings/233-audio-video-webgl-and-codecs-unmeasured.md)). `make verify-media-webgl-codecs` (`tools/conformance/verify_media_webgl_codecs.py`, `tests/conformance/media_webgl_codecs.json`, wired into CI `Build & unit tests` and `asan-verify`) grades **25 normative spec checks** with **5 built-in negative controls** under the `PASS`/`KNOWN`/`FIXED`/`FAIL` contract: currently **12/25 pass, 13 `KNOWN`** (`audio_video=10/12` [2 known], `webcodecs=0/5` [5 known], `webgl=2/8` [6 known]; Phase 253, [finding 257](docs/findings/257-a-media-element-kept-nothing.md), [phase](docs/phases/253-an-honest-media-element.md), promoted `media_element_constants`, `media_element_volume_setter_and_range_error`, `media_element_muted_current_time_and_playback_rate`, `media_element_time_ranges_and_media_error`, `media_element_can_play_type_standard_codecs` and `media_element_play_without_source_rejects`: `NETWORK_*`/`HAVE_*` constants, per-element `volume`/`muted`/`currentTime`/`playbackRate` state with the spec's `IndexSizeError`/`TypeError` split, `TimeRanges`/`MediaError` interfaces, `error === null`, `play()` rejecting with `NotSupportedError`, and `canPlayType` answering `maybe` for the shipped container/codec pairs and `""` otherwise). The valid 100 ms PCM WAV data URI still leaves `readyState === 0` / `duration === NaN`; `muted` is live state now (`defaultMuted` reflects the attribute); the media state lives on the element's script wrapper per realm, not on the tree. `MediaSource`, `AudioContext`/`OfflineAudioContext`, all 8 WebCodecs constructors, and all WebGL 1/2 constructors & `getContext("webgl")` / `"webgl2"` are unimplemented. `tests/render/reftests.json` also adds the paired positive-control/target pixel reftests `canvas_2d_script_paints_canvas_green` (PASS) and `webgl_clear_paints_canvas_green` (`KNOWN`), bringing `reftests.json` to **56/66 pass, 10 known** (**107 pages / 395 assertions** combined). | `src/js/DomHtmlBindings.cpp`, `tools/conformance/verify_media_webgl_codecs.py`, `tests/conformance/media_webgl_codecs.json`, `tests/suites/253-html-media-element-state.cpp`, `tests/render/reftests.json` | [`docs/phases/229-media-webgl-and-codecs-oracle.md`](docs/phases/229-media-webgl-and-codecs-oracle.md), [finding 233](docs/findings/233-audio-video-webgl-and-codecs-unmeasured.md); [`docs/phases/253-an-honest-media-element.md`](docs/phases/253-an-honest-media-element.md), [finding 257](docs/findings/257-a-media-element-kept-nothing.md); WHATWG HTML §4.8.11 & §4.12.5, W3C MSE, W3C Web Audio, W3C WebCodecs, Khronos WebGL 1.0/2.0 | The self-contained `HTMLMediaElement` state/constant/range-error cases are DONE (Phase 253). Next: demux/decode (ROADMAP row 36) before WAV metadata or `playbackRate` seeking, the load algorithm (networkState/readyState/events), `defaultPlaybackRate`, then MediaSource/Web Audio, WebCodecs, WebGL; every fixed case trips `FIXED` and exits 1 until promoted |
| ~~**`Object.isFrozen` and `Object.isSealed` answer `false` for an object that is not extensible and has no properties.**~~ **Fixed in Phase 125 (finding 130).** Both are ECMA-262 §7.3.15 TestIntegrityLevel — not extensible, then every own property asked whether it is writable (frozen only) or configurable — and now say so. The engine had kept `frozen`/`sealed` as bits only `freeze()`/`seal()` set, so `preventExtensions({})`, `seal({})` and `preventExtensions` over fully-read-only properties all answered `false` where the language has `true`. test262 `built-ins/Object/isFrozen` + `isSealed`: 85/92 → 88/92 | `src/js/BuiltinsObject.cpp` (`test_integrity_level`) | ECMA-262 §20.1.2.13, §20.1.2.15, §7.3.15 | Done; the four remaining failures are the `Proxy` row and the `not-a-constructor` row |
| | | | |
| ~~**A `String` object's own index properties are not described.**~~ **Fixed in Phase 132 (finding 137).** A String exotic object has its UTF-16 code-unit indices as own properties: non-writable, enumerable, non-configurable. `descriptor_for` handled the primitive `JsType::String` and its wrapper's `length`, but skipped the `ObjectClass::String` wrapper's virtual indices; it now asks the wrapped string before the ordinary property map. `Object.getOwnPropertyDescriptor`, `Reflect.getOwnPropertyDescriptor`, `Object.getOwnPropertyDescriptors` and `propertyIsEnumerable` agree with Node.js v22.22.3, including a surrogate-pair code unit | `src/js/BuiltinsObject.cpp` (`descriptor_for`) | ECMA-262 §10.4.3, §20.1.2.9, §28.1.6 | Closed; an index past the wrapped length remains an ordinary property |
| | | | |
| ~~**An array that is sealed or non-extensible still grows.**~~ **No longer true — fixed by #146 (Phase 116, finding 125) before this row was written.** Found while probing Phase 125 on a tree that predated that merge: `push` tested `object_val->frozen` rather than `[[IsExtensible]]`, so `Object.seal([1,2]).push(3)` grew the array where the language throws, and the same held for `unshift`, a growing `splice`, `sort` and `pop` on a sealed or `preventExtensions` array. Re-probed after merging #146: all eight cases throw exactly where Node v22 throws, so the row is closed with no code change of its own. Recorded rather than deleted because the probe and the reasoning are in finding 130 §4 | — | ECMA-262 §7.3.3 Set, §7.3.4 CreateDataPropertyOrThrow | Closed by #146; verified against Node in Phase 125 |
| | | | |
| ~~**Early errors still open after Phases 128 and 141.**~~ **Closed by Phase 171 (`docs/phases/171-direct-eval-context-and-declaration-instantiation.md`, finding 176).** Phase 128 (`docs/phases/128-a-class-body-only-knew-three-early-errors.md`) added §15.7 class-body rules (+173), Phase 141 (`docs/phases/141-early-syntax-errors-outside-class-bodies.md`, finding 146) added non-class parser rules (+703), and **Phase 171** finished the remaining strict-eval / `new.target` / `EvalDeclarationInstantiation` / loop-completion / parameter-scope pieces (`class/dstr` already stands at 3,840/3,840). Direct `eval` now inherits caller `strict`, `new.target`, `super.x`/`super()`, and class-field `forbid_arguments`; indirect `eval` executes in an isolated global lexical scope; `run_program()` runs atomic `EvalDeclarationInstantiation` with Annex B.3.5 simple-`catch` exemption for both `var` and hoisted functions; global `var`/function configurability and sloppy local `eval` `delete` follow ECMA-262; `invoke_function()` splits `param_env -> body_var_env -> body_lex_env` on parameter expressions; loops reset completions; and invalid `break`/`continue`/labels, top-level `return`, `new.target`, and computed `static ['prototype']` throw. Re-measured vs `origin/main`: `DEFAULT_DIRECTORIES` **14,898 → 14,967 / 15,686 (+69 / -0)**, `language/eval-code` **163 → 251 / 343 (+88 / -0)**, non-module `language/` **20,629 → 20,893 / 23,005 (+264 / -0)**, `annexB` **673 → 673 (+0 / -0)**, `js-conformance` **1,145 / 1,158 (90 / 90 everyday)**, `make test` **339 / 339**, `tools/conformance/verify_eval_context.py` **13 / 13**. Remaining outside this row: `Proxy` (96), `$262` (53), `88` pre-TC39-PR#1322 `declare-arguments` tests matching Node.js v22.22.3, `'with'`, strict lexer octal literals (`010`), and moving built-ins off `m_global_env` (`decl-lex-*-global.js`) | `include/litebird/js/JsParser.hpp`, `include/litebird/js/JsInterpreter.hpp`, `src/js/JsParser.cpp`, `src/js/JsEval.cpp`, `tools/conformance/verify_eval_context.py` | Phase 128 / finding 133, Phase 141 / finding 146, Phase 171 (`docs/phases/171-direct-eval-context-and-declaration-instantiation.md`), finding 176 (`docs/findings/176-eval-dropped-its-callers-context.md`) | Closed. Parser RAII guards over `m_functions` (`DepthGuard`, `LabelScope`, `InParamsScope`) use vector indices rather than references because nested parses reallocate `m_functions` |
| | | | |
| ~~**`set_property` does not create a property on an object that has no property table yet.**~~ **The premise does not hold — checked at `0b637ab` and retired by Phase 168 (finding 173).** `JsValue::set_property`'s tail already creates the table (`loose_properties(true)`, then `properties = std::make_shared<PropertyMap>()`), so `(*map)[name] = val` is not dropped: a write to a native function, a `null`-prototype object, a class's `prototype`, a `RegExp`, an `Error`, a promise and a bound function all read the value back, as Node.js v22.22.3 does. The hand-written `[[Set]]` in `install_error_stack_accessor` was written against a bug that was not there; it stays until the phase that takes `src/js/JsOps.cpp`, and deleting it is that phase's first task | `src/js/JsOps.cpp` (`install_error_stack_accessor`), `src/js/BuiltinsObject.cpp` (`define_from_descriptor`) | [Phase 154](docs/phases/154-error-prototype-stack-is-an-accessor.md), [finding 173](docs/findings/173-a-bound-function-made-its-own-target-immortal.md) | Nothing to reproduce: the probe and its answers are in the finding. What the row was really pointing at was a collector bug, and it is fixed |
| | | | |
| ~~**The row about `align-self`, `align-content` and `justify-content` is in `AGENTS.md` twice.**~~ **Fixed in Phase 180 (`docs/phases/180-flex-and-grid-box-alignment.md`, finding 184):** deduplicated into the single closed Phase 180 row above (`uniq -d` confirms 0 duplicate rows). | `AGENTS.md` | `docs/phases/179-two-rows-the-merge-damaged.md`, `docs/phases/180-flex-and-grid-box-alignment.md` | Closed alongside the cross-axis alignment row |
| | | | |
| **The tree-walker's per-iteration cost is measured since Phase 204** (`docs/phases/204-the-tree-walker-pays-for-names-it-already-found.md`, finding 208): `make bench-js-core` (`tools/bench/bench_js_core.py`, new) runs six scripts — a loop, a property loop, a call, a recursion, an array and a method call — **inside a function** (a top-level `var` is a property of `window` here, and measures the global object instead: 748 ms against 355 ms for the same loop) and **with Node.js on the same box**, alternating the two binaries and reporting medians, because every number on a two-vCPU host moves ±20% run to run. On `3a0b600`'s engine the tree-walker is **3× V8 on recursion and array indexing, 9× on a loop, 18× on a property loop and 27.5× on a call** — so `ROADMAP.md`'s "50–100× on loops and calls" was right about calls and wrong about loops, and that row now says this. Phase 204 took the counter: `eval_update` walks the scope chain **once** for `i++` and writes the binding it found, where it used to build a `Reference` (a `std::string` and a `JsValue` copy) and walk twice — **`loop` 1.11–1.13×, `property` 1.07–1.09×, `array` 1.01–1.10×, and `call`/`method`/`recursion` unchanged.** A per-scope cache of the last four bindings looked up was also built, proved correct, measured at **1.04×/1.00×/1.13× for 176 bytes of every scope, and reverted**; so was a global version stamp (bumped 3,000,078 times in a 1M-iteration loop, because a call defines three or four bindings) and a cached stack floor. **The scope chain is not where a loop's time goes in this engine** — the copies around a `Reference` were. **Still open, in order of what it is worth:** a global `var` is written twice per assignment (the binding, then `set_property` on the global object, which `WindowExotic::set` and `assign_hook` find again); `invoke_function` builds a `shared_ptr` environment and a `define` per parameter and per `var` (~2.4 µs a call, the `call` row); `JsValue::set_property`'s exotic branch compares origins through a `std::function` per write; `js_check_stack` recomputes the thread's stack floor per expression (~2% of the profile, caching it unproven); a `Binding` is 80 bytes, 32 of them an uninterned name | `src/js/JsEval.cpp`, `tools/bench/bench_js_core.py`, `Makefile` | `docs/phases/204-the-tree-walker-pays-for-names-it-already-found.md`, `docs/findings/208-the-evaluator-refound-the-name.md` | The suite is `a_counter_is_written_where_the_walk_found_it` (postfix/prefix, a string counter, a property counter that is not the fast path, a shadow, an `eval` write — `1,2,`, as Node v22.22.3 prints — a global counter, the words of the `const`/TDZ refusals, per-iteration `let`, an undeclared name). `make bench-js-core` is the check; `BENCH_JS_BASELINE=<old binary>` compares two builds, `BENCH_JS_MAX_RATIO=` gates. The next change to take is the global `var` in `src/js/JsInterpreter.cpp`; the call frame is the bytecode VM's row |
| **The bytecode VM has a differential oracle since Phase 259** (`docs/phases/259-the-vm-nothing-called.md`, finding 263): `ROADMAP.md` row 7 asks for the VM as the single engine with the tree-walker kept as a differential oracle, and **the oracle did not exist**. `grep -rn eval_bytecode tests/ tools/` was **empty** — no test, no tool, no caller — so the compiler was free to refuse `var` outright (`Function-scoped var is not supported by the bytecode compiler`), which it did, making **every ordinary script uncompilable**; 8 of 12 probe cases were refused and the 4 that worked had no declaration in them. A subsystem nothing calls cannot fail, so it does not. `make verify-bytecode` is new: `tools/conformance/bytecode_probe` runs one script through `eval` and `eval_bytecode` on the same document and `tools/conformance/verify_bytecode_agreement.py` requires the two answers to be equal, over **28 cases that must agree and 7 the compiler still refuses**, each named (destructuring, `switch`, `try`, `for...of`, `typeof` and the other unary operators, object spread, tagged templates). A case that starts agreeing while listed as refused **fails the run**, as the pixel oracle's `FIXED` does, so the list shrinks by decision and cannot quietly grow. It was **shown to fail** before it was trusted: with `+` compiling to `OpSubtract`, 11 disagreement lines appeared. The compiler learned `var` (**a binding of the function, not the block** — `var a = 1; { var b = 2; } a + b` is 3; a `let`-shaped stack slot answered 1, because leaving the block pops the slot), the ternary, `do`/`while`, `break` and `continue`: **28/28 agree, 395 suites**. Three defects found, all silent and two of them infinite loops: a block-nested `var` gave 1 instead of 3; `break` patched before `emit_loop` pointed *at* the back edge, so `while (true) { …; break; }` never ended; and a forward operand written at `at + 1` instead of `at` (because `emit_jump` answers the *operand* offset, not the opcode's) wrapped a jump to **65297**. **Still open:** making the VM *the engine* is much larger — it has no closures, `this`, `arguments`, exceptions or iterator protocol, which is why `require_simple_function` refuses so much; and **nothing was made faster** by this phase, which is the honest result | `src/js/BytecodeCompiler.cpp`, `include/litebird/js/BytecodeCompiler.hpp`, `tools/conformance/bytecode_probe.cpp`, `tools/conformance/verify_bytecode_agreement.py`, `tests/suites/259-bytecode-agrees-with-the-tree-walker.cpp`, `Makefile` | `docs/phases/259-the-vm-nothing-called.md`, `docs/findings/263-the-vm-was-never-called.md` | The check is `make verify-bytecode`, and `--dump` prints every answer, which is how a case moves from the refused list to the agreeing one. Take the next statement kind from that list, and add its case to `REFUSES` first so the day it works is a failure that says so |
| | | | |
| ~~**A method's `this` is boxed before the call, so `typeof this` is never a primitive.**~~ **The call path is not the fault; this row was wrong, and Phase 201 measured it so (finding 205, `docs/phases/201-the-row-about-this-was-never-true.md`).** The engine already boxes only for **sloppy** functions and leaves a strict function's `this` as it was passed: `(function () { "use strict"; return typeof this; }).call(42)` answers `"number"`, `…call(true)` `"boolean"`, `…call(null)` `"object"`, and the sloppy form answers `"object"` — all four the same as Node v22.22.3. Fifteen call forms (plain, `.call`/`.apply`/`Reflect.apply`, `bind`, object and class methods, accessors, on a primitive receiver) agree with Node one for one. The row's example was wrong twice over: `Boolean.prototype.toString = function () { return typeof this; }` makes `(true).toString()` `"object"` **in Node too**, because that replacement function is sloppy and boxing is correct for it; in strict code both engines answer `"boolean"`. The two test262 cases it blamed fail for a different reason and belong to another row: `Array.prototype.toLocaleString` did not ask its elements for their own `toLocaleString`, so the overridden method was never called — `[true,false].toLocaleString()` answered `"true,false"` where Node answers what the method returns. See the `Array.prototype.toLocaleString` row. | `src/js/JsInterpreter.cpp`, `src/js/VM.cpp`, `src/js/JsOps.cpp` | ECMA-262 §10.2.1.2 (OrdinaryCallBindThis); test262 `built-ins/Array/prototype/toLocaleString/primitive_this_value.js`, measured against Node.js v22.22.3 | A change here must show a call form where a strict function's `this` is boxed, or a sloppy one where it is not. No such form was found: probe before believing the row |
| | | | |
| ~~**`Array.prototype.toLocaleString` never asks the element either.**~~ **Done in Phase 192 (`docs/phases/192-an-array-to-locale-string-asks-the-element.md`, finding 196):** `Array.prototype.toLocaleString` was `join_array(receiver, ",")`; it is now §23.1.3.28 — `Get` each place, skip `undefined`/`null`, and `Invoke(element, "toLocaleString")` with the element unwrapped (test262's `primitive_this_value` needs the strict `this` to stay a primitive). Both of those cases also needed `Object.prototype.toLocaleString`, which was `js_to_string(self())` and asked nothing: §20.1.3.5 is `Invoke(this, "toString")`, so `src/js/BuiltinsObject.cpp` is in the phase as well. Re-measured over `built-ins/Array` + `built-ins/TypedArray`: **3,375 → 3,380 / 4,536 (+5 / -0)** (the row said +3 for a narrower sweep), `built-ins/Array/prototype/toLocaleString` **9/12** with the three left gated on `BigInt`, `make test` **348 / 348**, js-conformance 1,146/1,158 and dom-conformance 678/678 unchanged. **Still open:** no `toLocaleString` in the engine takes `locales`/`options` or passes them on (`[1].toLocaleString("de")` is not a browser's answer), the typed array keeps its own walker, and the three `BigInt` cases need `BigInt` | `src/js/BuiltinsArray.cpp`, `src/js/BuiltinsObject.cpp` | Phase 177 / finding 182, Phase 192 / finding 196 | Closed by #238 |
| | | | |
| **`%Function.prototype%` does not carry the `caller`/`arguments` poison pill.** Phase 146 (`docs/phases/146-a-functions-name-and-length-are-its-own.md`) made `name` and `length` own properties and gave a sloppy ordinary function its `null` `arguments`/`caller` and its eager `prototype`. The standard's own arrangement is different: the pill is a pair of throwing accessors on `%Function.prototype%`, which is why `'caller' in f` is true for an arrow while `f.caller` throws. This engine answers the `HasProperty` question in `has_property` and the throw in `throw_if_forbidden_property`, so both answers are right without the object existing. Also open in that area: `%AsyncFunction.prototype%` and `%GeneratorFunction.prototype%` are not separate objects (`Object.getPrototypeOf(async function () {})` is `Function.prototype`, and `Object.prototype.toString` says `[object Function]` for both — `prototypes.dat`), and a *native* function lists `name,length` where the standard's order is `length,name` because `js_make_native` defines `name` first | `src/js/JsValue.cpp` (`has_property`, `throw_if_forbidden_property`), `src/js/JsOps.cpp` (`js_make_native`) | Phase 146 / finding 151; `prototypes.dat`'s async/generator case; 4 test262 `forbidden-ext` cases lost and regained in that phase | Build the pill as a real accessor pair on `%Function.prototype%` and delete both fictions. Order matters: `forbidden-ext` asserts `hasOwnProperty("caller") === false` on a strict function, so the pill must be inherited and never installed as an own property |
| | | | |
| **Hebrew fixed-position points are ordered before shaping (Phase 200, finding 204).** The #166 SHIN/QAMATS/SHIN DOT discrepancy is repaired: five reduced probes and twelve mark-order permutations agree with HarfBuzz in absolute placement. **Still open:** general normalization, ordering across cantillation/non-Hebrew combining marks, presentation-form fallback and special meteg sequences. | `src/render/FontRenderer.cpp`, `tools/shaping/` | `docs/phases/200-hebrew-mark-order.md`, `docs/findings/204-hebrew-mark-order.md` | Stable ordering of contiguous Hebrew fixed-position points only; raw glyph differences and pixel equivalence remain separate measurements. |
| | | | |
| ~~**`Array.from` reads an iterator's getter twice per call.**~~ **Done in Phase 225 (`docs/phases/225-array-from-reads-the-method-it-has.md`, [finding 229](docs/findings/229-the-iterator-method-was-read-twice.md)).** The iterable path asked `js_iterator_method` and then handed the source to `js_iterate`, which asked again; `iterate_with_method` in `src/js/BuiltinsArray.cpp` now runs the protocol with the method already in hand. `iteration.dat` answers `1,2 1 2 3 4` (82/84) and js-conformance is 1,149/1,158; the two copies of the loop (`iterate_with_method` beside `iterate_with` in `src/js/JsOps.cpp`) await the file being free of #261, as the row above says | `src/js/BuiltinsArray.cpp` (`iterate_with_method`) | `iteration.dat`, the case above; Phase 225 / finding 229 | Retired by Phase 225, landed on `main` while #261 was in flight |
| ~~**The label activation guard mistakes interactive content for label text.**~~ **Fixed in Phase 251** ([`docs/phases/251-label-activation-respects-interactive-content.md`](docs/phases/251-label-activation-respects-interactive-content.md), [finding 255](docs/findings/255-label-click-forwarded-through-interactive-content.md)). `after_click` already forwarded clicks through `forms::label_control`, with the `for`/first-labelable-descendant association and disabled-control check. Its exception used `has_activation_behavior` as a proxy for HTML's interactive-content category, however: `<button type=button>` is interactive but has no click default action, so clicking it inside `<label for=check>` wrongly activated `check`. `forms::is_interactive_content` now classifies that HTML category separately and the label guard uses it, without changing which elements receive activation behavior. A real `BrowserShell::click_at` suite confirms explicit `for` targeting, implicit descendant association, no second toggle when clicking the control itself, suppression for a `button type=button`, disabled controls, and no invented association for an unrelated nearby control. **Correction to the old H4 attribution:** the pinned TodoMVC template has `<input class=toggle>` beside `<label>{{title}}</label>` with no `for` or contained control. That label has no labeled control, so forwarding it to the checkbox would violate HTML. The holdout driver observes its `.toggle` hit-testing to the label and refuses the target mismatch; that separate hit-test/layout symptom remains, not a label-activation failure. | `src/dom/FormState.cpp`, `include/litebird/dom/FormState.hpp`, `src/js/DomFormBindings.cpp`, `tests/suites/251-label-click-activates-control.cpp` | holdout-01 H4, checked against the pinned TodoMVC template; HTML Standard, label activation and interactive content; Phase 251 / finding 255 | No arbitrary forwarding for an unassociated label. H4's hit-target/geometry issue needs a separate diagnosis; this phase makes no driver workaround |
| **`::before` and `::after` generate since Phase 215** (`docs/phases/215-the-icon-that-had-no-box.md`, finding 219): a string-valued `content` makes an inline run — an ordinary `TextBox` with no node, so `textContent` is untouched — appended before the children (`::before`) or after them (`::after`). Measured on the row's own oracle: the Rust Book's icon-only anchor was `800x0` with an empty inline box and is now `800x20` with a line and a text box, and `#a::before{content:"XXX"}` grows the line 30.2 → 63.1. **Still open:** only a string is represented, so `counter()`, `attr()`, `url()` and the content list generate nothing; `::first-line`, `::first-letter`, `::marker`, `::placeholder` and `::selection` match no box; and the glyph itself is not drawn when the fallback font has no such codepoint (U+F105 measures the 9.6px the font can draw), which is why the box existing is the claim and not the picture | `src/css/CssParser.cpp`, `src/css/StyleResolver.cpp`, plus layout and paint for the generated box (`src/layout/`, `src/render/`) | holdout-01 re-run 2026-11-23 (`tools/real_pages/holdout-01/README.md`) | The row that un-blocks icon-font-only controls — a large fraction of real-world buttons, links and menu items. Glyph-level correctness (the actual glyph) matters less than the box existing and being hit-testable |
| **The geometry a script reads disagrees with the painted layout.** ~~For the APG automatic-tabs biography panel, the after frame paints the paragraph at y≈560 while `getBoundingClientRect().top` reads **2061.4** … likely a stale-layout or origin-kind mismatch.~~ **CLOSED — Phase 243, finding 247.** The premise was wrong and the rect was innocent: on the pinned page the rect and the painted layout box agree to the decimal on every element (Δy = 0 for `#tab-1`, `#tab-2`, `#tablist-1`, `#tabpanel-2`). The real defect was that `getComputedStyle` read a second cascade (`DomStyleBindings.cpp::style_elements_of`) that collected `<style>` only — a `<link rel=stylesheet>` has no `text_content()`, so a page's linked CSS was painted but invisible to script, and `matches()` said `true` for a rule the resolver had never seen. Fixed by loading linked sheets through `DocumentStyles::load_linked_stylesheet`, the layout's own loader. **The holdout gate still fails**, for a new and honest reason: `#tabpanel-2 p` reads `top` 680.5 in a 600px viewport because the tabs lay out vertically (y 498/539/589) and `#tablist-1` reports `784 × 22.5` while its children stack to 181px — two separable `inline-block`/flex sizing defects now unmasked, and better claims than this row was. Oracle: `tests/suites/243-a-linked-stylesheet-reaches-the-script.cpp` (a linked and an inline sheet must answer the same, and agree with the layout; reverting the fix fails it on `linked.display == "none"`) | `src/css/DocumentStyles.cpp` (`load_linked_stylesheet`, `is_stylesheet_rel`), `src/js/DomStyleBindings.cpp` (`style_elements_of`, `sheet_of`, `sheet_text_of`) | holdout-01 re-run 2026-11-23 (`tools/real_pages/holdout-01/README.md`); measurement re-taken 2026-11-15 | Do not remove the assertion: it still catches a real defect, just the layout's rather than the rect's. The next claim on this page is the `inline-block` tab layout, not the geometry |
| ~~**`import(...)` does not parse.**~~ **Done in Phase 224 ([`docs/phases/224-import-is-a-call-where-a-paren-follows.md`](docs/phases/224-import-is-a-call-where-a-paren-follows.md), finding 228).** `await import('./specLinks.mjs')` in APG `shared/js/app.js` (line 52) throws `SyntaxError: Unexpected token 'import'` (the token is reached in expression position and `fail_unexpected` throws at `JsParser.cpp`), so the spec-link enhancement in app.js dies; the tabs widget survives (`tabs-automatic.js` is a classic script). ECMA-262 §13.3.12 makes `import(specifier)` an expression: the statement path now lets `import` through when `(` follows it (`import.meta` and `import x from` are still refused, with their own messages), `parse_primary` parses the argument into an `ImportCallExpr`, and `parse_new` refuses `new import(x)` while `new (import(x))` parses and is a run-time `TypeError`, as in Node v22.22.3. Evaluating it runs ToString on the specifier at the call site — a `Symbol` throws rather than rejecting — and answers a rejected promise whose `TypeError` names the specifier, because no module loader exists. **What is still open:** the evaluation half, in the sense this row meant it — nothing is loaded, so an imported binding is never there and `import()` always rejects; a second argument is a `SyntaxError` here where Node rejects with *The second argument to import() must be an object*; and `import.meta` stays refused | `src/js/JsParser.cpp`, `include/litebird/js/JsParser.hpp` (`ImportCallExpr`), `src/js/JsEval.cpp` (`NodeKind::ImportCall`) | ROADMAP tier 2 row on ES-module semantics; holdout-01 re-run 2026-11-23 (`tools/real_pages/holdout-01/README.md`) · Phase 224 | The parse half, as this row asked: the acceptance is that `import('./x.mjs')` parses and answers a rejected promise, and the ES-module row owns the loader. Strictly additive over the classic-script path: `import.meta`, `import x from` and a bare `typeof import` are refused exactly as before
| **`<dialog>` gets its UA box and centring since Phase 223** (`docs/phases/223-the-dialog-that-was-never-given-a-box.md`, finding 227): the UA sheet now carries HTML §Rendering 15.3.3's own dialog rule — `border: solid` (3px), `padding: 1em`, a background, and `width`/`height: fit-content` with `margin: auto` — and `:modal`, a real pseudo-class answered from the document's modal-dialog pointer, switches it to `position: fixed` with both block insets, so an open `showModal()` dialog is centred on both axes over the backdrop the painter already drew. Measured on the row's own oracle (holdout-01 H3's open frame): border 0px→3px solid black, padding 0px→16px, background transparent→white, placement in-flow top-left→fixed centred; the page behind it was already `(229,229,229)` and still is. Getting there needed three shorthands repaired (`border: solid` emitted no width, `inset` was accepted and dropped, out-of-flow auto margins were never solved). **Still open:** `width: fit-content` has no max-content measure behind it, so the dialog is as wide as its widest word (114.4px against Chromium's 265px); `::backdrop` is not author-addressable (the colour lives in the painter); the native `<select>` still paints nothing; `dialog:modal` does not carry the standard's `overflow: auto`, because `paint_atom` pushes its clip before the box's own chrome and would paint the dialog's border away — that paint-order defect is its own row | `src/css/StyleResolver.cpp` (UA sheet + shorthands), `src/dom/Selector.cpp`, `src/layout/LayoutBox.cpp` | holdout-01 re-run 2026-11-23 (`tools/real_pages/holdout-01/README.md`); HTML §Rendering 15.3.3; `oracle-frames/H3-ref-open.png` | Small and pixel-oracled. The gates already passed; this is the presentation half, and the open frame is now the oracle. Intrinsic width and the clip-order defect are the next rows, not this one |
| ~~**`with` statements are a parse error.**~~ **Done in Phase 246 ([`docs/phases/246-with-statement.md`](docs/phases/246-with-statement.md), finding 250):** sloppy-mode `with (expr) stmt` now parses (`WithStmt`) and executes through Object Environment Records (`JsEnvironment::make_with_scope`, ECMA-262 §14.11 / §9.1.1.2) while strict mode continues to reject `with` early (top-level `"use strict"`, strict functions, `new Function`, strict direct `eval`, and class bodies/static blocks) and single-statement lexical/class/function declarations are rejected. `with_object_has_binding` checks `HasProperty(O, N)` before consulting `O[@@unscopables]`, exercising Phase 233's 16-name `Array.prototype[Symbol.unscopables]` table ([finding 237](docs/findings/237-there-was-no-unscopables-table.md)) so outer bindings win over unscopable array methods; `eval_call`/`eval_tagged_template` pass `WithBaseObject()` as `this`; `eval_assign`/`exec_var_decl`/`bind_pattern`/`eval_update`/`typeof`/`delete`/completions/generators/`async`/`await` and GC root/cycle tracing all support `with` scopes with zero overhead outside `with` (`has_with_ancestor()`). Oracle: `python3 tools/conformance/verify_with_statement.py` (CI-wired, **19/19** match Node.js v22.22.3 including TodoMVC's runtime-compiled template pattern) and `tests/suites/246-with-statement.cpp` | `include/litebird/js/JsParser.hpp`, `src/js/JsParser.cpp`, `include/litebird/js/JsInterpreter.hpp`, `src/js/JsInterpreter.cpp`, `src/js/JsEval.cpp`, `src/js/BuiltinsArray.cpp`, `src/js/BuiltinsObject.cpp` | ECMA-262 §14.11, §9.1.1.2, §23.1.3.34; [`docs/phases/246-with-statement.md`](docs/phases/246-with-statement.md); [`docs/findings/250-with-statement-and-object-environment-records.md`](docs/findings/250-with-statement-and-object-environment-records.md) | Closed. Note: ES2024 / Node.js v22.22.3 do not define a separate `%TypedArray%.prototype[@@unscopables]` property (`undefined` on `%TypedArray%.prototype`) |
| ~~**A checkbox paints the literal string `on`.**~~ **Done in Phase 232 ([`docs/phases/232-a-checkbox-no-longer-paints-on.md`](docs/phases/232-a-checkbox-no-longer-paints-on.md), finding 236).** In TodoMVC's added-item row `<input class="toggle" type="checkbox">` rendered as a small box containing `on` (holdout-01 H4-added frame) — the default value for checkbox/radio is "on" (`FormState.cpp:832`, `ValueMode::DefaultOn`) and `BrowserShell.cpp`'s overlay painted that string into every `<input>`. The overlay now branches on `forms::input_type`: `checkbox`/`radio` render a bordered box (3px radius for checkbox, circular for radio) with a checkmark/dot when `forms::checked` is true, and never paint `value()` as text. Text fields keep the existing rounded box + value/placeholder rendering. Verified by `a_checkbox_does_not_paint_the_string_on` (`tests/suites/229-checkbox-does-not-paint-on.cpp`) and the H4 frame no longer contains the text "on" | `src/shell/BrowserShell.cpp` (the form-control overlay), `src/dom/FormState.cpp` (`ValueMode::DefaultOn`), `src/render/` (intrinsic control painting) | holdout-01 re-run 2026-11-23 (`tools/real_pages/holdout-01/README.md`) | `appearance: auto` per engine; `FormState::value()` still returns "on" for submission, only the painting changes |
| ~~**`ResourceLoader::resolve_url` drops the base document's last path segment for query-only references.**~~ **Fixed in Phase 221** ([finding 225](docs/findings/225-query-references-lost-the-filename.md)): query-only, fragment-only and empty references preserve the base path; absolute URL bases use the URL parser and relative filesystem-style bases keep their path. A direct Node URL differential agrees on 50/50 pathless cases (1/50 before), and holdout-02 H4's real link click now navigates and fills the sorted, decoded parameter table. **Phase 231** ([finding 235](docs/findings/235-relative-path-resolution-was-string-splitting.md)) resolves path-bearing references against absolute hierarchical URLs with the URL parser: 132/132 agree with Node, up from 22/132; dot segments cannot turn the host into a path segment. **Still open:** legacy relative filesystem-style and opaque bases, absolute-reference passthrough and full anchor same-document navigation. | `src/net/ResourceLoader.cpp`, `tests/suites/221-pathless-references-keep-the-document.cpp`, `tests/suites/231-path-bearing-url-references.cpp`, `tools/conformance/verify_relative_navigation.py`, `tools/conformance/verify_path_resolution.py` | `docs/phases/221-query-references-keep-the-document.md`, `docs/findings/225-query-references-lost-the-filename.md` | H4 is a now-exposed regression check, not a fresh compatibility score |
| ~~**Tab advances a ring index without ever calling `focus()`.**~~ **Done in Phase 228 ([`docs/phases/228-the-tab-key-moves-the-focus.md`](docs/phases/228-the-tab-key-moves-the-focus.md), finding 232).** `focus_next()`/`focus_prev()`/`focus_element()` (`src/shell/BrowserShell.cpp`) increment `m_focused_index` over `m_interactive_elements` and only set `m_dirty` — no `focus()`, no `blur()`, no dispatched events. Measured (holdout-02 H2): after clicking `#number1`, typing and two Tab presses, `document.activeElement` is still that input. They now move the document's focus through the bindings' own `dom::focus_element` — the path `element.focus()` uses — so `document.activeElement` follows the ring and `blur`/`focusout`/`focus`/`focusin` reach the page's listeners and `:focus` matches; the index became only where the walk stands between presses, reconciled with `document.focused_element()` first, so a script's own `el.focus()`, a removed stop or an inert one sets where the next Tab starts. Measured on the same page: `BODY` before the first Tab, `#number1`/`#number2`/`#go` after the first three, the page's log in step. **Still open:** the ring's stops are not `is_focusable` — `tabindex` elements, `select`, `textarea`, `contenteditable` and `summary` are reachable by `focus()` but not by Tab; the `change` event the H2 gate fails on is not fired by the blur path (the control's value is unchanged by a focus move, so `change` is a listener's to see only where the platform would fire it); a click does not move the index; and Shift+Tab is the terminal's own escape sequence, not a key `handle_key` carries | `src/shell/BrowserShell.cpp` (`set_document_focus`, `forget_focus_if_gone`, the focus ring), `include/litebird/shell/BrowserShell.hpp` | holdout-02 (`tools/real_pages/holdout-02/README.md`) · Phase 228 | Fix must dispatch, not just move a number — the H2 gate fails precisely at the missing `change` |
| **Adjacent Latin composition asks the font (Phase 234, finding 238).** Unicode canonical pairs are composed before lookups only when the current face has the result. The #284 Latin raw and ink discrepancy is repaired without a font-specific offset. **Latin mark ordering and composition across nonblocking marks are fixed in Phase 266 (finding 270)**, measured by eight exact HarfBuzz matches. Missing-composite decomposition landed in Phase 240. Still open: decomposing supported composites before later marks, leading-mark ordering, non-Latin normalization and source clusters. | `src/render/FontRenderer.cpp`, `tools/shaping/` | `docs/phases/234-latin-font-aware-composition.md`, `docs/findings/238-latin-composition.md` | Font-aware adjacent composition only, not full NFC or browser pixel conformance. |
| **Missing Latin composites use covered canonical parts (Phase 240, finding 244).** Same-face canonical decomposition removes .notdef only when every part is covered; existing glyphs stay intact. **The stacked-mark discrepancy is fixed in Phase 250 (finding 254):** executing GPOS in lookup order instead of type batches makes all seven reduced cases match HarfBuzz. | `src/render/FontRenderer.cpp`, `tools/shaping/decomposition_diagnostic.py` | `docs/phases/240-latin-covered-decomposition.md`, `docs/findings/244-missing-latin-composites.md` | The seven-case diagnostic now passes; general normalization, source clusters, script-specific feature staging and cross-font fallback remain separate. |
