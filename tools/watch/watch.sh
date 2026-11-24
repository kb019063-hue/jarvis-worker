#!/bin/bash
# The landing session's tools for one watch (AGENTS.md, "Working together",
# point 4). A sandbox is new at every turn and /tmp with it, so they are here
# and not in /tmp, where they were written again at the start of every watch
# until 24 November 2026.
#
#   tools/watch/watch.sh start "YYYY-MM-DD HH:MM"   the hour the status comment names; the first look
#   tools/watch/watch.sh look            the beat, and what changed since the look before
#   tools/watch/watch.sh st              a look, the lot, and the CI state of every open pull request
#   tools/watch/watch.sh cands           the pull requests that can be taken now, one line of numbers
#                                        (why not, and why without a green run of its own, on stderr)
#   tools/watch/watch.sh plan N [N...]   which of them merge after one another onto main (one line of
#                                        numbers); the others, and the files, on stderr. Changes nothing
#   tools/watch/watch.sh build N [N...]  this branch = main + an empty commit + those heads in the
#                                        order given (not pushed). 0 built; 5 built without one that
#                                        conflicts (it says which, in which files); 6 one is not
#                                        finished and green; 7 the tree is not the one a chain of
#                                        merges gives
#   tools/watch/watch.sh full [N...]     `build`, and on the lot what only this branch brings to main:
#                                        the commits of the branch `own`, the carried pull requests
#                                        ($STATE/carry), the empty rows, README's count; the checks
#                                        that read them; `push`. 9 when anything said no (not pushed)
#   tools/watch/watch.sh push            push the lot (forced, with lease); its run starts
#   tools/watch/watch.sh lot             0 READY; 2 main moved or a job failed; 3 a head moved or a
#                                        pull request is not open; 4 running; 5 the own run of one
#                                        that was taken while it was still going has failed
#   tools/watch/watch.sh rehead          after `lot` said 3: go on with the heads as they are now, when
#                                        main and they still give the lot's tree (an agent that only
#                                        merged main into its branch)
#   tools/watch/watch.sh wait [SECONDS]  `lot` every 20 s, until it is not 4 or the time is up; a look
#   tools/watch/watch.sh land [last]     merge each pull request of the lot as it is, in number order.
#                                        `last`: the watch's own merge follows, so no merge starts a
#                                        run on main, and the tree to reach is $STATE/lot.asis
#   tools/watch/watch.sh once N          #N is green with main in its head and no lot is in test:
#                                        merge it at once
#   tools/watch/watch.sh taken HH:MM     a "taken" note in each pull request of the lot, a board line
#   tools/watch/watch.sh landed "how" N [N...]   a "landed" note in each, a board line
#   tools/watch/watch.sh sendback N "why, and what to do"   the line that sends #N back, in #N and
#                                        on the board; #N is then left alone until its head moves
#   tools/watch/watch.sh note N FILE     comment FILE on #N, print the comment's id
#
# State is in $WATCH_STATE (default /tmp/watch): end (the hour), lot.heads
# (lines "N SHORTSHA"), lot.main, lot.head, lot.asis, sentback (lines
# "N SHORTSHA"), ignore (one number a line: a pull request that has no agent
# and is left alone, as #193 is), land/ (the bodies of the merges and the
# notes).
#
# Nothing here pushes to anybody else's branch or closes a pull request.

set -u
cd "$(git rev-parse --show-toplevel)" || exit 9
STATE="${WATCH_STATE:-/tmp/watch}"
NAME="${WATCH_NAME:-Lander}"
mkdir -p "$STATE/land"
R="$(git remote get-url origin | sed -E 's#^.*github\.com[:/]##; s#\.git$##')"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
BOARD="${CHECK_PR_BOARD:-$(timeout 30 gh pr list --state open --limit 100 --json number,title --jq '[.[] | select(.title | test("^BOARD:"))][0].number' 2>/dev/null)}"

runs_of() {  # runs_of FULLSHA -> "completed/success x12" and the like
  timeout 30 gh api "repos/$R/commits/$1/check-runs?per_page=100" \
    --jq '[.check_runs[] | "\(.status)/\(.conclusion)"] | group_by(.) | map("\(.[0]) x\(length)") | join(", ")'
}
whole() {  # whole FULLSHA: every check run of it has ended in success, and there are ten or more
  local runs; runs="$(runs_of "$1")"
  [[ "$runs" =~ ^completed/success\ x([0-9]+)$ ]] && [ "${BASH_REMATCH[1]}" -ge 10 ]
}
green() {  # green FULLSHA: `whole`, or the short run of a pull request with the label `lot` passed:
           # two jobs or more in success and the rest skipped (.github/workflows/ci.yml). Enough for
           # a pull request to be taken into a lot, whose own run is all twelve; never for the lot.
  local runs; runs="$(runs_of "$1")"
  [[ "$runs" =~ ^completed/success\ x([0-9]+)$ ]] && [ "${BASH_REMATCH[1]}" -ge 10 ] && return 0
  [[ "$runs" =~ ^completed/skipped\ x([0-9]+),\ completed/success\ x([0-9]+)$ ]] && [ "${BASH_REMATCH[2]}" -ge 2 ]
}
came_back_without_a_run() {  # came_back_without_a_run N FULLSHA
  # AGENTS.md, "Working together", point 4: a pull request that went back for
  # a conflict in documents comes back as `main` merged into the head that
  # was green, with [skip ci], and is taken with no run of its own.
  local n="$1" head="$2" first second old auto
  git log -1 --format=%B "$head" | grep -qF '[skip ci]' || return 1
  first="$(git rev-parse -q --verify "$head^1")" || return 1
  second="$(git rev-parse -q --verify "$head^2")" || return 1
  old="$(grep -E "^$n " "$STATE/sentback" 2>/dev/null | tail -1 | cut -d' ' -f2)"
  [ -n "$old" ] && [ "$(git rev-parse --short=7 "$first")" = "$old" ] || return 1
  git merge-base --is-ancestor "$second" origin/main || return 1
  # what git makes of the two alone, conflict marks and all, against what the
  # agent made: they may differ in documents only
  auto="$(git merge-tree --write-tree "$first" "$second" 2>/dev/null | head -1)"
  [ -n "$auto" ] || return 1
  [ -z "$(git diff --name-only "$auto" "$head^{tree}" | grep -vE '\.md$')" ]
}
code_was_green() {  # code_was_green FULLSHA
  # A head whose code is code that was green: a commit of the branch before
  # it has a green run, and the head is that commit, merged with `main` by
  # git alone where `main` was merged since, with changes to documents only
  # on top (a conflict in AGENTS.md resolved, a write-up finished). Its own
  # run, if it has one, need not be waited for: the lot is `main` with this
  # head merged in, the same tree, and the lot's run is the test. On 24
  # November #313 lay finished for 25 minutes with such a head: its agent
  # had merged `main` with the mark that skips CI, and the commit that was to
  # start a run again carried the mark in its message too.
  local head="$1" c g="" m auto changed runs
  for c in $(git rev-list --first-parent -8 "$head" | tail -n +2); do
    git merge-base --is-ancestor "$c" origin/main && return 1   # nothing of its own was ever tested
    runs="$(runs_of "$c")"
    case "$runs" in *failure*|*timed_out*) return 1 ;; esac
    if green "$c"; then g="$c"; break; fi   # a run the next push cancelled says nothing
  done
  [ -n "$g" ] || return 1
  m="$(git merge-base "$head" origin/main)"
  if git merge-base --is-ancestor "$m" "$g"; then auto="$(git rev-parse "$g^{tree}")"
  else auto="$(git merge-tree --write-tree "$g" "$m" 2>/dev/null | head -1)"; fi
  [ -n "$auto" ] || return 1
  changed="$(git diff --name-only "$auto" "$head^{tree}" | grep -vE '^([^/]+\.md|docs/.+|tests/[^/]+/README\.md)$')"
  [ -z "$changed" ]
}
takeable() {  # takeable N FULLSHA; says why in $WHY
  local runs
  WHY="green"; green "$2" && return 0
  runs="$(runs_of "$2")"
  case "$runs" in *failure*|*timed_out*) WHY="its own run failed"; return 1 ;; esac
  WHY="its code was green"; came_back_without_a_run "$1" "$2" && return 0
  code_was_green "$2" && return 0
  # A finished pull request whose own run is still going is taken as well
  # (WATCH_EAGER=0 turns that off): its run and the lot's go side by side, and
  # it lands when the lot's has passed. Waiting for its own run first cost
  # every pull request one run's length, 8 to 12 minutes, before the lot's
  # run began. If its own run fails, `lot` says so (5) and the lot is built
  # again without it; main still gets only a tree a whole run passed on.
  # A head with no run at all is not taken this way: a claim's empty commit
  # under a title that has lost its WIP: would be merged as the pull request.
  WHY="eager: its own run is still going"
  [ "${WATCH_EAGER:-1}" = "1" ] && [ -n "$runs" ] && return 0
  WHY="no run on its head, and its code was never green"; return 1
}
note() {  # note N FILE -> "#N COMMENTID"
  local url; url="$(timeout 40 gh pr comment "$1" --body-file "$2" 2>&1 | tail -1)"
  echo "#$1 ${url##*-}"
}
open_ones() {  # "N FULLSHA fin|WIP MERGEABLE..." of every open pull request that is a claim
  timeout 40 gh pr list --state open --limit 100 --json number,title,headRefOid,mergeable \
    --jq '.[] | select((.title | test("^(BOARD|WATCH):")) | not) | "\(.number) \(.headRefOid) \(if (.title | test("^WIP")) then "WIP" else "fin" end) \(.mergeable)"' | sort -n
}
fetch_pr() { timeout 60 git fetch -q origin "+refs/pull/$1/head:refs/remotes/origin/pr$1"; }
agent_of() { timeout 30 gh pr view "$1" --json body --jq '([.body | match("(?m)^Agent: *(\\w+)") | .captures[0].string] | first) // "-"' 2>/dev/null; }

cmd="${1:-}"; [ $# -gt 0 ] && shift
case "$cmd" in
start)
  echo "$1" > "$STATE/end"; exec "$0" look ;;

look)
  end="$(cat "$STATE/end" 2>/dev/null)"; [ -n "$end" ] || { echo "no hour: tools/watch/watch.sh start \"YYYY-MM-DD HH:MM\" first"; exit 9; }
  timeout 60 python3 tools/ci/check_pr.py watch --beat "$end" >/dev/null 2>&1 || echo "the beat failed"
  # The label `lot` gives a pull request the short run of CI while a watch is
  # on (.github/workflows/ci.yml). It is put on once, so that an agent who
  # took it off to have all twelve jobs is not given it back, and only where
  # the branch has the check_pr.py that knows a short run from a whole one:
  # an older one says "ready" to a merge on two jobs when no watch is on.
  timeout 40 gh pr list --state open --limit 100 --json number,title,labels,headRefOid \
      --jq '.[] | select((.title | test("^(BOARD|WATCH):")) | not) | select(([.labels[].name] | index("lot")) | not) | "\(.number) \(.headRefOid)"' 2>/dev/null \
    | while read -r n full; do
        grep -qx "$n" "$STATE/labelled" 2>/dev/null && continue
        fetch_pr "$n" 2>/dev/null || continue
        git grep -q 'def short_run_note' "$full" -- tools/ci/check_pr.py 2>/dev/null || continue
        timeout 20 gh api -X POST "repos/$R/issues/$n/labels" -f 'labels[]=lot' > /dev/null 2>&1 && echo "$n" >> "$STATE/labelled" && echo "label lot: #$n"
      done
  if [ -f "$STATE/watch.now" ]; then cp "$STATE/watch.now" "$STATE/watch.before"; else : > "$STATE/watch.before"; fi
  timeout 90 python3 tools/ci/check_pr.py watch > "$STATE/watch.now" 2>&1
  echo "look $(date -u '+%H:%M:%S')  main $(timeout 20 git ls-remote origin refs/heads/main | cut -c1-7)"
  diff "$STATE/watch.before" "$STATE/watch.now" | grep '^[<>]' | grep -v 'a watch is on' | cut -c1-200
  echo "to take: $(grep -E 'into the lot|at once' "$STATE/watch.now" | sed -E 's/^ *#([0-9]+) \[([^]]*)\] ([0-9a-f]+).*/#\1[\2]\3/' | tr '\n' ' ')" ;;

st)
  "$0" look | tail -12
  "$0" lot > "$STATE/lot.out" 2>&1; echo "lot: exit $?: $(tail -2 "$STATE/lot.out" | tr '\n' ' ')"
  open_ones | while read -r n full wip mergeable; do
    echo "  #$n ${full:0:7} $wip $mergeable $(timeout 30 gh api "repos/$R/commits/$full/check-runs?per_page=100" --jq '([.check_runs[] | select(.conclusion == "success")] | length | tostring) + "/" + (.check_runs | length | tostring) + (if ([.check_runs[] | select(.conclusion != null and .conclusion != "success")] | length) > 0 then " FAILED: " + ([.check_runs[] | select(.conclusion != null and .conclusion != "success") | .name] | join(", ")) else "" end) + " since " + ((([.check_runs[].started_at] | min) // "-----------     ") | .[11:16])')"
  done
  date -u '+%H:%M:%S' ;;

cands)
  timeout 60 git fetch -q origin main
  # A pull request whose phase or finding number is on main, or was another
  # open one's first, will be renumbered by its agent (`check_pr.py merge N`
  # tells it to), and that push would move it under the lot: it is taken
  # when it has moved. To take one all the same, give its number to `plan`.
  clashes="$(timeout 90 python3 tools/ci/check_pr.py numbers 2>/dev/null)"
  open_ones | while read -r n full wip mergeable; do
    [ "$wip" = "fin" ] || continue
    grep -q "^$n ${full:0:7}$" "$STATE/sentback" 2>/dev/null && continue   # sent back, and has not moved
    grep -qx "$n" "$STATE/ignore" 2>/dev/null && continue                     # nobody's: left alone
    if echo "$clashes" | grep -q "^#$n: "; then
      echo "#$n ${full:0:7}: not taken, it has a number to change: $(echo "$clashes" | grep "^#$n: " | head -1 | cut -d' ' -f2-)" >&2; continue
    fi
    fetch_pr "$n" || continue
    if takeable "$n" "$full"; then printf '%s ' "$n"; [ "$WHY" = "green" ] || echo "#$n ${full:0:7}: $WHY" >&2; fi
  done; echo ;;

plan)
  # Which of these merge after one another onto main, in the order given:
  # their numbers on one line; the ones that do not, and the files, on stderr.
  # Nothing is changed. Ask this before `build` when a lot is in test, with
  # the lot's own numbers first: a newcomer that conflicts must not start the
  # lot's run again for nothing, and must not put out one that was taken
  # (both happened on 24 November, the second to #325 for two minutes).
  timeout 60 git fetch -q origin main || { echo "could not fetch main" >&2; exit 9; }
  prev="$(git rev-parse origin/main)"; ok=""
  for n in "$@"; do   # in the order given: the ones a lot in test holds first, so that no newcomer puts one of them out
    fetch_pr "$n" || { echo "could not fetch #$n" >&2; continue; }
    full="$(git rev-parse "origin/pr$n")"
    if out="$(git merge-tree --write-tree --name-only "$prev" "$full" 2>/dev/null)"; then
      prev="$(git commit-tree "$(echo "$out" | head -1)" -p "$prev" -p "$full" -m "chain $n")"; ok="$ok$n "
    else
      echo "CONFLICT #$n (after ${ok:-nothing}): $(echo "$out" | sed -n '2,/^$/p' | tr '\n' ' ')" >&2
    fi
  done
  echo "$ok" ;;

build)
  timeout 60 git fetch -q origin main || { echo "could not fetch main"; exit 9; }
  nums="$*"   # in the order given (`plan` says which order merges)
  : > "$STATE/lot.heads.new"
  for n in $nums; do
    fetch_pr "$n" || { echo "could not fetch #$n"; exit 9; }
    full="$(git rev-parse "origin/pr$n")"
    st="$(timeout 30 gh pr view "$n" --json state,headRefOid,title --jq '"\(.state) \(.headRefOid) \(if (.title | test("^WIP")) then "WIP" else "finished" end)"')"
    [ "$st" = "OPEN $full finished" ] || { echo "#$n: '$st' is not OPEN and finished at ${full:0:7}"; exit 6; }
    takeable "$n" "$full" || { echo "#$n ${full:0:7} is not green: $(runs_of "$full")"; exit 6; }
    echo "$n ${full:0:7}" >> "$STATE/lot.heads.new"
  done
  git checkout -q -B "$BRANCH" origin/main || exit 9
  git commit -q --allow-empty -m "WATCH: the lot of $(date -u '+%H:%M UTC, %-d %B') [skip ci]

The landing session's branch: main and the heads of the pull requests it
is testing together. Do not merge.

Agent: $NAME" || exit 9
  prev="$(git rev-parse origin/main)"; bad=0
  for n in $nums; do
    full="$(git rev-parse "origin/pr$n")"
    if git merge --no-ff -q -m "Lot: merge #$n (head ${full:0:7}) as it is

Agent: $NAME" "origin/pr$n" > "$STATE/merge.$n.log" 2>&1; then
      tree="$(git merge-tree --write-tree "$prev" "$full" | head -1)"
      prev="$(git commit-tree "$tree" -p "$prev" -p "$full" -m "chain $n")"
    else
      echo "CONFLICT #$n: $(git diff --name-only --diff-filter=U | tr '\n' ' ')"; git merge --abort; bad=5
      sed -i "/^$n /d" "$STATE/lot.heads.new"
    fi
  done
  if [ "$(git rev-parse 'HEAD^{tree}')" != "$(git rev-parse "$prev^{tree}")" ]; then echo "TREE MISMATCH"; exit 7; fi
  mv "$STATE/lot.heads.new" "$STATE/lot.heads"
  git rev-parse --short=7 origin/main > "$STATE/lot.main"; git rev-parse HEAD > "$STATE/lot.head"; git rev-parse HEAD > "$STATE/lot.asis"
  echo "lot built: head $(git rev-parse --short=7 HEAD) on main $(cat "$STATE/lot.main"): $(tr '\n' ' ' < "$STATE/lot.heads")"
  exit $bad ;;

full)
  # The lot (the numbers given, as they are; there may be none), and on it
  # what only this branch brings to main when the watch's own pull request
  # is merged: the commits of the branch `own` (the watch's changes to its
  # tools, written in a worktree beside the lot: `git worktree add -b own
  # /tmp/own origin/main`), the carried pull requests ($STATE/carry, lines
  # "N SHORTSHA"), the empty rows of "Open tasks", README's count of suites.
  # Then the checks that read them, and the push. Nothing is pushed when
  # one of those says no.
  "$0" build "$@"; rc=$?
  [ $rc -eq 0 ] || { echo "full: build said $rc; nothing more done"; exit 9; }
  asis="$(cut -d' ' -f1 "$STATE/lot.heads" | tr '\n' ',' | sed 's/,$//')"
  if git rev-parse -q --verify own > /dev/null; then
    base="$(git merge-base origin/main own)"
    if [ "$(git rev-parse own)" != "$base" ]; then
      git cherry-pick -x "$base..own" > "$STATE/land/pick.log" 2>&1 \
        || { echo "full: own's commits do not apply:"; tail -5 "$STATE/land/pick.log"; git cherry-pick --abort; exit 9; }
      echo "own: $(git rev-list --count "$base..own") commit(s)"
    fi
  fi
  carried=""
  if [ -s "$STATE/carry" ]; then
    while read -r n want; do
      [ -n "$n" ] || continue
      fetch_pr "$n" || { echo "full: could not fetch #$n"; exit 9; }
      h="$(git rev-parse --short=7 "origin/pr$n")"
      st="$(timeout 30 gh pr view "$n" --json state --jq .state)"
      [ "$st" = "OPEN" ] || { echo "full: #$n is $st: not carried"; continue; }
      [ "$h" = "$want" ] || echo "full: #$n is at $h, it was at $want when it was first carried"
      if git merge --no-ff --no-commit "origin/pr$n" > /dev/null 2>&1; then
        git commit -q -m "Carry #$n (head $h): it merges as it is now

Agent: $NAME" || echo "full: #$n: nothing to commit"
        echo "carried #$n $h: merged as it is"
      else
        u="$(git diff --name-only --diff-filter=U | tr '\n' ' ')"
        if [ "$u" != "AGENTS.md " ]; then echo "full: #$n conflicts in: $u; left out"; git merge --abort; continue; fi
        if ! python3 tools/watch/carry_rows.py "origin/pr$n" > "$STATE/land/carry$n.txt" 2>&1; then
          echo "full: #$n: carry_rows refused: $(tail -1 "$STATE/land/carry$n.txt")"; git merge --abort; continue
        fi
        git add AGENTS.md
        git commit -q -m "Carry #$n (head $h) with its rows of \"Open tasks\" put into the table as it is now

#$n does not merge with main: the conflict is in AGENTS.md alone, in the
table \"Open tasks\". Every other file is its own, as it is at $h. What it
did to the table was read from its own diff and done again to the table
as it is here (tools/watch/carry_rows.py):

$(sed 's/^/  /' "$STATE/land/carry$n.txt" | cut -c1-76)

Agent: $NAME"
        echo "carried #$n $h: $(tail -1 "$STATE/land/carry$n.txt")"
      fi
      carried="$carried$n,"
    done < "$STATE/carry"
  fi
  except="$(echo "$carried$asis" | sed 's/,$//')"
  # shellcheck disable=SC2086
  python3 tools/ci/check_rows.py --write ${except:+--except "$except"} > "$STATE/land/rows.log" 2>&1; tail -1 "$STATE/land/rows.log"
  make metrics-suites > "$STATE/land/metrics.log" 2>&1 || { echo "full: make metrics-suites failed"; tail -3 "$STATE/land/metrics.log"; exit 9; }
  if ! git diff --quiet; then
    git add -A
    git commit -q -m "Empty rows in \"Open tasks\" where no open pull request holds a row; README's count of suites

$(tail -1 "$STATE/land/rows.log" | cut -c1-76)
README's count of suites is the one make metrics-suites writes: $(ls tests/suites/*.cpp | wc -l) files
in tests/suites and the suites of tests/test_suite.cpp.

Agent: $NAME"
  fi
  bad=0
  python3 tools/ci/check_docs.py --base origin/main > "$STATE/land/docs.log" 2>&1 || { bad=1; echo "check_docs failed"; grep -i 'fail' "$STATE/land/docs.log" | head -5; }
  python3 tools/ci/check_numbers.py > "$STATE/land/numbers.log" 2>&1 || { bad=1; echo "check_numbers failed"; tail -3 "$STATE/land/numbers.log"; }
  python3 tools/ci/check_suites.py > "$STATE/land/suites.log" 2>&1 || { bad=1; echo "check_suites failed"; tail -3 "$STATE/land/suites.log"; }
  for t in tools/ci/check_rows.py tools/ci/check_pr.py tools/ci/check_numbers.py tools/ci/check_suites.py tools/ci/check_ci_parts.py tools/watch/carry_rows.py; do
    python3 "$t" --self-test > "$STATE/land/self.log" 2>&1 || { bad=1; echo "$t --self-test failed"; tail -2 "$STATE/land/self.log"; }
  done
  [ "$(git log -1 --format=%B | grep -c 'skip ci')" = "0" ] || { bad=1; echo "the head commit's message holds the mark: no run would start"; }
  [ $bad -eq 0 ] || { echo "full: not pushed"; exit 9; }
  "$0" push | tail -1
  git log --format='  %h %s' "$(cat "$STATE/lot.asis")..HEAD" --first-parent | cut -c1-110
  echo "full: as is: ${asis:-none}; carried: ${carried:-none}; head $(git rev-parse --short=7 HEAD) on main $(cat "$STATE/lot.main")" ;;

push)
  git rev-parse HEAD > "$STATE/lot.head"
  git push -q --force-with-lease origin "$BRANCH" 2>&1 | tail -1; date -u '+pushed %H:%M:%S' ;;

lot)
  [ -s "$STATE/lot.head" ] || { echo "no lot"; exit 4; }
  head="$(cat "$STATE/lot.head")"
  main_now="$(timeout 20 git ls-remote origin refs/heads/main | cut -c1-7)"
  [ -n "$main_now" ] || { echo "lot: could not read main"; exit 4; }
  if [ "$main_now" != "$(cat "$STATE/lot.main")" ]; then echo "lot: MAIN MOVED $(cat "$STATE/lot.main") -> $main_now"; exit 2; fi
  while read -r n sha; do
    [ -n "$n" ] || continue
    st="$(timeout 30 gh pr view "$n" --json state,headRefOid --jq '"\(.state) \(.headRefOid[0:7])"')"
    if [ "$st" != "OPEN $sha" ]; then echo "lot: #$n is '$st', the lot holds $sha"; exit 3; fi
  done < "$STATE/lot.heads"
  while read -r n sha; do   # a member that was taken while its own run was going: has that run failed?
    [ -n "$n" ] || continue
    own="$(runs_of "$(git rev-parse "origin/pr$n")")"
    case "$own" in *failure*|*timed_out*) echo "lot: #$n's own run failed ($own): build the lot again without it"; exit 5 ;; esac
  done < "$STATE/lot.heads"
  runs="$(runs_of "$head")"
  echo "lot $(date -u '+%H:%M:%S') ${head:0:7} main $main_now: ${runs:-no check runs yet}"
  if whole "$head"; then echo READY; exit 0; fi
  case "$runs" in
    *failure*|*cancelled*|*timed_out*)
      timeout 30 gh api "repos/$R/commits/$head/check-runs?per_page=100" --jq '.check_runs[] | select(.conclusion != "success" and .conclusion != null) | "   \(.name): \(.conclusion) (job \(.id))"'; exit 2 ;;
    *) exit 4 ;;
  esac ;;

rehead)
  # A head moved under the lot (exit 3 of `lot`). When its agent only merged
  # `main` into it, `main` and the heads as they are now still give the tree
  # the lot holds: the run that is going is a run on them, and the lot goes
  # on with the new head. 0 it does; 2 main moved; 3 a head that is not
  # takeable, or a conflict; 7 another tree.
  timeout 60 git fetch -q origin main || exit 9
  [ "$(git rev-parse --short=7 origin/main)" = "$(cat "$STATE/lot.main")" ] || { echo "main moved"; exit 2; }
  prev="$(git rev-parse origin/main)"; : > "$STATE/lot.heads.new"; moved=""
  while read -r n sha; do
    fetch_pr "$n" || exit 9
    full="$(git rev-parse "origin/pr$n")"
    st="$(timeout 30 gh pr view "$n" --json state,headRefOid,title --jq '"\(.state) \(.headRefOid) \(if (.title | test("^WIP")) then "WIP" else "finished" end)"')"
    [ "$st" = "OPEN $full finished" ] || { echo "#$n: '$st' is not OPEN and finished at ${full:0:7}"; exit 3; }
    if [ "${full:0:7}" != "$sha" ]; then
      takeable "$n" "$full" || { echo "#$n moved to ${full:0:7}, and that head cannot be taken: $(runs_of "$full")"; exit 3; }
      moved="$moved #$n $sha->${full:0:7}"
    fi
    tree="$(git merge-tree --write-tree "$prev" "$full" | head -1)"
    git merge-tree --write-tree "$prev" "$full" >/dev/null 2>&1 || { echo "#$n at ${full:0:7} does not merge after the ones before it"; exit 3; }
    prev="$(git commit-tree "$tree" -p "$prev" -p "$full" -m "chain $n")"
    echo "$n ${full:0:7}" >> "$STATE/lot.heads.new"
  done < "$STATE/lot.heads"
  if [ "$(git rev-parse "$prev^{tree}")" = "$(git rev-parse "$(cat "$STATE/lot.asis")^{tree}")" ]; then
    mv "$STATE/lot.heads.new" "$STATE/lot.heads"; echo "the lot goes on, on the same tree:${moved:- no head had moved}"
  else
    echo "main and the heads as they are now give another tree than the lot's:$moved"; exit 7
  fi ;;

wait)
  limit="${1:-240}"; start="$(date +%s)"; rc=4
  while :; do
    "$0" lot > "$STATE/lot.out" 2>&1; rc=$?
    if [ $rc -eq 3 ] && "$0" rehead > "$STATE/rehead.out" 2>&1; then cat "$STATE/rehead.out"; continue; fi
    [ $rc -ne 4 ] && break
    [ $(( $(date +%s) - start + 20 )) -ge "$limit" ] && break
    sleep 20
  done
  tail -3 "$STATE/lot.out"; echo "lot: exit $rc"
  "$0" look | tail -8
  exit $rc ;;

land)
  last="${1:-}"
  "$0" lot > "$STATE/lot.out" 2>&1; rc=$?
  if [ $rc -eq 3 ] && "$0" rehead; then "$0" lot > "$STATE/lot.out" 2>&1; rc=$?; fi
  [ $rc -eq 0 ] || { cat "$STATE/lot.out"; echo "STOP: the lot is not READY"; exit 1; }
  head="$(cat "$STATE/lot.head")"; main7="$(cat "$STATE/lot.main")"
  want="$(git rev-parse "$(cat "$STATE/lot.asis")^{tree}")"
  nums="$(cut -d' ' -f1 "$STATE/lot.heads" | tr '\n' ' ')"; final="$(tail -1 "$STATE/lot.heads" | cut -d' ' -f1)"
  all="$(echo $nums | sed -E 's/([0-9]+)/#\1/g; s/ /, /g')"
  while read -r n sha; do
    full="$(git rev-parse "origin/pr$n")"
    st="$(timeout 30 gh pr view "$n" --json state,headRefOid --jq '"\(.state) \(.headRefOid)"')"
    [ "$st" = "OPEN $full" ] || { echo "STOP: #$n is '$st'"; exit 3; }
    takeable "$n" "$full" || { echo "STOP: #$n is not green on ${full:0:7}: $(runs_of "$full")"; exit 3; }
    title="$(timeout 30 gh pr view "$n" --json title --jq .title)"
    if [ "$n" = "$final" ] && [ "$last" != "last" ]; then
      printf '%s\n\nLanded by the landing session (%s) as it is at %s: the last of this lot (%s), tested together with main %s in one CI run on %s, every job passed. With this merge main has the tree that run passed on, and its own run is there to save the compiled objects for the runs that follow: [objects only] (.github/workflows/ci.yml).\n' "$title" "$NAME" "${full:0:7}" "$all" "$main7" "${head:0:7}" > "$STATE/land/body$n.txt"
    else
      printf '%s\n\n[skip ci] Landed by the landing session (%s) as it is at %s: one of this lot (%s), tested together with main %s in one CI run on %s, every job passed. CI runs on main once for the lot, with the last of its merges.\n' "$title" "$NAME" "${full:0:7}" "$all" "$main7" "${head:0:7}" > "$STATE/land/body$n.txt"
    fi
  done < "$STATE/lot.heads"
  while read -r n sha; do
    full="$(git rev-parse "origin/pr$n")"
    if [ "$n" = "$final" ]; then
      timeout 30 git fetch -q origin main
      t="$(git merge-tree --write-tree origin/main "$full" | head -1)"
      [ "$t" = "$want" ] || { echo "STOP before #$n: main with it gives ${t:0:12}, the lot's tree is ${want:0:12}"; exit 7; }
    fi
    if timeout 60 gh pr merge "$n" --merge --match-head-commit "$full" --body-file "$STATE/land/body$n.txt" > "$STATE/land/merge$n.out" 2>&1; then
      echo "merged #$n ${full:0:7} $(date -u '+%H:%M:%S')"
    else
      echo "STOP: gh pr merge $n failed: $(tail -2 "$STATE/land/merge$n.out" | tr '\n' ' ')"; exit 4
    fi
  done < "$STATE/lot.heads"
  timeout 30 git fetch -q origin main
  now="$(git rev-parse 'origin/main^{tree}')"
  echo "main $(git rev-parse --short=7 origin/main): its tree is $([ "$now" = "$want" ] && echo "the lot's" || echo "NOT the lot's (${now:0:12} against ${want:0:12})")"
  [ "$now" = "$want" ] || exit 7 ;;

once)
  n="$1"
  timeout 60 git fetch -q origin main && fetch_pr "$n" || exit 9
  full="$(git rev-parse "origin/pr$n")"
  git merge-base --is-ancestor origin/main "$full" || { echo "STOP: main is not in #$n's head"; exit 2; }
  out="$(timeout 90 python3 tools/ci/check_pr.py merge "$n" --landing 2>&1)"; echo "$out" | tail -3
  echo "$out" | grep -q '^ready' || { echo "STOP: check_pr.py does not say ready"; exit 3; }
  title="$(timeout 30 gh pr view "$n" --json title --jq .title)"
  printf '%s\n\nLanded by the landing session (%s) as it is at %s: green on its own head with main %s in it, so its own CI run was a run on the tree this merge puts on main.\n' "$title" "$NAME" "${full:0:7}" "$(git rev-parse --short=7 origin/main)" > "$STATE/land/once$n.txt"
  want="$(git rev-parse "$full^{tree}")"
  if timeout 60 gh pr merge "$n" --merge --match-head-commit "$full" --body-file "$STATE/land/once$n.txt" > "$STATE/land/once$n.out" 2>&1; then
    timeout 30 git fetch -q origin main
    echo "merged #$n ${full:0:7} $(date -u '+%H:%M:%S'); main $(git rev-parse --short=7 origin/main), its tree $([ "$(git rev-parse 'origin/main^{tree}')" = "$want" ] && echo "the same as that head's" || echo "DIFFERENT")"
  else
    echo "STOP: gh pr merge $n failed: $(tail -2 "$STATE/land/once$n.out" | tr '\n' ' ')"; exit 4
  fi ;;

taken)
  at="$1"; main7="$(cat "$STATE/lot.main")"; list=""
  nums="$(cut -d' ' -f1 "$STATE/lot.heads" | tr '\n' ' ')"
  while read -r n sha; do
    others="$(for m in $nums; do [ "$m" != "$n" ] && printf '#%s, ' "$m"; done | sed 's/, $//')"
    printf '%s: #%s is taken, as it is at head `%s`. Push nothing more to it and do not merge `main` into it. It is being tested with `main` `%s`%s in one CI run (begun %s UTC), and lands when that run has passed; `python3 tools/ci/check_pr.py landed %s` says when.\n' "$NAME" "$n" "$sha" "$main7" "${others:+ and $others}" "$at" "$n" > "$STATE/land/taken$n.md"
    note "$n" "$STATE/land/taken$n.md"
    list="$list#$n (\`$sha\`), "
  done < "$STATE/lot.heads"
  printf '%s: lot of %s UTC: %s with `main` `%s`, one CI run. Each lands as it is when the run has passed.\n' "$NAME" "$at" "$(echo "$list" | sed 's/, $//')" "$main7" > "$STATE/land/taken.board.md"
  [ -n "$BOARD" ] && note "$BOARD" "$STATE/land/taken.board.md" ;;

landed)
  how="$1"; shift; names=""
  for n in "$@"; do
    read -r st at sha <<< "$(timeout 30 gh pr view "$n" --json state,mergedAt,headRefOid --jq '"\(.state) \(.mergedAt) \(.headRefOid[0:7])"')"
    [ "$st" = "MERGED" ] || { echo "#$n is $st: no note"; continue; }
    printf '%s: #%s has landed: merged as it was at `%s`, %s UTC (%s). Your next task begins with the person'"'"'s Continue.\n' "$NAME" "$n" "$sha" "$(echo "$at" | sed -E 's/.*T([0-9]+:[0-9]+):.*/\1/')" "$how" > "$STATE/land/landed$n.md"
    note "$n" "$STATE/land/landed$n.md"
    names="$names#$n ($(agent_of "$n")), "
  done
  [ -n "$names" ] || exit 0
  printf '%s: landed, each as it was: %s; %s. `main` is `%s`.\n' "$NAME" "$(echo "$names" | sed 's/, $//')" "$how" "$(timeout 20 git ls-remote origin refs/heads/main | cut -c1-7)" > "$STATE/land/landed.board.md"
  [ -n "$BOARD" ] && note "$BOARD" "$STATE/land/landed.board.md" ;;

sendback)
  n="$1"; why="$2"
  full="$(timeout 30 gh pr view "$n" --json headRefOid --jq .headRefOid)"; [ -n "$full" ] || exit 9
  printf '%s: #%s is yours again (head %s): %s\n' "$NAME" "$n" "${full:0:7}" "$why" > "$STATE/land/back$n.md"
  note "$n" "$STATE/land/back$n.md"; [ -n "$BOARD" ] && note "$BOARD" "$STATE/land/back$n.md"
  echo "$n ${full:0:7}" >> "$STATE/sentback" ;;

note)
  note "$1" "$2" ;;

*)
  sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 9 ;;
esac
