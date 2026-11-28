#!/bin/bash
# One look at everything that can change under a watch, for the landing
# session:
#
#   tools/watch/poll.sh [SINCE]     SINCE as 2026-11-28T14:00:00Z; eight minutes ago when left out
#
# It prints the hour and main; what `watch.sh cands` says (with what it
# writes to stderr: a draft that is finished, a claim that is no draft, a
# head it does not take and why); the open pull requests, with what GitHub
# says of merging each; the comments since SINCE that are not the landing
# session's own; the pushes to other branches; what was merged or closed;
# and the last line of `rule.py`. It changes nothing and writes no beat of
# its own (`cands` does).
#
# Until 28 November 2026 this was written again by hand in /tmp at the
# beginning of every watch, six times that day alone, each time a little
# differently: once with a list of closed pull requests that stopped at the
# eight newest, so that two of a lot of three were not in it.
set -u
cd "$(git rev-parse --show-toplevel)" || exit 9
R="$(git remote get-url origin | sed -E 's#^.*github\.com[:/]##; s#\.git$##')"
NAME="${WATCH_NAME:-Lander}"
mine="refs/heads/$(git rev-parse --abbrev-ref HEAD)"
since="${1:-$(date -u -d '8 minutes ago' +%Y-%m-%dT%H:%M:%SZ)}"
echo "== $(date -u +%H:%M:%S) main $(timeout 20 git ls-remote origin refs/heads/main | cut -c1-7)  (since $since)"
echo "cands: $(timeout 85 tools/watch/watch.sh cands 2>&1 | cut -c1-420)"
echo "-- open:"
timeout 20 gh pr list --limit 100 --json number,title,isDraft,mergeStateStatus,headRefOid,updatedAt \
  --jq '.[] | select(.title | test("^BOARD") | not) | "#\(.number)\(if .isDraft then " draft" else "" end) \(.mergeStateStatus) \(.headRefOid[0:7]) \(.updatedAt[11:19]) \(.title[0:64])"'
echo "-- comments, not $NAME's:"
timeout 30 gh api "repos/$R/issues/comments?since=$since&per_page=100&sort=created&direction=asc" \
  --jq ".[] | select(.body | test(\"^$NAME:\") | not) | \"\(.created_at[11:19]) #\(.issue_url | split(\"/\") | last) [\(.id)] \(.body[0:330] | gsub(\"\n\"; \" \"))\""
echo "-- pushes to other branches:"
timeout 30 gh api "repos/$R/activity?per_page=60" \
  --jq ".[] | select(.timestamp > \"$since\") | select(.ref != \"$mine\") | \"\(.timestamp[11:19]) \(.activity_type) \(.ref | sub(\"refs/heads/\"; \"\")) \(.before[0:7])..\(.after[0:7])\""
echo "-- merged or closed since:"
timeout 20 gh pr list --state closed --limit 40 --json number,title,state,closedAt \
  --jq ".[] | select((.closedAt // \"\") > \"$since\") | \"#\(.number) \(.state) \(.closedAt[11:19]) \(.title[0:60])\""
echo "-- rule: $(timeout 40 python3 tools/watch/rule.py 2>&1 | tail -1)"
