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
echo "$STATE" > /dev/null
echo; echo "$((cases - failed))/$cases pass"
[ "$failed" -eq 0 ]
