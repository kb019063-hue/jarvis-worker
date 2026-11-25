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
- **The two numbers are the pull request's own number** since 24 November
  ("Before starting", point 2), not read from either document.
  `python3 tools/next_ids.py` reads the documents, `docs/`,
  `docs/state.json` and the open pull requests: `--pr-body` prints the block
  a pull request opens with, `--sources` says where each number came from,
  and `--taken` lists every number taken. Before that day it handed out the
  next free pair, which reserved nothing: a number was taken by opening the
  pull request, and two that took the same one could at least be seen in
  `gh pr list` instead of being hidden in two documents.
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
merges each as it is. No test is given up for that: `main` still only ever
gets a tree that a whole run passed on, file for file, and a pull request
whose own run fails is not merged. What is given up is the runs that
tested a tree nobody was going to merge. (Until 24 November a pull request
also had to pass its own run before it was taken; since then the two runs
go side by side: point 4.)

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
   **Take `WIP:` off with your last push, and not before.** While a watch
   is on, the pull request can be taken from that moment, while its own
   run is still going; a title that loses `WIP:` before the work is all
   pushed gets half the work merged.
   - **`do not merge: a watch is on …`** The landing session takes #N as it
     is. Push nothing more to it and do not merge `main` into it, however
     far `main` has moved: either would start a run of your own and move
     the head under the one that is testing it. That goes for an empty
     commit "to start CI" as well: when the last comment on #N says it is
     taken, it is, run or no run. Wait with
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
     says in two to four minutes what the run will say in five about the
     unit suites of the plain build;
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
   - **a finished pull request is taken while its own run is still
     going.** Until 24 November it had to be green on its own head first,
     which put a run's length, 10 to 13 minutes that night, in front of
     the run on the lot. Now the two go side by side, and it lands when
     the lot's has passed. If its own run fails, `watch.sh lot` says so
     and the lot is built again without it: the innocent ones in that lot
     lose the minutes it had run, and that is the price. A head with no
     run at all is not taken this way (a claim's empty commit under a
     title without `WIP:` would be merged as the pull request);
   - **a head whose code was green is taken without waiting for its own
     run**, the same rule made general (`code_was_green` in
     `tools/watch/watch.sh`): a commit of the branch before it has a green
     run, and the head is that commit merged with `main` by git alone,
     with changes to documents only on top. That is an agent that merged
     `main` itself, or finished a write-up, or took a line out of
     `AUDIT.md`. The lot is `main` with that head merged in, the same
     tree whether its own run has ended or not. A head with a failed run
     is not taken;
   - **a head that moves under a lot does not always end the lot.** When
     its agent only merged `main` into it, `main` and the heads as they
     are now still give the tree the run is testing; `watch.sh rehead`
     checks that, and the lot goes on with the new head. In the first
     quarter of an hour of 24 November's watch three heads moved under
     one lot (#305, #313, #321) and none of them cost a run;
   - **a pull request's own run is the short one, watch or no watch.**
     Every push to it runs the jobs of the plain build that take a
     minute or two: "Build & unit tests", "Documentation shape", the
     reftests and the four corpus jobs (html5lib, JavaScript, script DOM
     and URLs, XML and web-platform-tests). The eight sanitizer jobs and
     the deep pages are skipped. The whole of CI runs on the lot it
     lands in, on `main` as it will be, which its own run never saw.
     **Jobs "skipped" on your pull request are that, and nothing to mend**;
     `python3 tools/ci/check_pr.py merge N` says so. You still run the
     checks of "What "done" means" here before the last push: a pull
     request that fails a job in the lot takes the others in it back a
     run, and the lot's run names the job, not the pull request. For its first
     four hours the short run was two jobs, and on 24 November two lots in a
     row failed for it: #368 had changed what the tree dump writes and failed
     one case of the html5lib corpus, its own run was green because that job
     was not in it, and which member of the lot it was had to be found by
     building each alone, while five others waited. With the corpus jobs in its
     own run, such a pull request fails by itself two minutes after its push.
     When no watch is on, `merge N` starts every job on your head (it
     puts the label `lot` on and takes it off: a run that taking a label
     off started is the whole one) and says "not ready" until they have
     passed: nobody merges on a short run. The reason is the queue: a run was
     twelve jobs and no more than twenty were seen running at once. At
     04:57 on 24 November 19 were running and 25 waiting, in
     six runs, and the lot that five finished pull requests were in had
     eight jobs that had not started 4.7 minutes after its push; it took
     17 minutes where the one at 05:24, alone on the runners, took 10.5.
     A pull request was tested three times: by its own run, by the
     lot's, and by the run on `main` after the lot. That last one now
     builds, keeps the objects and runs the plain unit suites, no more
     (`[objects only]` in the lot's last merge);
   - **it was not always every push.** Until 25 November a run was
     short only under the label `lot`, which the landing session put on
     at its next look, or under a title that began with `WIP:`; any
     other push started every job. At 18:10 on 24 November three pull
     requests that had just been opened each started all fourteen jobs,
     and the lot of 18:12, which a finished pull request was waiting
     for, had ten jobs without a runner two and a half minutes after its
     push: that brought the `WIP:` rule. On 25 November #431 was opened
     finished, with no `WIP:` to its name, and began all sixteen jobs
     of a run at 06:53; the lot it went into was pushed at 06:57, and
     at 06:58 twenty jobs had a machine and eleven waited, five of them
     the lot's. In the lot before, eleven of eighteen jobs had waited
     37 to 95 seconds. Nobody reads the long jobs of a pull request's own
     run, so no push starts them now. Work in progress is merged on no
     run at all. When the `WIP:` is off, every job runs before the
     merge: in the lot while a watch is on, and with no watch because
     `merge N` starts them on your head. A sanitizer job you want to see
     on work in progress: `make asan-build` and the part here (`make
     help` lists them);
   - **when a lot's run fails, every member that could be at fault stays
     out of the next lot until it is known which one it was.** The run
     names the job, not the pull request. On 24 November the lot of 08:38
     failed in the unit suites and in the web-platform-tests; the landing
     session took out the one member whose suite had crashed and kept
     the other two, and the lot of 08:44 failed again, which cost three
     newcomers six minutes. Which member it was is found by building each
     alone with `main` and running the target that failed (three to four
     minutes each in the sandbox) while the lot of the others is in test;
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
     ends its access within minutes. What it writes itself is committed
     in a second worktree (`git worktree add -b own /tmp/own
     origin/main`), and `watch.sh full N…` builds the lot with those
     commits on top, so that the watch can end at any lot that has
     passed without one more run. **Only the watch's tools, CI's
     configuration and documents ride on a lot that way.** The members
     are then merged as they are, and `main` has their tree, which
     differs from the one the run passed on in those files alone. **A
     carried pull request rides on the last lot only**, the one that
     lands whole with the watch's own merge. On 24 November #331, #326
     and #332 were merged as they were from two runs that had #317's and
     #324's code on top: for forty minutes `main` had trees no run had
     passed as they were, and nothing came of it only because nothing in
     them leaned on those two;
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

   A conflict in the table "Open tasks" is carried by
   `tools/watch/carry_rows.py`: it reads from the pull request's own diff
   which row it rewrote and which it added, does that again to the table
   as it is in the lot, and refuses anything else. And since a watch can
   pause, a pull request is carried too when its agent is there and it
   has gone back twice for a document: on 24 November #317 went back
   twice in a quarter of an hour (03:32 and 03:44 UTC) for empty rows it
   had put in by hand, each time against a table that had moved on.

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
   `WIP:` pull request with no push for two hours, and no comment in its
   agent's name anywhere in that time, is marked `QUIET` by `python3
   tools/ci/check_pr.py`. It is still a claim, and not yours to take or
   to close: ask in a comment on it, and tell the person you work for,
   who can continue its chat or say that it is given up.)
2. **Claim before doing the work.** Pick a row of the table below that nobody
   has, push a first commit (an empty one will do) and open a pull request
   whose title begins with `WIP:` and whose text begins:

   ```
   Task: <the row of the table>
   Files: <what it will change>
   Phase and finding: this pull request's number
   Agent: <the name you gave yourself; "Working together", above>
   ```

   **Your phase and your finding are the number of the pull request
   itself**, the one `gh pr create` prints when it has opened it: #360 is
   Phase 360 and finding 360, `docs/phases/360-….md`,
   `docs/findings/360-….md`, `tests/suites/360-….cpp`, and its title
   says so when the `WIP:` comes off. Nobody has to be asked, because
   GitHub gives no two pull requests the same number.
   (`python3 tools/next_ids.py --pr-body` prints the four lines, and
   `python3 tools/ci/check_pr.py claim N` says the numbers back.) Until
   24 November 2026 the two came from `python3 tools/next_ids.py`, the
   next free number of each, and two agents who asked in the same minute
   took the same pair: in one hour of that morning five pull requests had
   a number another had said first (#336 twice, #347, #354, #357) and
   were renumbered after they were finished, files, rows, title and all,
   one of them twice under a lot that was being tested. The numbers are
   no longer one after another, and nothing needs them to be. A pull
   request opened before that day keeps the numbers it has. A change that
   is not engine work —
   documents, tooling, CI — takes neither: write `No phase, no finding` and
   say why instead.
3. **One task at a time, inside its files.** A bug found outside the task
   becomes a new row of the table; it is not fixed in passing. If the work
   turns out to need a file of `main` that another open pull request names,
   **go on, and change no more of that file than the task needs**: the
   one that lands second merges `main` (point 4). Until 25 November 2026
   the rule was to stop and tell the person you work for, and the first
   agent it stopped closed a finished pull request for one function of a
   file somebody else had named ("What has gone wrong", Claims). **Whatever
   stops you, push what you have under `WIP:` first**: a patch in `/tmp`
   goes with the sandbox.
4. **Look again once the pull request is open.** Two agents that look at the
   same moment both see the same file free, and each of them did look
   first. `python3 tools/ci/check_pr.py claim N` lists what #N has in
   common with the other open pull requests. Of two that would **make**
   the same file, the one opened later takes another name, and the
   earlier one changes nothing. A file `main` has can be changed by both:
   `claim N` names the pull requests that share it, and if the one that
   lands first leaves the other unable to merge, the other merges `main`.
   A pull request holds the files it has changed as well as the ones it
   names. Put `[skip ci]` in the message of the first commit: an empty
   commit needs no run, and a run is an hour of runner time.
5. **Push the work as you go.** Commit and push what you have every time
   it builds and its suite passes here, and at least every twenty
   minutes, with `[skip ci]` in the message: such a push starts no run
   and costs nobody anything, and the work is then on GitHub whatever
   happens to the chat. A sandbox can end without warning, and what was
   not pushed ends with it; and from outside, work that is only in a
   sandbox cannot be told from no work at all ("What has gone wrong",
   Claims: #367, #389). What is pushed, the same chat or another agent
   can go on from. **The last push, the one `WIP:` comes off after, has
   no `[skip ci]`**: a head commit without a run is finished for nobody.
   **When a push fails, the work is still only in the sandbox.** Try
   again, then try `gh`, which talks to another host than `git push`
   does: `git diff origin/main > /tmp/p.diff`, and the diff as a comment
   on your pull request (`gh pr comment N --body-file`, the diff inside
   a code block). If that fails too, your reply begins by saying so, and
   carries the diff if it is small: the reply is the one thing that
   outlives the sandbox for certain, and the next turn can read it
   ("What has gone wrong", the sandbox: #419).

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
     `python3 tools/ci/check_docs.py` notes two rows that touch. **That
     note asks nothing of you.** The landing session puts in a missing
     empty row at the end of a watch (`check_rows.py --write`, which
     stays clear of the rows that open pull requests hold). One put in by
     hand beside another task's row is a change that touches that row: on
     24 November #317 put in eleven, and was in conflict with three
     other pull requests for it.

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
  here first. When a job of CI fails all the same, **its last step says
  what failed in the job's annotations**, which the API itself serves,
  and **`python3 tools/ci/run_report.py N` reads them for you**: which
  job of #N's newest run failed, in which step, and what it said
  (`--run RUN` for another run; `--times` for where a run's time went,
  job by job, and how long its jobs waited for a machine).
  Every job does that since 24 November (`tools/ci/annotate_failure.py`;
  the steps of the jobs that are not sanitizer jobs run in
  `tools/ci/logged.sh`, which keeps a failed step's output for it). Until
  then "Build & unit tests" said `exit code 2` and no more: #364's agent
  was told which suite had failed, and at which line, by the landing
  session, which had built the pull request again to find out. In a
  sanitizer job the name of the step that failed is a
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
- The `WIP:` comes off the title, and CI is green on the head commit
  (a push's run is the short one: "Working together", point 4). Then
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
python3 tools/ci/check_pr.py            # what each open pull request claims, and how old its last commit is
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
- **A whole directory in a `Files:` line.** `check_pr.py` reads a name
  that ends in `/` as every file under it. On 25 November #390 named
  `tests/suites/`, `docs/phases/` and `docs/findings/` so, and `claim N`
  told each of the six pull requests opened after it that #390 "had"
  its phase, its finding and its suite first, and to stay out of them.
  Those three directories are where every pull request adds a file of
  its own; named bare they claim nothing now, and the pull request that
  named them is told so. A directory that is yours alone is still
  claimed whole.

  The advice that stood here then, "name your own files
  (`docs/phases/N-….md`)", made the next one. The `Files:` line is
  written when the pull request is opened, before its number is known,
  so #415 and #421 both wrote `docs/phases/N-*.md`, `docs/findings/N-*.md`
  and `tests/suites/N-*.cpp`, letter for letter, and at 06:09 the same
  morning `claim 421` said #415 had the three first and that two pull
  requests cannot both make them. #421's agent wrote on the board that
  it would add no phase, finding or suite while they were held. Nothing
  was held: a name there that is written before its number is no file's
  name, and claims nothing now. **Your own phase, finding and suite need
  not be in the `Files:` line at all**: they begin with your number, and
  nobody else has it. The line is for the files of the engine and the
  tools, which somebody else may want too.
- **A file under another pull request's number.** #393 was written as
  Phase 387 while #387 to #392 were being opened, and was finished with
  a phase, a finding and a suite called `387-…`. The landing session
  took it; its agent renamed the three two minutes later; a lot can
  only land the commit it was tested with, so it was built again, with
  #388 waiting in it. **The number is the one GitHub gave the pull
  request, and the three file names begin with it**: `claim N` refuses
  any other, and the landing session takes no pull request that has
  one. The title carries the number into the merge commit: #393's still
  said 387 when its lot had passed.
- **A claim that did not say it was one.** #423 was opened on 25
  November at 06:19 as "Claim: full `<html>` root layout box …": one
  empty commit, no `WIP:` in its title, none of the four lines. To
  every tool that is a finished pull request, and four minutes later
  the landing session's list had it as "can be merged as it is, at
  once". Merged, it would have ended its agent's branch before the work
  began. The landing session put the `WIP:` on and said so on it; and
  `check_pr.py` now takes no pull request that changes no file, whatever
  its title says. **`WIP:` is what says "not yet", and nothing else
  is.**
- **A finished pull request, closed for a file.** #390 was finished and
  measured in its agent's sandbox (two more test262 cases, two more of
  the JavaScript corpus, nothing lost). It needed one function of
  `src/js/JsInterpreter.cpp`, which #387 had named eight minutes before
  #390 was opened; `claim 390` said "stay out of it, or tell the person
  you work for"; its agent kept a patch in `/tmp` and closed the pull
  request, 23 minutes after opening it, with nothing pushed. #387 landed
  45 minutes later. The rule had been written for agents that merged
  their own work and paid for every conflict; with more than ten agents and a
  landing session it stopped work that would have merged. **A file of
  `main` that two pull requests change is no longer a refusal**
  ("Before starting", points 3 and 4): `claim N` says who shares it, and
  the one that lands second merges `main`. A file both would make is
  still the first one's. And nothing finished stays in a sandbox: push
  it under `WIP:`, whatever is in the way.
- **A claim nobody pushed to, and what the landing session did to one.**
  #367 was opened on 24 November at 07:58 with a commit that claims and
  nothing else, and was not pushed to again. When the person we work for
  went to continue its chat the next day, its sandbox and its tools no
  longer worked, and whatever it had done was gone with them. #389 was
  opened on the 25th at 02:37 in the same way. At 05:16 the landing
  session, asked to take care of the claims that were still quiet,
  closed it: nothing had been pushed for 2 hours 40 minutes, in which
  21 pull requests had gone from opened to landed in 9 to 76 minutes
  each. It had read #389's own page and nothing else. #389's agent had
  written a comment on #416 thirteen minutes before. #389 was opened
  again at 05:30; nobody had taken its row in between. Two things,
  then. **Push as you go** ("Before starting", point 5): work in a
  sandbox is lost with the sandbox, and until it is pushed nobody else
  can tell it from no work. And **no push is not no agent**: the list
  that `check_pr.py` prints now says beside each claim when its agent
  last wrote anywhere, and marks `QUIET` only a `WIP:` pull request
  with neither a push nor such a comment for two hours. That one is
  still a claim: to ask about, not to take, and not to close on the
  strength of its own page.

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
- **A run of every job is eighteen jobs and 36 to 41 minutes of runner
  time; the short run of a pull request is seven jobs and six minutes**
  (25 November, with the objects in ccache: runs 45557484052,
  45558414164 and 45552164646), and more by how much of the engine it
  has to compile (how long it takes is two points down).
  **The mark works wherever it stands in the message**, in a sentence
  about it too. On 24 November #313's commit "re-run CI on the merged
  head" explained in its message why the commit before it had carried
  `[skip ci]`, so it carried it too, started nothing, and the pull request
  lay finished for 25 minutes; the landing session's own push had done
  the same two hours earlier. Write "the mark" when you write about it.
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
  yet, was 13.8 minutes (run 45307390078). The first with the objects was
  8.2 (run 45310301051), its longest job 6.7: the step "Build" took 3 to
  7 seconds where it had taken 85 to 158, the sanitizer build 10 to 31
  where it had taken 292 to 377, and the run 33 minutes of runner time
  where it had taken 75. When the runners are busy a run waits for them:
  the first six lots of that night took 11.6, 13.0, 10.2, 10.5, 14.5 and
  14.7 minutes from the push to the last job, the last two beside the
  runs of the pull requests in them and a run on `main`. And the runners
  are not equally fast. The step that runs `make deep` took 4 min 22 s
  on one and 8 min 11 s on another twenty minutes apart, and on the slow
  one a page that takes 33 of its 60 seconds on a quiet machine ran out
  of them: #317's run failed for a change that had touched nothing of
  it, with `exit code 2` and no other word, and its agent pushed an
  empty commit to have the run again. Since then a run of
  `tools/fuzz/deep_pages.py` that goes over the time limit beside the
  others is run once more by itself before it counts, and the job writes
  what failed as annotations like the sanitizer jobs do. **A check that
  measures time fails on a slow machine: give it the machine to itself
  once before it says so.** A lot's run saves its objects for the
  lot after it (a watch's pull request is the one pull request whose
  runs save any): the next lot is pushed a minute after the last one
  landed, before `main`'s run has saved what landed, and at 04:13 that
  cost 4 min 49 s of compiling in each sanitizer job. A run on `main`
  saves the objects for everybody else's runs, which is one more reason
  for the last merge of a lot to start one. A job that cannot fetch ccache
  compiles everything, as before.

  With the compiling gone a run is as long as its longest job, and on 24
  November that was the unit suites in the sanitizer build: one process,
  8 min 52 s of a run of 10.4 minutes, on a machine with four cores. CI
  now runs them as **several processes side by side**
  (`make asan-unit ASAN_UNIT_STRETCHES=balanced`): told to,
  `tests/test_suite.cpp` runs one stretch of its order, and
  `tools/ci/run_stretches.py` passes when every stretch passed and they
  add up to every suite. `make asan-unit` here is one process, as it was:
  one stretch alone holds 4.8 GB. The deep pages of the sanitizer build
  are a job of their own ("Sanitizers: deep and oversized pages"), which
  with the web-platform-tests in front of them was 9 min 43 s. **A suite
  must not need a suite before it**: the first suite of a stretch has
  none. Three rules keep it the same checks:
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
  - **`asan-fuzz` needs the documents of the W3C XML suite unpacked before
    it.** The XML fuzzer mutates them (`build/xml-conformance/`). Without
    them it does not fail: it mutates the dozen documents written in the
    script, passes, and has checked far less. The one job ran `asan-xml`,
    which unpacks them, in front of it, and nothing said that it mattered.
    The fuzzing is a job of its own now, which unpacks them for itself
    (`run_xml_conformance.py --unpack-only`), and the check above requires
    that step or `asan-xml` before `asan-fuzz` in the same job.

  The jobs were divided once more on 24 November, by the times of two
  runs. In the lot of 07:41 (run 45350008834, 12.1 minutes) the longest
  job was the XML suite with the fuzzing, 12.0 minutes: 5.2 of build, 2.2
  of suite, 4.3 of fuzzing. In the next, whose objects were all in ccache
  (run 45351810983, 8.1 minutes), it was the script corpora with the
  verifiers, the generated XML and the string stress tests, 8.0 minutes,
  beside a web-platform-tests job of 3.6. Now the fuzzing is alone, the
  XML suite runs after the web-platform-tests, and the verifiers, the
  generated XML and the string stress tests are a job of their own: six
  sanitizer jobs, fourteen jobs in a run. In the first run with them
  (45356130854, 9.2 minutes from the push) those took 5.8, 5.2, 5.0 and
  4.3 minutes, and the longest job was the unit suites again, 8.9
  minutes, of which the suites were 7.6.

  That evening the unit suites were measured, and the time was not where
  anybody had looked. Of the step's 466 seconds, 146 compiled the suites'
  binary (one translation unit, one compiler) and 320 ran the suites; of
  three equal stretches one took 320 seconds and the others 22 and 24,
  because the suites that nest pages 200,000 deep stand together in the
  middle of the order (79, 37 and 30 seconds). Since then:
  - the job ends with **a notice that gives each stretch's time and
    memory, its slowest suites, and the suite at which half of its time
    was over** (the job's annotations, as for a failure);
  - the stretches begin where `UNIT_SUITE_CUTS` in the Makefile says, and
    the two that hold the deep suites start when the others have ended
    (`UNIT_SUITE_LATE`);
  - **a suite that measures time gives itself room.** Some fifty places
    say "this takes a fifth of a second: fail above five". In the
    sanitizer build, beside a process that was turning over 4.8 GB, one
    of them took more than five (run 45459129005; it cost #376 nine
    minutes, and the fault was the landing session's, which had put the
    deep suites beside it). A stretch that fails beside the others, with
    no sanitizer's report in what it wrote, is now run once more by
    itself, and passes with a warning on the job if it passes then. A
    bound that is a multiple of something the suite measured in the same
    run needs no second chance;
  - the suites' binary is an object of its own, compiled beside the
    engine and not after it: `make -j2 test` here does the same;
  - the sanitizer build is made without GCC's variable tracking
    (`-fno-var-tracking`), a pass for debuggers that took more than half
    of the compile time of the two files it was measured on here (in CI
    an object then took 2.3 to 3.0 seconds where it had taken 3.5 to 4.3,
    in runs that were not otherwise alike);
  - the XML suite is a job of its own again, and the deep pages are two
    jobs (the dumps; the terminal and html5lib): sixteen jobs in a
    run then, seven in the short run (eighteen since the next
    morning, below).

  With all of it a run of every job, its objects in ccache, was 5.3
  minutes from the push (run 45473629683), where the first of that
  evening had been 8.5 (run 45451780819). The unit suites were 117
  seconds, as six stretches: four side by side, then the two deep ones.
  The four took 236 seconds between them for what two had done in 162, so
  more processes than that buy little; and the longest job is now the
  script corpora of the sanitizer build, 5.2 minutes, of which 88
  seconds were its build.

  The run after that one was 6.4 minutes (45475379777), and it is the
  one to remember. Its machine was slower: the five stretches that can
  be compared took 1.4 to 1.8 times as long. The suite that had failed
  at 18:24
  (`inner_html_is_parsed_as_the_content_of_its_element`, "under five
  seconds") failed again, this time beside three ordinary stretches; its
  stretch was run again by itself, passed in 69 seconds, and the job
  passed with a warning that named the suite. **When that warning names
  a suite, give the suite's bound room in the sanitizer build**
  (`#if defined(__SANITIZE_ADDRESS__)`, as that suite now has, and as
  the large XMLHttpRequest response had before it): a warning that comes
  with every other run is one nobody reads, and the second run of the
  stretch is a minute of every run it comes with.

  A third run, with that bound changed, needed no second run of anything
  and was 6.0 minutes (45477108702), and its four stretches side by side
  took 91 to 121 seconds where the first run's had taken 55 to 64. **What
  the suites take depends on the machine by a factor of two**: 117, 176
  and 204 seconds for the same six stretches in three runs within half
  an hour. One run says little about a change to CI; compare several.

  On 25 November two things were found in a run's notices and warnings
  that no failure would ever have shown, because nothing failed:
  - **the sanitizer jobs were compiling what nothing had changed.** The
    saved objects were kept under a limit, the directory was always at
    it, and what ccache removed to stay under it included 8 to 21 of
    the 107 objects the last build had used. Every sanitizer job then
    compiled those again: 20 to 88 seconds each, where the step takes 3
    to 5 when every object is there. The job that saves now keeps what
    its build used and nothing else (0.11 GB where 0.5 was fetched);
  - **one job in fourteen was refused ccache's binary** by the project's
    release page, compiled the sanitizer build from nothing, 300
    seconds, and made its lot 9.7 minutes. It warned, as it is meant
    to, and nobody read warnings. The binary now comes with the saved
    objects.

  The same morning the three longest jobs of a run were taken one
  after another, each found by the times of its steps and nothing else:
  - **the fuzz run's six parts were six lines of one recipe**, one
    after another on a machine with four cores: 237 and 245 seconds.
    They are six targets now, and `make -j` runs them side by side:
    138 and 129 seconds. `make fuzz` alone does what it did;
  - **the two deep stretches of the unit suites waited for the other
    four**: 72 and 85 seconds, begun when those had taken 102 and 127.
    They have a job of their own, "Sanitizers: unit suites, the deep
    ones" (`UNIT_SUITE_ONLY`, the Makefile), and took 54 to 93 seconds
    there with the machine to themselves;
  - **the deep pages' second job gave every page to the engine twice**
    (the terminal, html5lib): 244 to 247 seconds, the longest job of
    the run that landed #407. They are three jobs, one for each way,
    and took 116, 110 and 74 seconds (run 45558414164).

  **Eighteen jobs in a run, seven in the short run.** A run with its
  objects in ccache was 5.9 minutes before the three (45554494852),
  then 4.7, 4.8, 4.5 and, with all three, 4.8 (45555583953,
  45556761844, 45557484052, 45558414164). In three of those four the
  longest job was the web-platform-tests of the sanitizer build, 267
  to 270 seconds; in the 4.5 they took 154, on another machine, and
  the deep pages were the longest. What is left is the machine. A job
  cut in two would cost a build more and a place among the twenty
  that run at once, which a lot and two pull requests' runs now fill.

  `python3 tools/ci/run_report.py N --times` prints every job of #N's
  newest run with its long steps, its notices and its warnings, and how
  long the jobs waited for a machine. **When a run is longer than the
  one before it, read that before you change anything.**

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
- **A watch that stopped without saying so.** On 25 November the landing
  session's turn ended at 07:22 with a lot in test and the status line
  promising a watch until 08:15. #433 had been told at 07:22 that it would
  land when the run passed; the run passed at 07:26 and nobody was there.
  `merge N` went on saying "a watch is on" until 07:52, thirty minutes
  after the last look, and #433 was merged by its own agent at 08:26.
  Three more pull requests were finished by 09:05 and stood until 12:40.
  **The landing session ends its watch itself** (`check_pr.py watch
  --off`, then its own merge) **while it still has the turn to do it
  in**: a watch of two hours is long, and the hour it names on the board
  is a promise that somebody is looking.
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
- **What is committed and not pushed is gone with it.** On 25 November
  the agent of #419 measured six places where an array's `length` was
  written through its "not writable", wrote the fix, saw it pass, and
  committed it. `git push` could not reach GitHub from that sandbox
  (`curl https://github.com` failed its TLS handshake, while two other
  hosts answered in the same shell), the turn ended, and the sandbox
  with it. The next turn found a branch with one commit, the claim, and
  did the work a second time as #428. A commit is not kept until it is
  pushed; "Before starting", point 5, says what to do when it cannot be.
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
| **Pixels are measured since Phase 120** (`docs/phases/120-pixels-nobody-measured.md`, finding 121): `tools/conformance/render_check.py` renders each page in `tests/render/reftests.json` and decodes the PNG with **its own reader** (all five scanline filters, per-chunk CRC) so the engine is not asked to grade its own writer; `make render-conformance`, and a CI job. 10 pages, 30 assertions, exact equality because box edges are not anti-aliased. The harness self-tests first and was shown to fail: a one-column bug in `BitmapRenderer::fill_rect` reddened 9 of 10. Extended by Phase 122 (`docs/phases/122-the-oracle-reaches-layout.md`, finding 127) to **36 pages, 133 assertions** (Phase 134 added six and fixed three; Phase 135 added two and fixed two): flex main axis (`justify-content` `space-between`/`center`/`flex-end`, `flex-direction: column`, `flex-wrap`), grid tracks and `gap`, floats and `clear`, absolute positioning, `overflow` clipping, a data-URI image, `border-radius` (anti-aliased, so the corners are asserted `not-colour`) and `transform` (the ink moves, the layout does not). Each page may carry a `known_failure` string; the run prints `KNOWN` with the reason and does not count it as a pass, and an entry on a page that **passes** prints `FIXED` and exits 1, so the corpus shrinks rather than accumulating excuses. Phase 193 adds three inline-image reftests, with 47/48 passing and one known failure: `white-space: nowrap` still allows inline replaced elements to wrap. Chromium 153 agrees with all 11 new assertions. Phase 211 (`docs/phases/211-table-and-inline-pixel-oracle.md`, finding 215) adds 15 CSS 2.1 reftests (65 assertions) for table-cell backgrounds/layers/spacing/spans (6 pass, 3 `known_failure`: `tbody` and `<col>` backgrounds unrendered, shorter sibling cell does not stretch to row height) and inline formatting (`text-align`, `line-height`, `inline-block`, `vertical-align`, `white-space: pre`, inline `<span>` horizontal margin/padding: 2 pass, 4 `known_failure`), bringing `tests/render/reftests.json` to **55/63 passing, 8 known** (104 pages / 386 assertions across both corpora). Phase 238 (`docs/phases/238-css-tables-and-inline-formatting.md`, finding 242) fixes `tbody`/`thead`/`tfoot` row-group box sizing, `rowspan >= 1` table cell height stretching, `white-space: nowrap` & `pre`/`pre-wrap`/`pre-line` parsing/wrapping, `vertical-align: top`/`bottom`/`text-top`/`text-bottom`, `display: inline-block` horizontal IFC flow & atomic painting, and inline horizontal `margin`/`padding`/`border` cursor advance, clearing 6 `known_failure` reftests (`inline_nowrap_keeps_replaced_elements_on_one_line`, `table_layers_cell_over_row_over_tbody_over_table`, `table_cell_background_fills_row_height_of_taller_sibling`, `inline_block_boxes_flow_horizontally_and_wrap`, `inline_vertical_align_top_and_bottom_on_replaced_elements`, `inline_span_horizontal_margin_and_padding_offset_child`) and bringing `tests/render/reftests.json` to **62/66 passing, 4 known** and `tests/render/body-origin.json` to **36/41 passing, 5 known** (clearing `inline_body_honors_its_default_horizontal_margin`, 107 pages / 395 assertions across both corpora). **Phase 264** (`docs/phases/264-css-table-columns-pre-visibility-opacity-and-gradients.md`, finding 268) adds `<colgroup>`/`<col>` display/layout/layering, `white-space: pre`/`pre-wrap`/`pre-line` newline preservation in `LayoutTree.cpp`, `visibility` paint enforcement, multi-layer inline ancestor backgrounds, isolated opacity compositing groups, and `background-image: linear-gradient(...)` with root propagation blocking — clearing all 6 remaining CSS/layout/paint `known_failure` entries across both corpora (`table_colgroup_col_background_shows_through_transparent_cells`, `inline_white_space_pre_breaks_on_source_newlines`, `hidden_body_children_do_not_paint_but_canvas_does`, `inline_body_under_a_translucent_inline_child`, `root_opacity_applies_to_the_document_paint`, `root_background_image_blocks_body_propagation`) and bringing `tests/render/reftests.json` to **64/66 passing, 2 known** (only AVIF picture decode and WebGL) and `tests/render/body-origin.json` to **40/41 passing, 1 known** (only empty-block/sibling margin collapse). **Phase 275** (`docs/phases/275-webgl-software-pipeline.md`, finding 279) lands a software WebGL 1.0/2.0 rendering pipeline and GLSL ES 1.00/3.00 compiler backed by the shared canvas pixel buffer, clearing `webgl_clear_paints_canvas_green` and bringing `tests/render/reftests.json` to **65/66 passing, 1 known** (only AVIF picture decode). **Phase 376** (`docs/phases/376-general-vertical-margin-collapse.md`, finding 376) implements general CSS 2.1 §8.3.1 vertical margin collapse in `LayoutBox::layout_block()`, clearing `an_empty_first_block_collapses_with_the_following_margin` and bringing `tests/render/body-origin.json` to **41/41 passing, 0 known**. Paint order left the list in Phase 135, which is the ledger working: the float and `transform` pages had carried reasons since Phase 122 and passed the moment the display list started emitting in Appendix E order. Still open: text-glyph line breaking / bidi inline runs, collapsed table borders, SVG and canvas visual comparisons are incompletely measured; **no WPT reftests are vendored** (`find tests/wpt -name '*-ref*'` is 0); glyph *shape* is not compared, only ink position; Phase 207 (finding 211) adds `make diff-render` to compare full RGBA screenshots between two builds for either corpus or a single page, without making a conformance claim | `tools/conformance/render_check.py`, `tests/render/`, `Makefile`, `.github/workflows/ci.yml` | `docs/phases/120-pixels-nobody-measured.md`, `docs/findings/121-a-pixel-was-never-compared.md` | Adding a reftest is one entry in `tests/render/reftests.json` and needs no engine change, so this row is a good one to extend rather than a claim to take |
| | | | |
| **Paint order is Appendix E since Phase 135** (`docs/phases/135-paint-order-is-appendix-e.md`, finding 140): the row above was added by Phase 122 because `DisplayList::build_recursive` painted in document order, so a float came out under the background of the block after it and a transformed element under a later sibling's ink. `src/render/PaintCommand.cpp` now walks the seven steps of CSS 2.1 Appendix E — own chrome and ink, negative `z-index` stacking contexts, in-flow block backgrounds, floats, inline content, then the box's own stacking contexts ascending by `z-index` — with one function per step (`paint_atom`, `paint_block_backgrounds`, `paint_floats`, `paint_inline_contents`, `paint_stacking_contexts`). **Still open:** a box that clips or establishes a stacking context is painted as one atom, because a flat display list cannot interleave two phases across a clip push/pop, so phases are honoured inside such a box and not across it (finding 140 §4); `inline-block`/`inline-table` internal phases are not separated from their parent's; negative `z-index` is hoisted to the nearest atom rather than the nearest stacking context; `overflow: auto` still scrolls the viewport. The engine's own suite found the one regression (`canvas2d_and_html_paint_arabic_cursive_and_contextual_glyphs` counted two identical `DrawText` commands), and the pixel oracle printed `FIXED` against the float page the moment it started passing | `src/render/PaintCommand.cpp`, `tests/render/reftests.json` | `docs/phases/135-paint-order-is-appendix-e.md`, `docs/findings/140-the-display-list-had-no-phases.md` | Done. The four remaining `known_failure` pages are the cross-axis row and the rounded clip |
| | | | |
| ~~**`align-self` and `align-content` are not read, and `justify-content` is ignored in a column container**~~ **Done in Phase 180 (`docs/phases/180-flex-and-grid-box-alignment.md`, finding 184):** added `AlignSelf` plus `align_self`, `justify_items`, `justify_self` in the 3 padding bytes of `ComputedStyle` (zero size growth), parsed them alongside `place-items`/`place-self`/`place-content` and `-webkit-*` aliases, fixed `ComputedStyle::hash()` to include grid and item alignment, implemented `align-self` + multi-line `align-content` + `row-reverse`/`wrap-reverse` in row flex, full main-axis `justify-content` + vertical `flex-grow` + wrapping + `align-content` + `align-self` + `column-reverse` in column flex, and `align-self`/`justify-items`/`justify-self`/`justify-content`/`align-content` in grid. Cleared the final two `known_failure` entries in `tests/render/reftests.json` (`flex_column_justify_content_flex_end`, `flex_align_self_overrides_align_items`) and added 7 new alignment reftests (**45/45 pages, 179 assertions, 0 known failures**). **Still open:** `baseline` alignment, `safe`/`unsafe` overflow fallback, vertical column `flex-shrink`, and `auto` margin absorption along flex axes | `include/litebird/css/StyleResolver.hpp`, `src/css/StyleResolver.cpp`, `src/layout/LayoutBox.cpp`, `tests/render/reftests.json`, `tests/test_suite.cpp` | `docs/phases/180-flex-and-grid-box-alignment.md`, `docs/findings/184-align-self-align-content-and-column-justify-content.md` | Closed; the original render reftest corpus now has 0 `known_failure` entries |
| | | | |
| **HTTP authentication is whole except for the parts that need a prompt or a proxy.** Phase 113 read the challenges (RFC 7617 `Basic`, RFC 7616 `Digest`) and Phase 115 (`docs/phases/115-the-rest-of-http-authentications-challenge.md`, finding 120) finished the challenge side: `SHA-512-256` (FIPS 180-4 §6.7, a different function from SHA-512 truncated), `auth-int` over the request body (§3.4.3), `userhash` (§3.4.4, the username field is `H(username ":" realm)` with the algorithm in use), `Authentication-Info`/`nextnonce` (§3.5, with `nc` counted per nonce), `407`/`Proxy-Authenticate` reported as `proxy_auth_schemes`/`proxy_auth_realm`, and a refused credential is **deleted** on a non-stale 401 after an authorized attempt. Phase 162 / finding 167 honors a same-realm Digest `stale=true` challenge: the replacement nonce is retried once without discarding the credential; a repeated stale response is bounded and preserves it. Two bugs in what Phase 113 had already shipped were fixed: `-sess` folded A1 with MD5 whatever the algorithm was (`SHA-256-sess` answered `1dfbe580…` where it should be `1eba72f3…`), and the loader ranked `SHA-512-256` as if it were `SHA-256` by prefix match. Phase 184 ([`docs/phases/184-a-digest-credential-is-digested-in-nfc.md`](docs/phases/184-a-digest-credential-is-digested-in-nfc.md), finding 188) normalizes the username and password to NFC before they are digested, which RFC 7616 §3.4.1 requires and this client had not done: one credential typed precomposed and typed with a combining accent computed two different responses (`2248d1d2…` and `f2c94816…`), and now computes one. `auth_nfc` runs inside `auth_digest_build`, so A1, `userhash` and the `username` field all see the same bytes, and RFC 7616 §3.9.1's published example still answers `8ca523f5…`. ~~no proxy support (`Proxy-Authorization` is never sent, so a 407 is reported and the load fails)~~ **Fixed in Phase 434** ([`docs/phases/434-proxy-authorization.md`](docs/phases/434-proxy-authorization.md), [finding 434](docs/findings/434-the-proxy-was-only-ever-asked.md)): the challenge was parsed and *reported* (`proxy_auth_schemes`/`proxy_auth_realm`) and that was all — the comment above `auth_note_proxy_challenge` said "this client does not send to proxies". There is now a **second store** (`g_proxy_auth_store`, `set_proxy_credentials`/`has_proxy_credentials`/`clear_proxy_credentials`/`proxy_credential_count`), a second authorizer `auth_proxy_authorization_for` that ranks challenges as the origin's does but reads only that store and keys nonce bookkeeping by the proxy authority, and one bounded retry in `load_http` that sends `Proxy-Authorization` and forgets the pair on a non-stale `407` (a stale Digest nonce keeps it). **The separation is the point, not a detail**: RFC 9110 §11.7 keeps the two protection spaces apart, and the existing suite already asserted a document credential must not be sent to a proxy (`tests/test_suite.cpp`, the `407` block) — reusing `set_auth_credentials` would have turned a missing feature into a credential leak, so `tests/suites/434-proxy-authorization.cpp` asserts both directions in one test. 479/479 suites; js-conformance 1153/1158, dom- 678/678, xml- 1718/1718 unchanged. **Still open:** no prompt (credentials come from `set_auth_credentials`/`set_proxy_credentials`), no general `http://` proxying so a `407` can only come from something already in the request's path (only `wss://` has an HTTP CONNECT tunnel, Phase 242), a `Digest` proxy challenge is answered through the same path but not separately measured, and the store holds UTF-8 octets with any `charset` other than `UTF-8` refused; `Basic` is not normalized, because RFC 7617 asks for none | `src/net/ResourceLoader.cpp` (`g_proxy_auth_store`, `auth_proxy_authorization_for`, the `407` retry in `load_http`), `include/litebird/net/ResourceLoader.hpp`, `tests/suites/434-proxy-authorization.cpp`, `tests/test_suite.cpp` | `docs/phases/113-http-authentication-challenges.md`, `docs/phases/115-the-rest-of-http-authentications-challenge.md`, `docs/phases/162-digest-stale-retries.md`, `docs/findings/117-a-401-was-just-a-failed-load.md`, `docs/findings/120-a-published-vector-can-be-wrong.md`, `docs/findings/167-stale-digest-nonce-is-not-a-refusal.md` | The digests are held to published vectors and to an independent implementation, and the wire is a loopback server this repository starts. **RFC 7616 §3.9.2 as printed is wrong** — erratum 4897; the suite asserts the corrected pair and says why |
| | | | |
| **General ES-module semantics and transferable messaging remain open.** Phase 66 gives module workers a MIME-checked static graph subset (default/named/namespace imports; simple declaration/default/named exports; named re-exports) and gives Worker/Window/same-agent `MessagePort` neutral graph packets for primitives, ordinary objects and arrays. It deliberately has no live bindings, cycles, `export *`, dynamic `import()`, `import.meta`, top-level `await`, module `<script>` support, typed arrays/`ArrayBuffer`, transferable ownership handoff, transferred/cross-agent ports, or `messageerror`; functions, symbols, DOM/host/exotic values and non-empty transfer lists reject with `DataCloneError` | `src/js/Worker.cpp`, `src/js/JsOps.cpp`, `src/js/DomWindowBindings.cpp`, `src/js/DomBindings.cpp` | Phase 66 / finding 72 (`docs/phases/66-es-module-workers-and-structured-clone.md`, `docs/findings/72-es-module-workers-and-structured-clone.md`) | Phase 66 also makes the process-wide symbol/interface registries process-lifetime allocations, retaining Phase 40’s exit-order safety while removing their static-destructor race | **`export * from './mod.js'` done in Phase 105 / finding 111** (`docs/phases/105-export-star-from-a-module.md`): the star is recorded in `ModuleTransform::star_exports`, the source module's namespace is republished beneath the module's own names, `default` is not republished and a nearer declaration wins. A namespace property is now an enumerable getter that resolves when it is read, not a copy made when the wrapper finished. Phase 416 ([`docs/phases/416-a-modules-exports-were-copies-of-its-bindings.md`](docs/phases/416-a-modules-exports-were-copies-of-its-bindings.md), finding 416) made the namespace getters live over the *binding* instead of over a snapshot: the wrapper published each export as `Object.defineProperty(exports, name, {get: function(){ return local; }})`, a getter closed over the module's own scope, and now returns its exports instead of leaving them on a global (the global kept every module the worker evaluated alive — 39,201 cells alive against 1,626). `import * as ns` and `export *` therefore answer 1, 2. **Still open:** a **named** import is still a copy, because `const n = imports[slot]` reads the dependency's namespace once and holds a `const` (`import { n, bump }` still answers 1, 1); making it live means the wrapper body can no longer treat an imported name as a value anywhere it appears. Still open as before: **two evaluations of one module are two instances** (Phase 66's missing module-record machinery, asserted in `module_worker_static_graph_loading_and_refusal`), cycles, dynamic `import()`, `import.meta`, top-level `await`, and `export * as ns from`, which has its own message and is still refused |
| | | | |
| **The exception a `DOMException` is made in is the receiver's realm**: `domparsing/innerhtml-05.xhtml` stays listed as `test-checks-parent-global` because it asks for the caller's realm where `dom/nodes/Document-createElement.html` (240 passing subtests) asks for the frame's. The engine keeps the receiver's, as browsers do; no implementation passes both. `make_dom_exception` asks the node's realm first, then the method's | `src/js/DomBindings.cpp` (`make_dom_exception`), `include/litebird/js/DomBindings.hpp` | [`docs/phases/62-the-realm-of-a-domexception.md`](docs/phases/62-the-realm-of-a-domexception.md); [`docs/findings/68-the-realm-of-a-domexception.md`](docs/findings/68-the-realm-of-a-domexception.md); `tests/wpt/README.md` | Settled by measurement: the candidate change scored 7,759/8,009 with 240 unlisted. Do not re-open without a Chrome, Firefox and Safari run |
| | | | |
| **`TextDecoder` and `TextEncoder` reach script, and the decoder reads in pieces since Phase 163.** Phase 127 (`docs/phases/127-textdecoder-and-textencoder-reach-script.md`, finding 132) installed the two globals over the tables Phase 47 and Phase 110 had already built: all 40 decoders by their 228 labels, `decode()` taking an `ArrayBuffer` or any view, `fatal`/`ignoreBOM`, BOM stripping, `encode()` answering a real `Uint8Array`, and `encodeInto()` writing into one. Phase 163 ([`docs/phases/163-textdecoder-in-pieces.md`](docs/phases/163-textdecoder-in-pieces.md), finding 168) added `{stream: true}`: the bytes a chunk ends inside of are held back and put in front of the next chunk, the BOM is sniffed once per stream (a second one is data, a split one is waited for), a fatal stream that ends inside a character throws on the flush, and ISO-2022-JP's mode — chosen by an escape sequence — crosses the chunk through `decode_stream_chunk`. Phase 194 ([`docs/phases/194-the-byte-order-mark-belongs-to-a-utf-decoder.md`](docs/phases/194-the-byte-order-mark-belongs-to-a-utf-decoder.md), finding 198) took the BOM back out of the byte layer, where it had been a prefix check run for every encoding: §7.1 skips the first *scalar value* when it is U+FEFF, and only a UTF-8 or UTF-16 decoder can produce one, so `FE FF` is U+FFFE to a UTF-16LE decoder and `windows-1252`'s `EF BB BF` is `ï»¿`. `strip_byte_order_mark` now runs on the decoded string and `do_not_flush`/`restart_decoder` are §7.2's flag and first step, so a one-shot call sniffs its BOM again, a fatal error resets the decoder, and ISO-2022-JP's escape mode no longer outlives the call it was set in. What is held back is decided by scanning the chunk *forward*, one character at a time: a backwards walk over trailing bytes is wrong for EUC-KR and Big5, where a trailing byte can be a lead. Phase 209 (`docs/phases/209-the-cut-that-was-two-cuts.md`, finding 213) found the streaming oracle itself failing: `pending_tail` decided how many bytes to hold back by scanning from ASCII on every call, discarding the escape-chosen mode that `decode_iso2022_jp` had correctly carried, so a chunk cut between a double-byte pair's lead and trail byte after an earlier cut had left the decoder in the double-byte mode assembled the pair from the wrong two bytes — 596 of 2450 partitions of 19 byte strings differed from the one-shot answer, now 0, and the suite walks every partition (it fails with `got 596, expected 0` on the unfixed decoder). **Still open:** Phase 206 ([`docs/phases/206-the-codecs-inside-a-worker-scope.md`](docs/phases/206-the-codecs-inside-a-worker-scope.md), finding 210) settled the last of it: both globals were already in a worker's scope, because the install runs from `JsInterpreter`'s setup and a worker builds its own interpreter, and `text_codecs_are_installed_in_a_worker_scope` now runs nineteen probes inside a classic worker and answers them with `postMessage` — guarding the install with `if (m_document)` makes every one of the nineteen differ, which no suite did before. **Phase 276 (finding 280) took the first of the open items away, because it was not true:** the row said `TextDecoder` had no `stream` option -- so did the header of `src/js/TextCodec.cpp` -- and it has had one since Phase 163, with the partial bytes held in `DecoderState::stream_bytes`: a three-byte character split across two chunks answers `""` then `"€"`, a truncated tail flushes to `U+FFFD`, and `fatal` holds mid-stream and throws only at the flush, all four identical to Node v22.22.3. `make verify-text-codec` (`tools/conformance/verify_text_codec.py`, new) now runs 27 cases of both codecs against Node and requires the same answers, so this cannot drift into prose again. It found one real defect on its first run: `new TextDecoder().decode.length` answered **1** where the Encoding Standard's table and Node both say **0** (the argument is optional), now fixed. **Still open:** `TextEncoder` gains nothing from a `stream` option (`encode()` answers UTF-8 only, where a lone surrogate is always `U+FFFD`, and Node has none either); the `<meta charset>` prescan is still one pass over the first 1,024 bytes; `TextDecoderStream`/`TextEncoderStream` exist in neither scope and need `ReadableStream`/`WritableStream`/`TransformStream`, none of which this engine has, so they are a Streams phase and not a codec one; and no suite runs the codecs in a module or a nested worker | `src/js/TextCodec.cpp`, `src/net/TextEncoding.cpp`, `include/litebird/net/TextEncoding.hpp` | `docs/phases/110-the-other-way-through-the-encoding-tables.md`, `docs/findings/116-a-decoder-is-not-an-encoder.md`, `docs/phases/127-textdecoder-and-textencoder-reach-script.md`, `docs/phases/163-textdecoder-in-pieces.md`, `docs/findings/168-a-decoder-could-not-be-fed.md`, `docs/phases/194-the-byte-order-mark-belongs-to-a-utf-decoder.md`, `docs/findings/198-the-bom-was-a-byte-prefix.md`, `docs/phases/206-the-codecs-inside-a-worker-scope.md`, `docs/findings/210-the-worker-case-was-unasserted.md`, ARCHITECTURE.md Phase 47, section 5; AUDIT.md, finding 53 | The encoder is done and the interface is done. The stream's oracle is the engine's own whole decode — every **partition** of a byte string must answer what one call answers, and one cut is not every partition: the defect Phase 209 fixed needed two cuts, in a particular order, and was invisible to all of the single ones — because Node's ICU tables differ from the standard for some Big5 and gb18030 pointers, so another engine is not a reference for *those*, only for the BOM, error-mode and cut-invariance rules. |
| | | | |
| **Big5's single bytes are errors, and the sweep that said otherwise was wrong since Phase 245** ([`docs/phases/245-the-sweep-read-three-decoders-as-one.md`](docs/phases/245-the-sweep-read-three-decoders-as-one.md), [finding 249](docs/findings/249-big5-0x80-is-an-error-and-the-sweep-was-wrong.md)): a byte sweep over `0x80..0xFF` using Node's `TextDecoder` as the reference reported that `decode_big5` wrongly maps `0x80` and `0xFF` to U+FFFD, and a `0x80 -> U+0080` branch was written to match. It was checked against `whatwg/encoding`'s `encoding.bs` and is false: Big5's decoder has **three** steps where the sweep assumed four — ASCII is itself, `0x81..0xFE` is a lead byte, and **everything else returns error**. 0x80 is neither, so U+FFFD is the standard's answer and the engine was right all along. The clause the sweep expected lives in the **neighbouring** decoders: Shift_JIS step 4 says "ASCII byte **or 0x80**", gb18030 step 5 says "If byte is 0x80, then return U+20AC (€)" — **both already correct in this engine**, which is exactly why a sweep across the three manufactures a defect in the one that is right. Two more witnesses agree: the vendored corpus is silent (`big5_errors.html` has eight byte sequences and **no 0x80 at all**, so `make wpt` cannot move and its not moving proves nothing), and the Big5 **encoder** refuses U+0080 (encoder step 2 gives a byte only for an ASCII code point, and U+0080 has no index entry) — a decoder answering it would emit a character this engine cannot write. **No engine change:** `src/net/TextEncoding.cpp` is untouched and the drafted branch was reverted. **The sweep exists since Phase 283** (`docs/phases/283-the-encoding-sweep-the-row-asked-for.md`, [finding 288](docs/findings/288-icu-is-wrong-about-three-tables.md)): `tools/conformance/verify_encoding_tables.py` compiles a harness against the two objects that hold the tables and drives every one of them back through `decode_with_encoding` — 27 single-byte tables **both directions**, all 23,940 `kIndexEucKr` pointers, all 19,782 `kIndexBig5` (including the four two-code-point entries), all 11,280 `kIndexJis0208` via Shift_JIS (including 1,880 private-use), and three samples of each of the 207 `kIndexGb18030Ranges` with step 6's gap clause. **46,315 positions, 0 mismatches**, so the answer to "whether some other decoder disagrees with its own index" is no for every decoder this engine has. A **Node cross-check** compares the same tables against ICU and counts three encodings separately because ICU is the one that is wrong: `koi8-u` pointers 46/62 (table U+045E/U+040E, Node U+255D/U+256C), `windows-1253` byte 0xAA (no pointer, Node accepts it), and **`windows-1255` byte 0xCA** (table U+05BA in the unbroken run U+05B9/U+05BA/U+05BB, Node U+FFFD) — the third found by this tool. Two things that were learned by trying to break it and are now in the tool's docstring: a **0 entry in an index means "no pointer"**, not U+0000 (reading it as a code point produced 150 false mismatches, the same error class as the sweep this row is about), and **an internally consistent corruption is invisible to a check that only reads this engine** — injecting `0x1234` into `kSingleByte` was reported as 0 mismatches by every decoder check *and* by an encoder/table consistency probe, and only the Node cross-check saw it. **Still open:** the table row that asked for Big5 0x80 to be U+0080 describes correct behaviour and should be struck. **[Phase 375](docs/phases/375-encoding-tables-held-to-the-standard.md) ([finding 375](docs/findings/375-the-tables-were-only-checked-against-themselves.md)) closed the other half:** `tools/conformance/verify_encoding_index.py` (`make verify-encoding-index`, in CI) fetches `whatwg/encoding`'s own index files at a pinned digest and compares all 91,441 positions — the 27 single-byte tables, every multi-byte index including the 1,192 Big5 pointers the file leaves out, and the 207 gb18030 ranges — so a table generated from a bad revision is now caught against the standard rather than against the decoder that reads it. **0 mismatches**, and red on three injected defects, one per kind of table. What it does not do: it reads the index files, not the standard's test suite, and it says nothing about the shift_jis encoder, ISO-2022-JP's state machine or the `<meta charset>` prescan. | `tests/suites/245-big5-single-bytes.cpp`; Phase 283 adds `tools/conformance/verify_encoding_tables.py` (no `src/` change) | [`docs/phases/245-the-sweep-read-three-decoders-as-one.md`](docs/phases/245-the-sweep-read-three-decoders-as-one.md), [finding 249](docs/findings/249-big5-0x80-is-an-error-and-the-sweep-was-wrong.md), [Phase 283](docs/phases/283-the-encoding-sweep-the-row-asked-for.md), [finding 288](docs/findings/288-icu-is-wrong-about-three-tables.md), [finding 249](docs/findings/249-big5-0x80-is-an-error-and-the-sweep-was-wrong.md); WHATWG Encoding §11.1.1 (Big5), §12.3.1 (Shift_JIS), §10.2 (gb18030) | The suite fails in **three** wrong directions, which is what a guard for this mistake needs: the task's own fix (`0x80 -> U+0080`), ICU's answer for 0xFF (`0xFF -> U+F8F8`), and a "unified" dispatcher passing 0x80 through — the first two fail the same assertion, so a check that only covers one is satisfied by the other being absent. **When a corpus has no case for a byte, a green run is silence, not agreement** |
| | | | |
| ~~**`Promise` still does not make the subclass it was called on.**~~ **Done in Phase 196 ([`docs/phases/196-a-promise-subclass-makes-its-own-promises.md`](docs/phases/196-a-promise-subclass-makes-its-own-promises.md), finding 200 [`docs/findings/200-a-subclass-promise-had-no-state.md`](docs/findings/200-a-subclass-promise-had-no-state.md)):** `NewPromiseCapability` (§27.2.1.5) with `GetCapabilitiesExecutor`, `SpeciesConstructor` asked by `then`, `PromiseResolve` (§27.2.4.7.1) answering a promise of the same constructor unchanged, and the promise's state reachable from the **cell** as well as from the value (`JsObject::internal` on `ObjectClass::Promise`), because `super(...)` hands the native parent the object `new` made and `take_over` copies what a cell holds. `new P(function(){}).then` works, `P.resolve`/`P.reject`/`P.all`/`P.allSettled`/`P.any`/`P.race`/`P.withResolvers` and `p.then`/`catch`/`finally` all answer `P`, and `Promise[Symbol.species]` has a reader. `built-ins/Promise` in test262 went **470 → 546 of 732**. Three engine bugs surfaced behind it and are fixed: `current_this()` is a reference into the call context, so a native that hands it to a call had its receiver overwritten (the class constructor's `%fn` became an object and `super(...)` threw *'super' keyword unexpected here*); `settle_through` settled a rejection through `[[Resolve]]`, so a rejection passing through a `then` with no handler arrived at the next `then` as a value; and a capability's derived promise shared the source's `PromiseData`. `tools/conformance/verify_promise_subclass.py` (CI-wired) runs 22 programs through the engine and Node.js and requires the two to agree. **Still open:** `Promise.try` (15 cases) and `allKeyed`/`allSettledKeyed` (89) are absent features; `await` and async functions do not consult a subclass's species; and the collector's `use_count() > 3` baseline for a promise cell's reactions is a conservative heuristic, not a proof (finding 200 §4) | `src/js/Promise.cpp`, `include/litebird/js/Promise.hpp`, `src/js/BuiltinsObject.cpp` | [`docs/phases/140-a-subclass-is-made-by-the-builtin-it-was-called-on.md`](docs/phases/140-a-subclass-is-made-by-the-builtin-it-was-called-on.md), [`docs/findings/145-species-was-never-asked.md`](docs/findings/145-species-was-never-asked.md), [`docs/findings/200-a-subclass-promise-had-no-state.md`](docs/findings/200-a-subclass-promise-had-no-state.md) | Closed for the subclass question; the reaction leak in the row below is still the GC row's |
| | | | |
| ~~**A write to an index of an array does not ask the prototype for a setter, and `sort` does not read and write in the standard's order.**~~ **The setter half is done in Phase 265** ([`docs/phases/265-a-write-to-an-array-index-asks-the-chain.md`](docs/phases/265-a-write-to-an-array-index-asks-the-chain.md), [finding 269](docs/findings/269-an-index-had-two-homes.md)): a place the array does not own — a hole, or past the end — asks its prototype chain (§10.1.9.2), so an accessor setter runs with the array as `this` and the place stays a hole, an accessor with no setter or a non-writable data property refuses the write (a TypeError in strict code), and a writable data property is shadowed by the element write. Phase 249's four `push`/`unshift` `set-length-array-*` cases, which were this row's open half, pass. Also fixed: `Object.defineProperty` on an index past the end kept its value in the element and its attributes in the map, so `hasOwnProperty` said `false` while the read found `'p'`. **Still open:** `sort` does not do its `Get`, `Set` and `DeleteProperty` in the standard's order when `Array.prototype` has accessors (`sort/precise-comparefn-throws.js`, `sort/precise-prototype-element.js`; `precise-prototype-accessors.js` passes now) | `src/js/JsValue.cpp` (`set_through_prototype`, `set_property`), `src/js/BuiltinsArray.cpp`, `src/js/BuiltinsObject.cpp` (`define_from_descriptor`) | [`docs/phases/116-the-methods-that-move-places.md`](docs/phases/116-the-methods-that-move-places.md), section 4; [`docs/findings/125-reverse-ran-over-the-accessor.md`](docs/findings/125-reverse-ran-over-the-accessor.md); [`docs/phases/265-a-write-to-an-array-index-asks-the-chain.md`](docs/phases/265-a-write-to-an-array-index-asks-the-chain.md), [`docs/findings/269-an-index-had-two-homes.md`](docs/findings/269-an-index-had-two-homes.md) | The gate is the count of index-named keys that the hole-read question already keeps, so an ordinary program pays one relaxed load per element write: `BENCH_JS_BASELINE` shows the `array` case at 0.99× and the total 1.01× faster. `built-ins/Array` 2,767 → 2,774 with seven fixed and none broken; `built-ins/Object` 3,103 → 3,159; `language` unchanged. The bug #193 shipped was reporting a writable data property as handled, which refused the write it should have shadowed |
| | | | |
| **Array destructuring reads the places since Phase 137** ([`docs/phases/137-a-pattern-reads-the-place-not-the-vector.md`](docs/phases/137-a-pattern-reads-the-place-not-the-vector.md), finding 142): `var [x] = a` took the elements of an array whose iterator is the installed one straight from `array_elements`, so a hole was `undefined` to it whatever the index read as — with an accessor on index 0 whose getter returns `"g"`, `var [x] = a` bound `undefined` where `for (x of a)` and `[...a]` gave `"g"` since Phase 108; with `Array.prototype[1] = "p"`, `var [x, y] = [1, , 3]` bound `undefined` to `y` where the language binds `"p"`. `IterationCursor::pull` now reads each place with `js_array_get`, which is what `a[i]` reads: an element costs what it did, and a hole asks `js_array_holes_read_undefined` before looking an index up by name (ten-place patterns 200 → 197 ms over 200,000 iterations on a dense array, 190 → 201 ms where every place is a hole). String and protocol sources were already right, and a replaced `Symbol.iterator` is still asked | `src/js/JsEval.cpp` (`IterationCursor::pull`, the `Mode::Array` case) | [`docs/phases/108-an-accessor-on-an-array-index.md`](docs/phases/108-an-accessor-on-an-array-index.md), section 4, which left it open; the suite is `array_destructuring_reads_the_place_not_the_vector` | Done: 13 checks, 9 of them failing on `main` at `e1414f6`; 308/308 unit suites, and no corpus moved — js-conformance 1143/1158, DOM 678/678, trees 1973/1973, URL 1188/1188, WPT 9567/9824 |
| | | | |
| **A global declaration is a property of the global object, and now has a descriptor, since Phase 199** ([`docs/phases/199-a-global-declaration-has-a-descriptor.md`](docs/phases/199-a-global-declaration-has-a-descriptor.md), finding 203): §9.1.1.4 makes the global declarations non-configurable data properties of the global object, but this engine keeps them in the global environment and lets the object reach them through `lookup_fallback`. `has_own_property` asks the hook; `descriptor_for` did not, so **39 names** were simultaneously an own property and not one — every constructor, the error family, `eval`/`parseInt`/`parseFloat`/`isNaN`/`isFinite`, `JSON`, `Math`, `Reflect`, `Promise`, the collections, `ArrayBuffer`, `DataView`, `Date`, `RegExp`, the typed arrays, `globalThis`, `undefined`, `NaN`, `Infinity` and the URI functions all answered `Object.getOwnPropertyDescriptor(this, name) === undefined` while `hasOwnProperty` said `true`. That is the exact pair `propertyHelper.js` tests first, which is why 35 test262 cases failed with *Cannot read properties of undefined (reading 'enumerable')* — a message naming neither the property nor the defect. The descriptor is now derived from the binding: a library global is `writable`, non-enumerable, configurable; a script's `var` or function declaration is writable, **enumerable**, non-configurable; `undefined`/`NaN`/`Infinity` are writable-false **and** configurable-false (§9.1.1.4's one non-configurable kind); and `let`/`const`/`class` stay *not* properties of the global object, which `is_lexical` decides. test262 14,969 → **14,980** of 15,686, 0 lost; js-conformance 1,145 → **1,146**; dom-conformance 678/678. `__proto__` **landed in Phase 371** (finding 371): it was in no property table at all — `get_property` synthesised the name and `set_property` mirrored it — so the descriptor, `hasOwnProperty`, `in` and `getOwnPropertyNames` all said it did not exist while `({}).__proto__` and `o.__proto__ = p` worked. It is now a real `{get, set}` accessor on `Object.prototype` (enumerable: false, configurable: true), and **both fast paths stand aside** under three conditions: no own `__proto__` entry on the receiver, `Object.prototype`'s accessor still the installed one, and the receiver's chain reaching `Object.prototype` at all — the third is what makes `o.__proto__ = null; o.__proto__` answer `undefined` and not `null`, as Node does. `is_hidden_key` also exempts this one name: `__proto__` matches its `__name__` pattern, so it was filtered out of `own_keys` even once installed. +13 test262 (15,143 → **15,156** of 15,686), +4 js-conformance (1,146 → **1,150**), dom-conformance 678/678, 0 lost; the 3 cases still failing in the directory need `Proxy`. ~~**Still open in this family:** the 190 remaining `defineProperty`/`defineProperties` cases are **undiagnosed**~~ **Partly diagnosed and fixed in Phase 388** ([`docs/phases/388-array-set-length.md`](docs/phases/388-array-set-length.md), [finding 388](docs/findings/388-the-descriptor-path-had-two-lengths.md)) — the family is **not** closed, but it is no longer undiagnosed. Two causes were in `define_own_property`'s `length` handling: (1) the descriptor path never ran §10.4.2.4 step 4, so `Object.defineProperty(arr, 'length', v)` was a different operation from `arr.length = v` — it did not delete the places above the new length and did not refuse when one was non-configurable; the sweep now runs, above the plain-attributes shortcut, because that shortcut returns early for exactly the `{ value: n }` shape every one of these tests uses; (2) making `length` non-writable wrote `Enumerable|Configurable` instead of `Writable`, and `configurable: true` on it was accepted where §10.4.2.3 step 3.a.i refuses it. test262 15,153 → **15,168 (+15, 0 broken)**, `defineProperties` 580 → 586 of 632 and `defineProperty` 1,074 → 1,080 of 1,131, `language` unmoved, `make test` 450/450, `js-conformance` and `wpt` unchanged. **What remains, by message, is now measured:** ~25 cases want a `TypeError` in shapes the sweep does not reach (a descriptor combining `value` with an attribute change, and §20.1.2.3.1's ordering for `defineProperties`), ~14 are *"0 descriptor should not be enumerable"* — the dense representation still has no attribute bits for a plain index, which is the original claim and is true for that path — 5 are the `"a === 10", actually 0` family, and 3 are `BigInt64Array` being undefined. Start the next phase from those messages, not from "190 undiagnosed" | `src/js/BuiltinsObject.cpp` (`descriptor_for`, `install_object_builtins`), `include/litebird/js/JsInterpreter.hpp` (`Binding::is_lexical`), `src/js/DomWindowBindings.cpp` (the Window exotic that claims the name), `src/js/JsValue.cpp` (`get_property`, `set_property`, `is_hidden_key`) | Phase 199 / finding 203, Phase 371 / finding 371; `built-ins/Object/getOwnPropertyDescriptor/*`, `built-ins/Object/prototype/__proto__/*`, `built-ins/*/prop-desc.js`, `tools/conformance/run_test262.py`; suites `global_declarations_have_descriptors`, `proto_is_a_property_of_object_prototype` | Two answers to "is this an own property" live in this engine — the property map and the `lookup_fallback` hook — so a third place that asks must be checked for both. **Put a new block before the exotic lookup, not after it**: the Window exotic answers `has()` true for every global name and then describes none of them, so anything appended to the end of `descriptor_for` is silently swallowed (the first version of this fix changed nothing for that reason). A `let` in the global scope must never get a descriptor, or it becomes a property the standard says it is not |
| | | | |
| ~~**A data property on an index of an array has no attributes of its own.**~~ **Fixed in Phase 249** ([`docs/phases/249-array-writes-that-must-refuse.md`](docs/phases/249-array-writes-that-must-refuse.md), [finding 253](docs/findings/253-an-attribute-defined-on-an-array-was-forgotten.md)). `Object.defineProperty(a, "0", { writable: false })` now keeps all three attributes: the element's value and flags are captured before the place is made a hole, the map entry holds them, `has_accessors` makes the map visible to reads and to `hasOwnProperty`, and `Object.getOwnPropertyDescriptor` answers `writable: false`. `length` is the same story — the count stays the value, the map holds the attributes, a `{ writable: false }` definition makes `a.length = 0` and `push` throw, and a `length` value that is negative, fractional or above 2^32−1 is the ArraySetLength `RangeError`. The same phase gave the seven result-building array methods `CreateDataPropertyOrThrow` (§7.3.6) instead of `[[Set]]`, so a species result with a non-writable-but-configurable place is replaced and one with a non-configurable place is a TypeError. **All four `set-length-array-*` cases in `push`/`unshift` pass since Phase 265** ([`docs/phases/265-a-write-to-an-array-index-asks-the-chain.md`](docs/phases/265-a-write-to-an-array-index-asks-the-chain.md), finding 269), which made the element write ask the prototype chain; Phase 265 also found that a definition on an index past the end kept its value in the element and its attributes in the map | `src/js/BuiltinsObject.cpp` (`define_from_descriptor`, `descriptor_for`), `src/js/JsValue.cpp` (the `length` read), `src/js/BuiltinsArray.cpp` (`create_data_property_or_throw`, `fill_species_result`, `copy_places_into`, `dense_path`, `push`, `unshift`) | [`docs/phases/249-array-writes-that-must-refuse.md`](docs/phases/249-array-writes-that-must-refuse.md), [`docs/findings/253-an-attribute-defined-on-an-array-was-forgotten.md`](docs/findings/253-an-attribute-defined-on-an-array-was-forgotten.md), Phase 108, Phase 123, Phase 116 §4 | Eight descriptor probes match Node field for field; `built-ins/Array` 2750 → 2767 with 17 fixed and 0 broken, all sixteen `target-array-with-non-writable-property`/`-non-extensible` cases among them; `make test` 393/393. A splice writes its *receiver* with Set and only its returned array with CreateDataProperty — a blanket rewrite broke two cases and had to be reverted |
| | | | |
| **The refused `delete` and its message are done since Phase 123** (`docs/phases/123-a-strict-delete-refuses.md`, finding 128): a property that is not configurable — and an array's `length`, which `[[Delete]]` used to answer `true` for — answers `false` in sloppy code and is a `TypeError` in strict code with V8's words; `delete name` is answered (a `var` `false`, a configurable global property removed, an unresolvable name `true`, `this` `true`) and is the language's early `SyntaxError` in strict code. **One consequence stays open:** an index of an array defined through a descriptor keeps no attributes, so deleting one does not throw (the row above) — the array half of this row was closed by Phase 249 and Phase 265. ~~`arguments`' prototype is `Array.prototype`, so its message says `#<Array>`~~ **Fixed in Phase 289** ([`docs/phases/289-arguments-is-an-object.md`](docs/phases/289-arguments-is-an-object.md), [finding 360](docs/findings/294-an-arguments-object-was-an-array.md)): an arguments object is an ordinary object with `%Object.prototype%` and its own `@@iterator` — the same function object as `%Array.prototype.values%` (§10.4.4.7) — so `[...arguments]` and `for…of` keep working. Moving the prototype alone broke 75 test262 cases; the standard had a second half. ~~**Still open there:** `callee` (§10.4.4.6) is `undefined` and not own, and mapped arguments (§10.4.4.2) are not modelled~~ **Both done in Phase 372** ([`docs/phases/372-arguments-is-an-object.md`](docs/phases/372-arguments-is-an-object.md), [finding 372](docs/findings/372-arguments-was-a-copy.md)): a sloppy function's `arguments` is a **mapped** arguments exotic object — one binding per simple parameter, resolved by name in the call's environment, so `arguments[0] = 42` reaches `a` and `a = 99` reaches `arguments[0]`, while a strict function's stay separate — and `callee` is an own non-enumerable `Writable\|Configurable` property holding the function, which strict code still does not have. Two things worth keeping: the `base[number]` fast path in `eval_arguments` answered the element **without** calling `get_property`, so an exotic was never consulted on a read (it now requires `!base.object_val->exotic`); and **Node does not follow §10.6 step 5.b.i** — `{value: 5}` writes the value into the parameter and *keeps* the mapping, where the spec's SameValue rule unmaps it. Only `{writable: false}` and an accessor descriptor unmap. Same-tree test262 15,147 → 15,149, 0 lost; `make test` 442 | `src/js/JsEval.cpp` (`case JsTokenType::Delete`) | [`docs/phases/108-an-accessor-on-an-array-index.md`](docs/phases/108-an-accessor-on-an-array-index.md), section 4, where it was met | Found while checking `pop` of an accessor that is not configurable, which does throw. Whether the code is strict is known where the operator is evaluated |
| | | | |
| ~~**The four `__defineGetter__` relatives do not exist**~~ **Fixed in Phase 136 (finding 141).** The `Reflect` half of this row landed in Phase 124; Annex B.3.3's `Object.prototype.__defineGetter__`, `__defineSetter__`, `__lookupGetter__` and `__lookupSetter__` are now installed. Definition uses `define_from_descriptor`, preserves the other half of an existing accessor, checks callability before converting the key, and returns `undefined`; lookup walks own descriptors up the prototype chain and stops at the first own property. Node.js v22.22.3 agrees; test262's four directories are 44/54, with all ten remaining cases requiring the still-absent `Proxy` | `src/js/BuiltinsObject.cpp`, `tests/test_suite.cpp` | ECMA-262 Annex B.3.3; test262 `built-ins/Object/prototype/__defineGetter__` and its three neighbours; [`docs/phases/136-legacy-accessor-reflection.md`](docs/phases/136-legacy-accessor-reflection.md), [`docs/findings/141-legacy-accessors-were-invisible.md`](docs/findings/141-legacy-accessors-were-invisible.md) | Closed; remaining test262 failures are the separate `Proxy` feature |
| | | | |
| **An array method no longer copies its receiver, since Phase 129** ([`docs/phases/129-an-array-method-must-not-copy-its-receiver.md`](docs/phases/129-an-array-method-must-not-copy-its-receiver.md), finding 134): `receiver()` used to read index 0 up to `length` before the method looked at an argument, so `{ 0: 9, length: Infinity }` was `std::bad_alloc` or SIGKILL and `{ length: 2**32 }` was an allocation where the standard wants a `RangeError`. test262 `built-ins/Array/prototype` went 1,391 → 1,523 of 1,632, 22 crashes → 0, 132 gained and none lost. **The length is no longer a count of storage (Phase 288).** The row asked for an audit of the methods that *move* elements; thirteen probes of them against Node found no disagreement in what they *answer*, and the audit's unasked-for case found the defect underneath them all: `a.length = 1e8` allocated 88 bytes per named-but-empty place, so it was OOM-killed, and `a.length = 4294967295` — a legal length — threw `std::bad_alloc` into script. A length may now be held in the array's own entry with the elements covering no more than it. Three defects went with it: the invented `2^26` cap on `new Array`, a `RangeError` invented at 2^24 for a stray index write, and `JSON`/spread/descriptor/`getOwnPropertyNames` all reading the element count as the length. `a.length = 1e7` is 507 ms → 0 ms, `make test` **419**, test262 **15135 → 15139 pass, 0 lost** ([`docs/phases/288-a-length-cannot-be-counted.md`](docs/phases/288-a-length-cannot-be-counted.md), finding 293). **Still open:** 109 tests in those directories fail; `find`/`findLast` do not walk a run of missing places above a 16-million bound (stated in the code) — and that deviation **cannot be fixed here**: I fixed it and measured 10.7 s for 20 million places, because an *empty* 20-million loop already costs 3.65 s against Node's 13 ms, so it waits on the bytecode VM rather than on an array patch; `pop`/`shift`/`splice` at a length near 2^32-1 are correct and slow for that same reason; and the methods that *move* elements — `reverse`, `sort`, `splice`, `fill`, `copyWithin`, `shift`, `unshift`, `with`, `toSpliced` — and the iterator objects were not audited for *cost* (only for answers) and some still loop to `length_of`. **`reduce` and `reduceRight` walk the places as they reach them since Phase 213** ([`docs/phases/213-reduce-asks-for-each-place.md`](docs/phases/213-reduce-asks-for-each-place.md), finding 217, [`docs/findings/217-reduce-listed-the-places-before-it-walked.md`](docs/findings/217-reduce-listed-the-places-before-it-walked.md)): both listed the indices that were present before the first callback ran, so a place a callback created ahead of itself was never visited — `[1, 2, , 4, '5']` with a callback that writes `arr[2]` answered `"75"` where Node.js v22.22.3 answers `"105"` — and `reduceRight`, which §23.1.3.22 walks downwards, could not see a place created *below* itself at all: `[9, , , 9]` filling 1 and 2 from index 3 answered 18 where Node answers 21. `reduce` now uses the same `walk_present_indices` walk as `forEach` and `map`, and `reduceRight` walks `k` down from `length - 1` asking `HasProperty` at each step. `built-ins/Array` **2,726 → 2,745 / 3,083 (+19 / -0)**, `make test` **350 / 350** | `src/js/BuiltinsArray.cpp` | [`docs/phases/129-an-array-method-must-not-copy-its-receiver.md`](docs/phases/129-an-array-method-must-not-copy-its-receiver.md), [`docs/findings/134-an-array-method-copied-what-it-could-not-hold.md`](docs/findings/134-an-array-method-copied-what-it-could-not-hold.md) | Adding a method is one `walk_present_indices` call; the crashes were all in the one `receiver()` helper |
| | | | |
| **`structuredClone` keeps the type of what it copies since Phase 252** ([`docs/phases/252-structured-clone-keeps-its-types.md`](docs/phases/252-structured-clone-keeps-its-types.md), finding 256): it was a walk over own enumerable properties, so a `Map` came back `{}` without `size` or `get`, and a `Set`, a `RegExp`, a typed array and an `ArrayBuffer` likewise; a transfer list was not read at all. It now switches on the type as HTML §2.7.6 does — `Map`/`Set` entries in order, `RegExp` pattern and canonical flags with `lastIndex` 0, bytes and geometry for `ArrayBuffer`/typed array/`DataView`, a transfer list that detaches the source — and the `seen` map keeps the graph's shape, so a view and its buffer clone to one buffer. Functions, host objects, Promises and the weak collections are a `DataCloneError`; a `Symbol` is a `TypeError`. **Still open:** a `Symbol` is a `DataCloneError` in Node (this engine cannot carry one at all), the message wording is the engine's, and the worker/`MessagePort`/window `postMessage` path keeps its own smaller packet clone — transfer lists there still say they are unsupported | `src/js/BuiltinsObject.cpp` (`structured_clone`, the transfer list), `src/js/BuiltinsCollections.cpp`, `src/js/BuiltinsRegExp.cpp`, `src/js/TypedArray.cpp`, `tests/suites/252-structured-clone-keeps-its-types.cpp` | `docs/phases/252-structured-clone-keeps-its-types.md`, `docs/findings/256-structured-clone-flattened-every-type.md` | Node v22.22.3 is the oracle for every case in the suite; the message path's packet clone is a separate job |
| | | | |
| **`AggregateError` exists since Phase 285** ([`docs/phases/285-an-error-that-collects.md`](docs/phases/285-an-error-that-collects.md), finding 290): it was the one error type of §20.5.7's eight that the library did not install, so `typeof AggregateError` was `undefined` and test262's `built-ins/AggregateError` was 0 of 25. `Promise.any` had been pretending: on total rejection it made a plain `Error`, hid `name: "AggregateError"` on it and hung `errors` off it, so the value was neither an `AggregateError` nor an instance of one, and its `errors` was a slot per input — `[undefined]` for an empty list, a hole for every input that never rejected. The constructor is now installed beside the other seven (its prototype made on first use, rooted, because the realm's own constructor belongs to another file), message defined only when one was given, and `Promise.any` counts what rejected so an `undefined` reason is kept apart from one that has not settled. **Still open:** the four tests left need `Proxy`, `$262`, or a deletable global (next row) | `src/js/BuiltinsObject.cpp` (`install_error_builtins`), `src/js/Promise.cpp` (`promise_any`), `include/litebird/js/JsOps.hpp` | `docs/phases/285-an-error-that-collects.md`, `docs/findings/290-aggregate-error-did-not-exist.md` | Node v22.22.3 is the oracle; test262 `built-ins/AggregateError` 0/25 → 21/25 |
| | | | |
| **A built-in global is an own property of the global object since Phase 404** ([`docs/phases/404-the-global-object-could-not-list-what-it-could-find.md`](docs/phases/404-the-global-object-could-not-list-what-it-could-find.md), finding 404). **Was:** `'Error' in globalThis` is `true` and `Object.prototype.hasOwnProperty.call(globalThis, 'Error')` is `true`, and `Object.getOwnPropertyDescriptor(globalThis, 'Error')` answers `{writable: true, enumerable: false, configurable: true}`, which is right — but `Object.getOwnPropertyNames(globalThis)` lists only `requestAnimationFrame`, `cancelAnimationFrame`, `queueMicrotask` and `crypto` (Node lists all 128), and `delete globalThis.Error` answers `true` and leaves `Error` in place. `JsIntrinsics::add_global` keeps the library's globals in a vector beside the property map, and the two are not kept in step, so every completeness or enumeration question about the global object — `test262`'s `prop-desc` checks among them — answers for the four that happen to be there. Measured on `cb0b4b2`, found by Phase 285 (finding 290), which is why one of its four remaining test262 cases fails. **Fixed:** `lookup_fallback` and `assign_hook` each take a name the caller already has, so nothing could ask *which* names the object owns — `Object.getOwnPropertyNames(globalThis)` reported **5** (only the names that were property-table entries) against Node's 129, and `delete globalThis.Error` answered `true` while erasing nothing. Two hooks (`JsObject::own_keys_hook`, `own_property_hook`) let `own_keys`, `[[Delete]]` and `has_own_property` ask, and `Binding::is_builtin` — set once by `mark_all_builtin()` when the environment holds only what the engine declared — keeps the lists apart: a built-in is in `getOwnPropertyNames` and **not** in `Object.keys`, a script's `var` is in both, a `let` in neither. 5 → **78** names; test262 15,177 → **15,179**, js-conformance 1,152/1,158 and dom-conformance 678/678 unchanged, 461 engine suites, 0 lost. **Still open:** the `%`-slot half of this row (a script reading `el['%classList']`), which is a different mechanism | `src/js/JsOps.cpp` (`JsIntrinsics::add_global`, `find_global`, the global object's enumeration), `src/js/BuiltinsObject.cpp` | `tests/suites/285-aggregate-error.cpp` (the probe), `docs/phases/285-an-error-that-collects.md` §4 | No write-up yet; the four globals that are own properties show what the shape should be |
| | | | |
| **A definition names the function it makes, and a spread of a method definition's `name` was `[object Object]`.** Phase 159 (`docs/phases/159-a-definition-names-the-function-it-makes.md`, finding 164) gave §15.4.4/§15.4.5 SetFunctionName its three forms: a symbol key contributes `[` + its description + `]` (and nothing when there is none), an accessor contributes `get `/`set `, and a grouped assignment target (`(f) = function () {}`) is *not* a named evaluation while the initialiser of a pattern/parameter is. **Still open in this family:** an anonymous callable in a *default export* — `export default function () {}` is named `default` by §16.2.3.6, and modules are not parsed here; ~~and `Symbol.prototype.description` on an undescribed symbol (`Symbol()` answers `""` here, the language `undefined` — one line in `BuiltinsObject.cpp`, and test262's `Symbol/prototype/description/*` is the oracle)~~ **Done in Phase 399** ([`docs/phases/399-symbol-description.md`](docs/phases/399-symbol-description.md), [finding 399](docs/findings/399-the-description-was-an-own-property.md)). It was **not** one line: the description was stored in the symbol's *property table*, which made it an own property (`hasOwnProperty(Symbol('x'), 'description')` was `true`, and `getOwnPropertyNames` listed it — test262's `is-not-own-property.js` asks exactly this), and a table of `std::string` had no way to spell "no description" distinct from `Symbol("")`. It rides in the value's `Extra` block now — `symbol_description` and a `symbol_has_no_description` flag, set only for `Symbol()` and `Symbol(undefined)` — with `symbol_description_text()` as the one place printing and property-key hints read it. My first attempt put the flag in the property table under its own key, which made `hasOwnProperty(Symbol(), '@@noDescription')` true: an observable key added to fix an observable key, caught by a probe written for it. test262 15,168 → **15,177 (+9, 0 broken)**, `built-ins/Symbol` 65 → 66 of 98 (seven description cases, including `is-not-own-property`), `language` unmoved, `make test` 454/454. Still open in `built-ins/Symbol`: `this-val-non-symbol.js` needs `Proxy`, which this engine has not | `src/js/JsEval.cpp` (`defined_name`, `name_function`), `src/js/JsParser.cpp` (`explicitly_grouped`), `src/js/BuiltinsObject.cpp` (the symbol description getter) | Phase 159 / finding 164; `language/expressions/object/fn-name-*`, `language/statements/class/definition/fn-name-*` | `defined_name` is the one place the rule lives now; a new definition site calls `name_function` rather than passing a hint to `make_function`, because the parser pre-names every method and a hint only reaches a function with no name. Check the two `evaluate_named` callers in `JsEval.cpp` (`eval_assign` and the default-value paths) before touching it: the assignment half is gated on `explicitly_grouped`, and the parameter/pattern half deliberately is not |
| | | | |
| ~~**`Object.assign` boxes, throws, and copies every own key since Phase 167**~~ **Family closed in Phase 198 (finding 202).** `Object.assign` ([`docs/phases/167-object-assign-boxes-throws-and-copies-every-own-key.md`](docs/phases/167-object-assign-boxes-throws-and-copies-every-own-key.md), finding 169) boxes its target with `ToObject`, throws on refused strict `Set`, and copies own enumerable symbol keys via `assign_keys` (`built-ins/Object/assign` 17/38 → 34/38, remaining 4 need `Proxy`). `Reflect.ownKeys` (Phase 175, finding 179) and `Object.getOwnPropertyDescriptors` (Phase 181, finding 185) report every own string and symbol key without the enumerable filter. **Done in Phase 198 (`docs/phases/198-object-spread-and-rest-copy-symbols.md`, finding 202):** object spread (`{...source}`) and object rest destructuring (`{...rest} = source`) in `src/js/JsEval.cpp` now share `copy_data_properties` (ECMA-262 §7.3.24), snapshotting own string and symbol keys in `[[OwnPropertyKeys]]` order, checking live own enumerability per key, excluding destructured string and symbol keys, and writing via `define_property` (`CreateDataPropertyOrThrow`) instead of `set_property` so target/prototype setters are not invoked. `tools/conformance/verify_symbol_copy_paths.py` 7/18 → **18/18** matching Node.js v22.22.3; test262 default suite 14,969 → **14,978** (+9, 0 lost) and all 17 spread/destructuring directories 14,076 → **14,094** (+18, 0 lost), leaving 0 non-`Proxy` `spread`/`obj-rest` failures | `src/js/BuiltinsObject.cpp` (`assign`, `assign_keys`), `src/js/JsEval.cpp` (`copy_data_properties`) | Phase 167 / finding 169; Phase 175 / finding 179; Phase 181 / finding 185; Phase 198 / finding 202; `tools/conformance/verify_symbol_copy_paths.py` | Closed; remaining test262 `CopyDataProperties` failures require `Proxy`. Do not make `own_keys` report symbols — `Object.keys`, `for...in` and `JSON` would inherit the change |
| | | | |
| **An abrupt completion still finishes the assignment, since Phase 176** ([`docs/phases/176-an-abrupt-completion-still-finishes-the-assignment.md`](docs/phases/176-an-abrupt-completion-still-finishes-the-assignment.md), finding 181): two defects that are one sentence of the standard each. (1) `base.prop = count += 1` with `base` null/undefined threw **before** the right-hand side ran, leaving `count` at 0 — the reference is resolved first, but `PutValue` is what refuses a nullish base, so `resolve_reference` now takes `throw_on_nullish` (false for a plain `=`) and `check_reference_base` makes the same call once the value exists; the eager refusal stays for compound assignment and `++`/`--`, where the base must be usable to read the current value. (2) A pattern that threw while binding left the iterator unclosed — `[ y = thrower() ] = iterable` called `return` zero times — where 7.4.11 requires IteratorClose on an existing abrupt completion, with the close's own throw discarded because the pattern's throw wins. A `next` that throws is **not** closed (7.4.8 step 7), which is why the first attempt at this lost seven cases. `language/expressions/assignment` 435 → **441 of 485**, the whole suite 14,889 → **14,895**, 0 lost; js-conformance 1145/1158 and dom-conformance 678/678 unchanged. **Done in Phase 271 ([`docs/phases/271-generator-return-closes-destructuring-iterator.md`](docs/phases/271-generator-return-closes-destructuring-iterator.md), finding 275):** a suspended generator's `GeneratorReturnException` now reaches array-pattern cleanup as a Return completion, not as a Throw: a successful `iterator.return()` preserves `.return(value)`, while a thrown close or a non-object close result replaces it; an existing pattern throw still wins and uses `close_quietly()`. Simple member assignment targets, including assignment-rest targets, are resolved before `IteratorStep` or rest draining, with nullish-base refusal deferred until the value/default is ready. The Phase 176 test's second-element target count was corrected to one `next`, since that target is evaluated before its own step. Test262 `language/expressions/assignment/dstr` **349 → 368 / 368** (+19, 0 lost) and the full `language/expressions/assignment` directory **447 → 466 / 485** (+19, 0 lost); `make test` passes **410 suites** and **114 cross-origin checks**. **Still open:** the broader assignment directory has 19 failures outside `dstr` (including Proxy-dependent cases, two blocked by unsupported BigInt literals, and other property-reference semantics); async-generator-specific coverage is not added | `src/js/JsEval.cpp` (`IterationCursor`, `bind_pattern`, `resolve_reference`, `check_reference_base`), `tests/test_suite.cpp`, `tests/suites/271-generator-return-closes-pattern.cpp` | Phase 177 / finding 181; Phase 271 / finding 275; `language/expressions/assignment/*`, `dstr/*`; `tools/conformance/run_test262.py language/expressions/assignment` | `resolve_reference` defaults to the eager nullish-base refusal; assignment-pattern callers that resolve a member before pulling must pass `false`, then call `check_reference_base` only after the value/default is ready. Do not move that check into `write_reference`. For IteratorClose, `close()` reports errors that replace a Return completion; `close_quietly()` is only for an existing Throw completion, which must win over errors from the close |
| | | | |
| **What a function is, in small**: ~~nothing tells a built-in that is a constructor from one that is not (`new Math.max()` makes an object)~~ **No longer true — fixed by Phase 158 (finding 163, `docs/phases/158-a-method-is-not-a-constructor.md`).** `js_define_method` now makes every built-in method (and `Math.max`'s own installer, which builds its values first) with `js_make_method`, so a method has no `[[Construct]]`: the 51 test262 `not-a-constructor` cases that ask `isConstructor(f)` passed, and `new Object.keys([])` is a `TypeError`. Both of the examples that named a *method* went with it: `class X extends Math.max {}` and `Reflect.construct(F, [], Date.now)` now throw the language's `TypeError`. Phase 284 adds non-constructible `Math.f16round` with direct binary16 rounding (263,955 Node/Python comparisons and test262 0/5 → 5/5; [phase](docs/phases/284-a-number-rounded-to-binary16.md), [finding](docs/findings/289-no-binary16-rounding-in-math.md)); `Float16Array` and DataView float16 methods remain separate. Phase 369 adds `Math.sumPrecise` with exact accumulation and one rounding (10,013 Python/Node numeric comparisons and 8 iterator comparisons; test262 0/10 → 9/10, the remaining case needs BigInt syntax; [phase](docs/phases/369-an-exact-sum-rounded-once.md), [finding](docs/findings/369-a-sum-that-rounded-every-step.md)). Phase 429 preserves the sign when a negative nonzero value rounds to zero in `toFixed`, while actual negative zero stays unsigned (115/144 → 144/144 Node cases; [phase](docs/phases/429-to-fixed-zero-sign.md), [finding](docs/findings/429-to-fixed-zero-sign.md)). Phase 407 makes `Number` exponential and precision formatting return non-finite receivers after argument conversion but before the range check (104/138 → 138/138 Node comparisons; [phase](docs/phases/407-number-format-special.md), [finding](docs/findings/407-number-format-special.md)). Phase 392 prevents `Math.cbrt` overflow on finite extremes (4,112/4,115 → 4,115/4,115 Decimal/Node probes; [phase](docs/phases/392-math-cbrt-extremes.md), [finding](docs/findings/392-math-cbrt-extremes.md)). Phase 383 sequences `Math.atan2` and `Math.pow` argument conversions left-to-right (22/22 Node probes, 8/22 before; [phase](docs/phases/383-math-converts-left-before-right.md), [finding](docs/findings/383-math-converted-the-second-argument-first.md)); other built-ins are not broadly audited. **Not open any more (Phase 208): a bound function reached only as a prototype keeps its target.** `Object.create(f.bind(target))` went on calling `f` with the wrong `this` after a collection: the collector counts a payload's captures and bound target, so a payload one handle holds is no root, and `Heap::mark_value` marked the payload's cell and closure but not the two vectors' contents, so the target was freed under a live function. `prototypes.dat`'s "reachable only as a prototype" case (recorded from Node.js) had answered `undefined` since it was written and now answers `bound kept`: js-conformance **1,146 → 1,147 / 1,158**, `prototypes.dat` 70/76 → 71/76, `make test` 355/355. Still open in this row: ~~`class X extends f.bind(null) {}` is accepted where the language refuses it — a bound function is a constructor but has no `prototype` to extend~~ **Fixed in Phase 403** ([`docs/phases/403-a-bound-function-has-no-prototype-to-extend.md`](docs/phases/403-a-bound-function-has-no-prototype-to-extend.md), [finding 403](docs/findings/403-the-extends-clause-ignored-a-missing-prototype.md)): the class evaluation read the parent's `prototype` and had two outcomes where §15.7.14 step 5 has three — the missing `else` is a `TypeError`, so a parent whose `prototype` is neither an object nor `null` (a bound function has no such property at all) is refused. The check sits after `IsConstructor`, as the spec's order has it, so a parent that is not a constructor keeps its own message. `language/statements/class/subclass/superclass-bound-function.js` passes; test262 +3 with 0 broken, `make test` 461/461, js-conformance 1,153/1,158; ~~`Object.getOwnPropertyNames(f)` lacks `length` and `name`, and `f.name = 'x'` takes effect~~ **also no longer true — Phase 146 (`docs/phases/146-a-functions-name-and-length-are-its-own.md`) made both own non-writable properties**; async and generator functions have `Function.prototype` itself as their prototype, where the language has an object for each kind between the two (three test262 tests, `AsyncFunctionPrototype-is-not-callable.js` and its relatives, passed while `Function.prototype` was not callable and fail since Phase 34); a property of a prototype that cannot be written does not stop an assignment to the object that inherits it | `src/js/JsOps.cpp`, `src/js/JsValue.cpp`, `src/js/BuiltinsObject.cpp` (`bind`), `include/litebird/js/Heap.hpp` (`JsObject::not_constructor` is the bit to set) | ARCHITECTURE.md, Phase 34, section 9; four failing cases of `prototypes.dat` | Phase 114 made `instanceof` ask `Symbol.hasInstance` and made a script function with no object `prototype` throw the language's `TypeError`, which is what `x instanceof` a bound function could not be asked before (a bound function delegates to its target now). Four host constructors still have no `prototype` (`Worker`, `WebSocket`, `IntersectionObserver`, `ResizeObserver`; `Headers`, `Response`, and `Request` gained per-realm `.prototype` objects in Phase 173, finding 177): a *native* function with no `prototype` is left answering `false`, so `1 instanceof Math.max` is `false` too. Give those four a `prototype` and the operator can throw for every function that has none |
| | | | |
| **A `JSON.parse` reviver is told the text it read, since Phase 405** ([`docs/phases/405-the-reviver-was-never-told-what-it-read.md`](docs/phases/405-the-reviver-was-never-told-what-it-read.md), [finding 405](docs/findings/405-the-text-was-there-and-the-reviver-could-not-see-it.md)): §25.5.1/§25.5.5 give a reviver a third argument, a context Record `{source}` holding the text the value was written as, and this engine called it with two. That is the half that makes Phase 393's `JSON.rawJSON` useful — a number too large for a `double` parses to `Infinity`, and only the original text can write it back out: `JSON.stringify(JSON.parse('{"a":1e1000}', function (k, v, c) { return typeof v === "number" ? JSON.rawJSON(c.source) : v; }))` is `{"a":1e1000}`, where before `c` was `undefined`. **The asymmetry is the trap:** §25.5.1 makes the Record only for a value that is not an Object, so a nested object and array get `{}` with no `source` while a nested primitive gets one — and the root gets the whole text trimmed, so `JSON.parse(' 1 ')` gives `"1"`. The source is a `std::map` keyed by the **holder and the key** the value was put under, because a primitive has no identity to key on; the parser records each span as it goes and `internalize` looks the pair up. Red on three engines — no third argument (fails `arguments.length` is 2), `source` on every value including containers (fails `'source' in c` for `[]`'s element), and an untrimmed root (fails `" 1 "`); two of the first three assertions written for it were themselves wrong and Node corrected them (`[1]`'s element 0 *does* have a source; the root is visited, so `[true,false]` yields three entries). **Still open:** the map is rebuilt per call with a `std::string` per primitive (unmeasured), a duplicate key's span wins by insertion order (unasserted), and `Array.fromAsync`/`Promise.try` are still absent. | `src/js/BuiltinsJson.cpp` | [Phase 393](docs/phases/393-a-number-the-double-could-not-hold.md), [finding 393](docs/findings/393-the-number-was-a-double-and-the-text-was-lost.md); ECMA-262 §25.5.1, §25.5.3.1, §25.5.5 | **A reviver's `source` is the value's own span, not the document's** — `JSON.parse('  1  ')` gives `"1"`. Key the map by `(holder, key)`, never by value identity: a primitive has none |
| | | | |
| **`Reflect.get` and `Reflect.set` use the receiver they are given, since Phase 414** ([`docs/phases/414-the-receiver-nobody-passed.md`](docs/phases/414-the-receiver-nobody-passed.md), [finding 414](docs/findings/414-the-receiver-was-an-argument-and-a-return-value.md)): §28.1.6/§28.1.13 are `[[Get]]`/`[[Set]]` **with a receiver**, and both methods dropped it — `Reflect.get({get x(){return this.y;}}, "x", {y: 9})` answered `undefined` where Node says 9. `Reflect.set` dropped the fourth argument *and* the answer: it returned `true` unconditionally, so `Reflect.set(Object.freeze({}), "x", 1)` was `true` where §28.1.13 step 2 and Node say `false`. `get` now goes through `js_get_from_chain`, the lookup `super.name` uses; `set` keeps `set_property` for the ordinary case and passes any other receiver through a thread-local `SetterReceiverScope` that the accessor call sites read through `setter_receiver(*)`, defaulting to the object — a second `set_property` would have restated 300 lines of array, accessor and prototype rules. **The same differential run also found `structuredClone(Symbol())` throwing `TypeError`:** HTML 2.7.4 step 6 is a `DataCloneError` DOMException in its own step before any object is looked at, `e.code` 25, for a nested symbol and a `Map` key too. **A comment beside the branch claimed that `TypeError` was the standard's answer and the phase-252 suite asserted it — two records of one wrong belief, each making the other look measured, neither citing the spec.** That is the lesson to carry: a passing test that documents its own deviation is a red flag, and it had stood for 162 phases. **Still open:** defining a *new* property on a receiver that lacks it (OrdinarySetWithOwnDescriptor step 5) is not done, `Proxy` is absent and is where this boolean earns its keep, and `Symbol.dispose`/`asyncDispose` are deliberately not added (Node's are registered symbols with the wrong description, and nothing parses `using`). | `src/js/BuiltinsObject.cpp` (`Reflect.get`, `Reflect.set`, `structured_clone`), `src/js/JsValue.cpp` (`setter_receiver`, `SetterReceiverScope`, the accessor call sites), `include/litebird/js/JsValue.hpp` | ECMA-262 §28.1.6, §28.1.13, §10.1.9.2; HTML 2.7.4 StructuredSerializeInternal step 6; `tests/suites/414-reflect-receiver.cpp`, `414-clone-symbol.cpp` | **The receiver is not the target** — a getter must see it as `this`, and a setter's write must land on it. Found by diffing ~50 expressions against Node at once, not by reading the source: three defects came out of one run. Red on three engines for `Reflect` (get ignoring, set ignoring, result always true), each failing a different assertion |
| | | | |
| **A `JSON.parse` reviver is told the text it read, since Phase 405** ([`docs/phases/405-the-reviver-was-never-told-what-it-read.md`](docs/phases/405-the-reviver-was-never-told-what-it-read.md), [finding 405](docs/findings/405-the-text-was-there-and-the-reviver-could-not-see-it.md)): §25.5.1/§25.5.5 give a reviver a third argument, a context Record `{source}` holding the text the value was written as, and this engine called it with two. That is the half that makes Phase 393's `JSON.rawJSON` useful — a number too large for a `double` parses to `Infinity`, and only the original text can write it back out: `JSON.stringify(JSON.parse('{"a":1e1000}', function (k, v, c) { return typeof v === "number" ? JSON.rawJSON(c.source) : v; }))` is `{"a":1e1000}`, where before `c` was `undefined`. **The asymmetry is the trap:** §25.5.1 makes the Record only for a value that is not an Object, so a nested object and array get `{}` with no `source` while a nested primitive gets one — and the root gets the whole text trimmed, so `JSON.parse(' 1 ')` gives `"1"`. The source is a `std::map` keyed by the **holder and the key** the value was put under, because a primitive has no identity to key on; the parser records each span as it goes and `internalize` looks the pair up. Red on three engines — no third argument (fails `arguments.length` is 2), `source` on every value including containers (fails `'source' in c` for `[]`'s element), and an untrimmed root (fails `" 1 "`); two of the first three assertions written for it were themselves wrong and Node corrected them (`[1]`'s element 0 *does* have a source; the root is visited, so `[true,false]` yields three entries). **Still open:** the map is rebuilt per call with a `std::string` per primitive (unmeasured), a duplicate key's span wins by insertion order (unasserted), and ~~`Array.fromAsync`/`Promise.try` are still absent~~ **`Array.fromAsync` is done in Phase 412** ([`docs/phases/412-array-from-async.md`](docs/phases/412-array-from-async.md), [finding 412](docs/findings/412-from-async-was-only-ever-a-promise-away.md)): §23.1.3.1 awaits every value and the mapper's result, prefers `@@asyncIterator` to `@@iterator`, falls back to the array-like path, and rejects rather than throwing — with the one throw step 2 has for a `this` that is not a constructor. A built-in has no frame to park, so the await is the blocking loop `eval_await` runs off the interpreter, written out in `BuiltinsArray.cpp`. Fourteen probes compared line for line with Node v22.22.3, identical; `make test` 465/465. **test262 does not move and that is expected: the pinned commit predates the method, so it has no `Array.fromAsync` cases.** `Promise.try` is `undefined` in Node v22.22.3 too, so it cannot be compared against the oracle and was left alone. **Still open, measured in Phase 415 (finding 415):** its async walk is a third copy of the iterator loop, and it has no `AsyncIteratorClose` — `next` is read **once per step** (3 reads for a 2-value iterable, against Node's 1) and the async iterator's `return` is **never called** (`closed=0`, against Node's 1, on a mapper throw and on a rejected `next`; both engines leave it alone on a walk that completes). | `src/js/BuiltinsJson.cpp` | [Phase 393](docs/phases/393-a-number-the-double-could-not-hold.md), [finding 393](docs/findings/393-the-number-was-a-double-and-the-text-was-lost.md); ECMA-262 §25.5.1, §25.5.3.1, §25.5.5 | **A reviver's `source` is the value's own span, not the document's** — `JSON.parse('  1  ')` gives `"1"`. Key the map by `(holder, key)`, never by value identity: a primitive has none |
| | | | |
| **The global object's `[[Delete]]` agrees with its own `[[GetOwnProperty]]`, since Phase 431** ([`docs/phases/431-two-answers-from-one-object.md`](docs/phases/431-two-answers-from-one-object.md), [finding 431](docs/findings/431-the-descriptor-and-the-delete-disagreed.md)): §9.1.1.4 makes exactly two things non-configurable — `undefined`/`NaN`/`Infinity` (§18.1.1-§18.1.3) and a `var`/function **a script** declares at the top level — and every library global §17-§26 defines is `{writable: true, enumerable: false, configurable: true}`. This engine had it backwards: it marked every engine-declared binding non-configurable and refused all of them in `[[Delete]]`, while `[[GetOwnProperty]]` derived `configurable` from **`!is_readonly`** — right for `undefined`, wrong for every function, and right by coincidence for one name out of three. So `Object.getOwnPropertyDescriptor(globalThis, "Object").configurable` was `true` and `delete globalThis.Object` was `false`, two answers from one object. `JsEnvironment::is_configurable_binding` is now the one table (`is_permanent` for the three values and for `hoist_var`'s declarations, `is_builtin` for the library, neither for an implicit global), asked by both paths, and a configurable delete calls **`remove_own`** — Node makes the bare name a `ReferenceError` afterwards, so a boolean alone would have traded one disagreement for a worse one. **The lesson, and it is Phase 414's again:** the Phase 404 suite asserted *both* halves of the contradiction twenty lines apart, under a header saying every expectation was measured against Node — which answers `true` to both. One line was measured and the other written from what the engine did, and the header's credibility covered for it. **A test asserting two things that cannot both be true is a red flag in the shape of a green tick.** Red on three engines, each a different assertion: as it was (`:49`), descriptor fixed but `[[Delete]]` still refusing (`:49`), and `true` without removing the binding (`:50`). **Still open:** a sloppy implicit global (`zz = 3`) still reports `configurable: false`, because the name is created at `src/js/JsEval.cpp:2439` through the same `define` a built-in uses — `define_script_global` is written and waiting for that one call site, which the open #421 owns. | `include/litebird/js/JsInterpreter.hpp` (`Binding::is_permanent`, `is_configurable_binding`, `define_script_global`, `hoist_var`), `include/litebird/js/Heap.hpp` (`own_property_hook`), `src/js/JsInterpreter.cpp` (`install_global_object_hooks`), `src/js/BuiltinsObject.cpp` (`descriptor_for`'s global branch) | ECMA-262 §9.1.1.4, §18.1.1-§18.1.3, the Note under §18.1; `tests/suites/431-global-configurable.cpp`, `404-global-object-owns-its-globals.cpp` | **`configurable` is not `writable`.** Answering one attribute from another is how a wrong answer looks measured. Ask the binding, in one function, and make `[[Delete]]` remove what it says it removed |
| | | | |
| **The fourth kind of global is the one that can be deleted, since Phase 436** ([`docs/phases/436-a-three-way-table-with-four-kinds-in-it.md`](docs/phases/436-a-three-way-table-with-four-kinds-in-it.md), [finding 436](docs/findings/436-the-fourth-kind-of-global.md)): §9.1.1.4 has **four** kinds of binding on the global object, and Phase 431 wrote the table as three progressive tests — `if (is_permanent) false; if (!is_builtin) return is_deletable; return true;` — so a sloppy implicit global (`zz = 3`), which is none of the three, fell into a branch written about sloppy `eval` and answered `false`. It reported `{writable: true, enumerable: true, configurable: false}` and refused `delete globalThis.zz`; Node gives `true,true,true` and `true`. **The defect was not a missing case but an unrepresentable one:** an enumeration of kinds reads correctly for every kind it has and invents a verdict for the one it lacks. `is_configurable_binding` is now `return !binding.is_permanent;` — the standard's own shape, &ldquo;these two are not configurable, the rest are&rdquo;, which has no row to fall through — and `assign_identifier`'s implicit-global tail calls **`define_script_global`**, which clears `is_builtin` so the name stops reporting `enumerable: false` like a library global. **Two causes, one symptom:** the first attempt changed only the call site and the output was *identical* — the rule still fell to `is_deletable`. That is evidence about the other half of the rule, not evidence the build did not take. Red at `436-implicit-global.cpp:36` on the old rule, and at `431-global-configurable.cpp:101` too, which fires first; the new suite fails on its own with 431 moved aside. test262 15196 → **15211 (+15, 0 lost)**. **Still open:** `delete globalThis.document` answers `true` and `document` still resolves — the property and the binding come apart, because a page global is attached by `dom::attach_window` rather than by this environment record. | `include/litebird/js/JsInterpreter.hpp` (`is_configurable_binding`, `define_script_global`), `src/js/JsEval.cpp` (`assign_identifier`) | ECMA-262 §9.1.1.4, §18.1.1-§18.1.3; `tests/suites/436-implicit-global.cpp`, `431-global-configurable.cpp` | **State the exceptions, not the permitted cases** — `!is_permanent` has no unhandled row; a list of tests has one for every kind not thought of. Nothing in a page's Window path goes through these two lines |
