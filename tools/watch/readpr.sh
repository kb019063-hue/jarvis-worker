#!/bin/bash
# What a pull request is, read before it is taken into a lot or made ready:
#
#   tools/watch/readpr.sh N [LINES]     LINES of its text are shown (10)
#
# Its title, whether it is a draft, what GitHub says of merging it; how far
# it is behind main and what its commits are; the files it changes; the
# check runs of its head; when it was renamed, made ready, made a draft,
# pushed over; whether it merges with main and what that merge would take
# out of AGENTS.md; what it changes outside the directories where every
# pull request adds its own files; the beginning of its text; its last
# comments. It fetches the head to origin/prN (as `watch.sh plan` does) and
# changes nothing else.
#
# "Taken as it is" is said of a pull request the landing session has read.
# This is that reading, in one command: on 28 November 2026 it showed in a
# minute that #544 had been opened with no claim and changed the very line
# #538 had claimed a quarter of an hour before.
set -u
cd "$(git rev-parse --show-toplevel)" || exit 9
n="${1:?the number of a pull request}"
R="$(git remote get-url origin | sed -E 's#^.*github\.com[:/]##; s#\.git$##')"
timeout 60 git fetch -q origin main "+refs/pull/$n/head:refs/remotes/origin/pr$n" || { echo "could not fetch #$n"; exit 9; }
h="$(git rev-parse "origin/pr$n")"
timeout 30 gh pr view "$n" --json title,isDraft,mergeStateStatus,mergeable,headRefOid,headRefName,createdAt,updatedAt \
  --jq '"#'"$n"' \(.title)\n  draft=\(.isDraft) \(.mergeStateStatus) \(.mergeable) head \(.headRefOid[0:7]) branch \(.headRefName) created \(.createdAt[11:19]) updated \(.updatedAt[11:19])"'
[ "$(timeout 30 gh pr view "$n" --json headRefOid --jq .headRefOid)" = "$h" ] || echo "  !! the head fetched (${h:0:7}) is not the head GitHub names: read it again"
echo "  behind main by $(git rev-list --count "$h..origin/main"), ahead by $(git rev-list --count "origin/main..$h"); merge base $(git merge-base "$h" origin/main | cut -c1-7); parents of the head: $(git log -1 --format=%p "$h")"
echo "-- commits:"; git log --format='  %h %cd %s' --date=format:'%H:%M:%S' "origin/main..$h" | cut -c1-120 | head -14
echo "-- files (against the merge base):"; git diff --stat "origin/main...$h" | tail -14 | cut -c1-110
echo "-- runs on the head: $(timeout 30 gh api "repos/$R/commits/$h/check-runs?per_page=100" \
  --jq '[.check_runs | group_by(.name)[] | max_by(.id) | "\(.status)/\(.conclusion)"] | group_by(.) | map("\(.[0]) x\(length)") | join(", ")')"
echo "-- events:"
timeout 30 gh api "repos/$R/issues/$n/events?per_page=100" \
  --jq '.[] | select(.event == "renamed" or .event == "ready_for_review" or .event == "convert_to_draft" or .event == "labeled" or .event == "unlabeled" or .event == "head_ref_force_pushed") | "  \(.created_at[11:19]) \(.event) \(.rename.to // .label.name // "" | .[0:70])"' | tail -10
if git merge-tree --write-tree origin/main "$h" > /dev/null 2>&1; then
  tree="$(git merge-tree --write-tree origin/main "$h" | head -1)"
  echo "-- merges with main: tree ${tree:0:12}; check_docs --kept: $(python3 tools/ci/check_docs.py --base origin/main --kept "$tree" 2>&1 | tail -1)"
  echo "-- what the merge changes outside src/ include/ tests/suites/ docs/phases/ docs/findings/:"
  git diff --stat origin/main "$tree" -- . ':!src' ':!include' ':!tests/suites' ':!docs/phases' ':!docs/findings' | tail -10 | cut -c1-110
else
  echo "-- CONFLICTS with main: $(git merge-tree --write-tree --name-only origin/main "$h" 2>/dev/null | sed -n '2,/^$/p' | tr '\n' ' ')"
fi
echo "-- what check_pr.py says of its claim:"; timeout 80 python3 tools/ci/check_pr.py claim "$n" 2>&1 | grep -E '^NO|opened first' | cut -c1-200 | head -6
echo "-- text:"; timeout 30 gh pr view "$n" --json body --jq .body | head -"${2:-10}" | cut -c1-300
echo "-- last comments:"; timeout 30 gh api "repos/$R/issues/$n/comments?per_page=100" --jq '.[-4:][] | "  \(.created_at[11:19]) \(.body[0:260] | gsub("\n"; " "))"'
