#!/bin/bash
# What the landing session needs before a watch, in a sandbox that is new:
#
#   tools/watch/setup.sh
#
# Every turn that follows the merge of the landing session's own pull
# request begins in a new sandbox, on a new branch from main: /tmp is
# empty, the clone is shallow, and Python has no yaml. This puts back what
# a watch works with, and does nothing where it is there already:
#
#   - the whole history (`plan`, `takeable` and `rehead` ask for merge
#     bases and for commits of the branches);
#   - pyyaml (check_docs.py and check_ci_parts.py read ci.yml);
#   - /tmp/own, a worktree on the branch `own`: the watch's changes to its
#     tools and to AGENTS.md are committed there, and `watch.sh full` puts
#     them on top of every lot;
#   - /tmp/sandbox, a detached worktree: the lot is built and its suites
#     are run there, beside CI, with
#       git -C /tmp/sandbox checkout -q -f --detach "$(cat $WATCH_STATE/lot.head)"
#     and then, in another command, `make -j2 all && make -j2 test`;
#   - $WATCH_STATE (/tmp/watch), where watch.sh keeps what it knows.
#
# Then: `tools/watch/watch.sh start "YYYY-MM-DD HH:MM"`.
set -u
cd "$(git rev-parse --show-toplevel)" || exit 9
STATE="${WATCH_STATE:-/tmp/watch}"
if [ "$(git rev-parse --is-shallow-repository)" = true ]; then
  timeout 300 git fetch -q --unshallow origin || { echo "setup: the history could not be fetched"; exit 9; }
else
  timeout 120 git fetch -q origin || { echo "setup: origin could not be fetched"; exit 9; }
fi
python3 -c 'import yaml' 2> /dev/null || timeout 120 pip install -q --break-system-packages pyyaml > /dev/null 2>&1 \
  || echo "setup: pyyaml could not be installed: check_docs.py will say so when it is asked"
[ -e /tmp/own ] || git worktree add -q -b own /tmp/own origin/main || echo "setup: no worktree /tmp/own (is there a branch \`own\` already? \`git worktree add /tmp/own own\`)"
[ -e /tmp/sandbox ] || git worktree add -q --detach /tmp/sandbox origin/main || echo "setup: no worktree /tmp/sandbox"
mkdir -p "$STATE/land"
echo "setup $(date -u +%H:%M:%S): branch $(git rev-parse --abbrev-ref HEAD) on main $(git rev-parse --short=7 origin/main), $(git rev-list --count origin/main) commits; own: $(git -C /tmp/own rev-parse --short=7 HEAD 2> /dev/null || echo none); sandbox: $(git -C /tmp/sandbox rev-parse --short=7 HEAD 2> /dev/null || echo none)"
echo "rule: $(timeout 40 python3 tools/watch/rule.py 2>&1 | tail -1)"
