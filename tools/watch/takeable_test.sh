#!/bin/bash
# What `takeable` of tools/watch/watch.sh answers, on made-up answers:
#
#   bash tools/watch/takeable_test.sh [WATCH_SH]
#
# `takeable` decides whether a finished pull request goes into a lot, and
# `ruled_out` what a rule of GitHub's on main has against it. Both ask git
# and GitHub. Here the functions they ask through (`green`, `runs_of`,
# `github_says`, `code_was_green`, `came_back_without_a_run`) answer what a
# case says, so a case is one line: what the head is like, and what is to
# come out (taken or not, and the first words of why). Status 1 when a case
# fails. It needs no network and changes nothing.
#
# Why it is here. Until 28 November 2026 watch.sh had no test but `bash -n`,
# and what it did under a rule on main had been written that morning with
# no rule to try it on. The person's rule was on from 11:28 UTC. Read
# against it, `takeable` refused a head whose code was green and whose own
# run was going ("wait for it", among the things to tell its agent): under
# the rule that is every pull request that comes back from a conflict in
# documents, since its merge of main is pushed without the mark that skips
# CI. The seventh and eighth case below are that head.
set -u
script="${1:-$(dirname "$0")/watch.sh}"
STATE="$(mktemp -d)"; trap 'rm -rf "$STATE"' EXIT
: > "$STATE/notits"
eval "$(sed -n '/^ruled_out() {/,/^}/p;/^takeable() {/,/^}/p' "$script")"
declare -F takeable > /dev/null && declare -F ruled_out > /dev/null || { echo "takeable and ruled_out were not found in $script"; exit 1; }
green() { [ "$G" = 1 ]; }                       # the head's own run passed
runs_of() { echo "$RUNS"; }                     # its check runs, as `runs_of` words them
came_back_without_a_run() { [ "$BACK" = 1 ]; }  # main merged under the mark into the head that was sent back
code_was_green() { [ "$WAS" = 1 ]; }            # a commit before it was green, and only documents changed since
github_says() { echo "$GS"; }                   # mergeStateStatus
git() { :; }                                    # no file added under a wrong number, no merge tree to read
python3() { :; }
failed=0; cases=0
try() {  # try "what the case is" 0|1 "the first words of WHY"; G WAS BACK GS RUNS are set in front of it
  local rc; WHY=""; takeable 7 abc1234abc1234abc1234abc1234abc1234abc12; rc=$?
  cases=$((cases + 1))
  if [ "$rc" = "$2" ] && [[ "$WHY" == "$3"* ]]; then echo "  [ok  ] $1"
  else echo "  [FAIL] $1"; echo "         wanted $2 '$3'"; echo "         got    $rc '$WHY'"; failed=$((failed + 1)); fi
}
short="completed/skipped x11, completed/success x7"
no_run="GitHub reports BLOCKED: the rule on main wants a passed run on its head abc1234, which has: no run at all. It needs a push without the mark"
G=1 WAS=0 BACK=0 GS=CLEAN   RUNS="$short" try "its own run passed: taken" 0 "green"
G=0 WAS=1 BACK=0 GS=CLEAN   RUNS="" try "no rule; its code was green and the head has no run: taken" 0 "its code was green"
G=0 WAS=0 BACK=1 GS=UNSTABLE RUNS="" try "no rule; it came back with main merged under the mark: taken" 0 "its code was green"
G=0 WAS=1 BACK=0 GS=BLOCKED RUNS="" try "a rule; its code was green and the head has no run: not taken, and what its agent is told" 1 "$no_run"
G=0 WAS=0 BACK=1 GS=BLOCKED RUNS="" try "a rule; it came back under the mark: the same" 1 "$no_run"
G=0 WAS=1 BACK=0 GS=BLOCKED RUNS="completed/cancelled x7" try "a rule; its code was green and the run on the head was cancelled: not taken" 1 "GitHub reports BLOCKED: the rule on main wants a passed run on its head abc1234, which has: completed/cancelled x7"
G=0 WAS=1 BACK=0 GS=BLOCKED RUNS="in_progress/null x5, queued/null x2" try "a rule; its code was green and its own run is going: taken while it goes" 0 "eager: its own run is still going"
G=0 WAS=1 BACK=0 GS=BLOCKED RUNS="completed/skipped x11, queued/null x7" try "...and when that run has only been queued" 0 "eager: its own run is still going"
G=0 WAS=0 BACK=0 GS=BLOCKED RUNS="in_progress/null x7" try "a rule; a finished head whose first run is going: taken while it goes" 0 "eager: its own run is still going"
G=0 WAS=0 BACK=0 GS=CLEAN   RUNS="in_progress/null x7" try "no rule; the same head: taken while it goes" 0 "eager: its own run is still going"
G=0 WAS=0 BACK=0 GS=BLOCKED RUNS="" try "no run on the head and its code never green (a claim's empty commit): not taken" 1 "no run on its head, and its code was never green"
G=0 WAS=1 BACK=0 GS=BLOCKED RUNS="completed/failure x1, completed/success x6" try "its own run failed: not taken, whatever was green before" 1 "its own run failed"
G=0 WAS=1 BACK=0 GS=BEHIND  RUNS="" try "a rule that wants main in the branch: not taken, and the person has to be told" 1 "GitHub reports BEHIND: a rule on main wants main in the branch. No lot can land under that"

# `runs_of`: what it makes of the check runs GitHub lists for a commit. Asked
# with `jq`, which is what `gh api --jq` is; where there is none, not asked.
eval "$(sed -n "/^RUNS_JQ='/p" "$script")"
said() {  # said "what the case is" "the line wanted" "the answer of .../check-runs"
  local got; got="$(jq -r "$RUNS_JQ" <<< "$3")"; cases=$((cases + 1))
  if [ "$got" = "$2" ]; then echo "  [ok  ] $1"
  else echo "  [FAIL] $1"; echo "         wanted '$2'"; echo "         got    '$got'"; failed=$((failed + 1)); fi
}
run() { printf '{"id": %s, "name": "%s", "status": "%s", "conclusion": %s}' "$1" "$2" "$3" "$4"; }
if [ -z "${RUNS_JQ:-}" ]; then echo "  [FAIL] RUNS_JQ was not found in $script"; failed=$((failed + 1)); cases=$((cases + 1))
elif command -v jq > /dev/null; then
  said "one run: its jobs" "completed/skipped x1, completed/success x2" \
    "{\"check_runs\": [$(run 1 Build completed '"success"'), $(run 2 Documents completed '"success"'), $(run 3 Deep completed '"skipped"')]}"
  # #522 on 28 November: the run that was cancelled (ids 11 to 13) and the one that passed (21 to 23)
  said "two runs on one commit, the first cancelled by the second: the second's jobs" "completed/skipped x1, completed/success x2" \
    "{\"check_runs\": [$(run 21 Build completed '"success"'), $(run 22 Documents completed '"success"'), $(run 23 Deep completed '"skipped"'),
                       $(run 11 Build completed '"cancelled"'), $(run 12 Documents completed '"cancelled"'), $(run 13 Deep completed '"skipped"')]}"
  said "a job that failed and is being run again: going, not failed" "completed/success x1, in_progress/null x1" \
    "{\"check_runs\": [$(run 1 Build completed '"failure"'), $(run 2 Documents completed '"success"'), $(run 5 Build in_progress null)]}"
  said "a job that passed and then failed when it ran again: failed" "completed/failure x1" \
    "{\"check_runs\": [$(run 1 Build completed '"success"'), $(run 5 Build completed '"failure"')]}"
  said "no check run at all: nothing" "" '{"check_runs": []}'
else echo "  (no jq here: runs_of was not asked)"; fi
echo; echo "$((cases - failed))/$cases pass"
[ "$failed" -eq 0 ]
