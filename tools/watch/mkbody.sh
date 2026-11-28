#!/bin/bash
# The text of the landing session's pull request at the end of a watch:
#
#   tools/watch/mkbody.sh      prints the name of the file it wrote
#
# $WATCH_STATE/body.head.md, then the table of the lots (`watch.sh table`),
# then $WATCH_STATE/body.tail.md, written to $WATCH_STATE/body.md. A mark
# `@NAME@` in the two files is replaced by the value of NAME in
# $WATCH_STATE/marks.json (hours, counts, hashes: what is only known at the
# very end), and `@OPEN@` by the list of the pull requests that are open
# now, each with its head, its agent and whether it is a draft under
# `WIP:`. Every mark has to occur exactly once, and none may be left: a
# text that went out with `@END@` in it, or with an hour written in two
# places and changed in one, is what this is for. Then
#
#   gh api -X PATCH repos/OWNER/REPO/pulls/N -f title=... -F body=@$WATCH_STATE/body.md
set -u
cd "$(git rev-parse --show-toplevel)" || exit 9
STATE="${WATCH_STATE:-/tmp/watch}"
for f in body.head.md body.tail.md marks.json; do [ -f "$STATE/$f" ] || { echo "mkbody: no $STATE/$f"; exit 9; }; done
{ cat "$STATE/body.head.md"; timeout 60 tools/watch/watch.sh table; cat "$STATE/body.tail.md"; } > "$STATE/body.raw" || exit 9
timeout 30 gh pr list --limit 100 --json number,title,headRefOid,isDraft,body > "$STATE/open.json" || { echo "mkbody: the open pull requests could not be listed"; exit 9; }
python3 - "$STATE" <<'PY'
import json, re, sys
state = sys.argv[1]
raw = open(f"{state}/body.raw").read()
marks = json.load(open(f"{state}/marks.json"))
lines = []
for pr in sorted(json.load(open(f"{state}/open.json")), key=lambda p: p["number"]):
    title = pr["title"]
    if title.startswith(("WATCH:", "BOARD:")):
        continue
    agent = re.search(r"(?m)^Agent: *(\w+)", pr.get("body") or "")
    who = f" ({agent.group(1)})" if agent else " (no name given)"
    wip = title.startswith("WIP")
    what = ("`WIP:`, a draft" if wip and pr["isDraft"] else "`WIP:`, **no draft**" if wip
            else "no `WIP:`, a draft" if pr["isDraft"] else "finished, not taken")
    lines.append(f"- #{pr['number']} `{pr['headRefOid'][:7]}` {re.sub(r'^WIP:? *', '', title)}{who}: {what}")
lines.append("- #219, the board, which is never merged.")
marks["OPEN"] = "\n".join(lines)
for name, value in marks.items():
    mark = f"@{name}@"
    if raw.count(mark) != 1:
        sys.exit(f"mkbody: {mark} occurs {raw.count(mark)} time(s) in the two files, not once")
    raw = raw.replace(mark, str(value))
left = sorted(set(re.findall(r"@[A-Z_0-9]+@", raw)))
if left:
    sys.exit(f"mkbody: no value in marks.json for {', '.join(left)}")
open(f"{state}/body.md", "w").write(raw)
print(f"{state}/body.md: {len(raw)} characters, {len(lines) - 1} open pull request(s) listed")
PY
