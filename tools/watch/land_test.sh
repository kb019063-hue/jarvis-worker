#!/bin/bash
# What `build` and `land` of tools/watch/watch.sh do, in a made-up
# repository:
#
#   bash tools/watch/land_test.sh [WATCH_SH]
#
# `land` is the one command of the landing session that writes to main: it
# merges the pull requests of a lot whose CI run has passed. Until 28
# November 2026 it had no test. What it does was known from the lots it had
# landed, and what it does when GitHub says no (a rule on main, in force
# since that day) had been written with no rule to try it on and had never
# happened.
#
# Here watch.sh itself runs, whole, in a world made for each case:
#
#   - `origin.git`, a bare repository with a `main` and two pull requests,
#     #7 and #8 (`refs/pull/N/head`), each adding one file;
#   - a clone of it, in which `watch.sh build 7 8` builds the lot;
#   - a `gh` that answers what the case says (what a pull request is now,
#     the check runs of a commit, what GitHub says of merging) and, asked to
#     merge, merges in `origin.git` as GitHub does, or refuses as it does.
#
# A case changes something in that world and says what is to come out: the
# status of `land`, words that are in what it printed, which pull requests
# were merged, and what main's tree is then (the lot's, as it was, or
# another). Status 1 when a case fails. It needs git 2.38 (`git merge-tree
# --write-tree`) and jq, no network, and writes nothing outside a temporary
# directory.
set -u
script="$(readlink -f "${1:-$(dirname "$0")/watch.sh}")"
[ -f "$script" ] || { echo "land_test: no $script"; exit 1; }
missing=""
command -v jq > /dev/null || missing="jq"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@example.invalid GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@example.invalid
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null GIT_TERMINAL_PROMPT=0
git init -q -b main "$T/probe" && git -C "$T/probe" commit -q --allow-empty -m probe \
  && git -C "$T/probe" merge-tree --write-tree HEAD HEAD > /dev/null 2>&1 || missing="${missing:+$missing and }git 2.38"
if [ -n "$missing" ]; then
  # On CI's machine both are there, and their absence is a failure, not a pass.
  echo "land_test: no $missing here: nothing was asked"; [ -z "${CI:-}" ]; exit $?
fi

mkdir -p "$T/bin"
cat > "$T/bin/gh" <<'GH'
#!/bin/bash
# The made-up GitHub of land_test.sh. In $WORLD: prN.json (state, headRefOid,
# title, isDraft, mergeStateStatus of #N), checks.SHA (the check runs of a
# commit, as the API lists them), origin.git, refuse (numbers whose merge is
# refused although it said they could be merged), merged (what it merged, in
# order), calls (what it was asked).
W="$WORLD"; echo "$*" >> "$W/calls"
jqf=""; a=()
while [ $# -gt 0 ]; do
  case "$1" in
    --jq) jqf="$2"; shift 2 ;;
    *) a+=("$1"); shift ;;
  esac
done
say() { if [ -n "$jqf" ]; then jq -r "$jqf"; else cat; fi; }
case "${a[0]:-} ${a[1]:-}" in
  "pr view") say < "$W/pr${a[2]}.json" ;;
  "pr comment") echo "https://github.invalid/pull/${a[2]}#issuecomment-1" ;;
  "pr merge")
    n="${a[2]}"; sha=""; body=""
    for ((i = 3; i < ${#a[@]}; i++)); do
      case "${a[i]}" in --match-head-commit) sha="${a[i+1]}" ;; --body-file) body="${a[i+1]}" ;; esac
    done
    state="$(jq -r .state "$W/pr$n.json")"; head="$(jq -r .headRefOid "$W/pr$n.json")"
    [ "$state" = OPEN ] || { echo "Pull request #$n is not open" >&2; exit 1; }
    [ "$sha" = "$head" ] || { echo "Head branch was modified. Review and try the merge again." >&2; exit 1; }
    [ "$(jq -r .isDraft "$W/pr$n.json")" = false ] || { echo "Pull Request is still a draft" >&2; exit 1; }
    if [ "$(jq -r .mergeStateStatus "$W/pr$n.json")" = BLOCKED ] || grep -qx "$n" "$W/refuse" 2>/dev/null; then
      echo "Pull request #$n is not mergeable: the base branch policy prohibits the merge." >&2; exit 1
    fi
    out="$(git -C "$W/origin.git" merge-tree --write-tree refs/heads/main "$head")" \
      || { echo "Pull request #$n is not mergeable: the merge commit cannot be cleanly created." >&2; exit 1; }
    new="$(git -C "$W/origin.git" commit-tree "$(echo "$out" | head -1)" -p refs/heads/main -p "$head" -F "$body")" || exit 1
    git -C "$W/origin.git" update-ref refs/heads/main "$new"
    jq '.state = "MERGED"' "$W/pr$n.json" > "$W/pr$n.new" && mv "$W/pr$n.new" "$W/pr$n.json"
    echo "$n" >> "$W/merged" ;;
  "api "*)
    case "${a[1]}" in
      */commits/*/check-runs*)
        sha="$(echo "${a[1]}" | sed -E 's#.*/commits/([0-9a-f]+)/check-runs.*#\1#')"
        if [ -f "$W/checks.$sha" ]; then cat "$W/checks.$sha"; else echo '{"check_runs":[]}'; fi | say ;;
      *) echo "gh (made up): no answer for api ${a[1]}" >&2; exit 1 ;;
    esac ;;
  *) echo "gh (made up): no answer for ${a[*]}" >&2; exit 1 ;;
esac
GH
chmod +x "$T/bin/gh"
: > "$T/check_docs.py"   # `takeable` asks check_docs.py what a merge takes out of AGENTS.md: nothing, here
export PATH="$T/bin:$PATH" CHECK_DOCS="$T/check_docs.py" CHECK_PR_BOARD=1

checks() {  # checks SHA whole|short|going|failed: the check runs GitHub lists for a commit
  local runs="" id=0 i
  job() { id=$((id + 1)); runs="$runs{\"id\":$id,\"name\":\"job $id\",\"status\":\"$1\",\"conclusion\":$2},"; }
  case "$2" in
    whole)  for i in $(seq 18); do job completed '"success"'; done ;;
    short)  for i in $(seq 7); do job completed '"success"'; done; for i in $(seq 11); do job completed '"skipped"'; done ;;
    going)  for i in $(seq 5); do job in_progress null; done; for i in $(seq 2); do job queued null; done ;;
    failed) job completed '"failure"'; for i in $(seq 6); do job completed '"success"'; done ;;
  esac
  echo "{\"check_runs\":[${runs%,}]}" > "$W/checks.$1"
}
pr() {  # pr N FIELD JSON: what GitHub says of #N from now on (`pr 8 isDraft true`, `pr 8 title '"WIP: eight"'`)
  jq ".$2 = $3" "$W/pr$1.json" > "$W/pr$1.new" && mv "$W/pr$1.new" "$W/pr$1.json"
}
push_to() {  # push_to N [FILE]: #N's agent pushes one more commit (an empty one with no FILE); its short run passes
  git -C "$W/work" checkout -q "pr$1"
  if [ -n "${2:-}" ]; then echo more > "$W/work/$2"; git -C "$W/work" add -A; fi
  git -C "$W/work" commit -q --allow-empty -m "one more of #$1"
  git -C "$W/work" push -q "$W/origin.git" "pr$1:refs/pull/$1/head"
  pr "$1" headRefOid "\"$(git -C "$W/work" rev-parse HEAD)\""; checks "$(git -C "$W/work" rev-parse HEAD)" short
}
worlds=0
world() {  # a new world: main, #7 and #8 finished and green, and a lot of both whose run has passed
  local n head
  worlds=$((worlds + 1)); W="$T/w$worlds"; mkdir -p "$W/state"; export WORLD="$W" WATCH_STATE="$W/state"
  : > "$W/merged"; : > "$W/calls"
  git init -q --bare -b main "$W/origin.git"; git init -q -b main "$W/work"
  mkdir "$W/work/src"; echo base > "$W/work/src/base.txt"
  git -C "$W/work" add -A; git -C "$W/work" commit -q -m "main"; git -C "$W/work" push -q "$W/origin.git" main
  for n in 7 8; do
    git -C "$W/work" checkout -q -b "pr$n" main; echo "$n" > "$W/work/src/$n.txt"
    git -C "$W/work" add -A; git -C "$W/work" commit -q -m "the change of #$n"
    git -C "$W/work" push -q "$W/origin.git" "pr$n:refs/pull/$n/head"
    head="$(git -C "$W/work" rev-parse HEAD)"
    printf '{"number":%s,"state":"OPEN","headRefOid":"%s","title":"The change of #%s","isDraft":false,"mergeStateStatus":"CLEAN"}\n' "$n" "$head" "$n" > "$W/pr$n.json"
    checks "$head" short
  done
  git clone -q "$W/origin.git" "$W/clone"; git -C "$W/clone" checkout -q -b watch
  MAIN0="$(git -C "$W/origin.git" rev-parse 'refs/heads/main^{tree}')"
  built="$(cd "$W/clone" && "$script" build 7 8 2>&1)" || { echo "  [FAIL] world $worlds: \`build 7 8\` said: $built"; failed=$((failed + 1)); return 1; }
  LOT="$(cat "$W/state/lot.head")"; checks "$LOT" whole
}
failed=0; cases=0
try() {  # try "what the case is" STATUS "words in what it printed" "merged, in order" lot|before|another [last]
  local out rc merged tree now
  out="$(cd "$W/clone" && "$script" land ${6:-} 2>&1)"; rc=$?
  merged="$(tr '\n' ' ' < "$W/merged" 2>/dev/null | sed 's/ $//')"
  now="$(git -C "$W/origin.git" rev-parse 'refs/heads/main^{tree}')"
  if [ "$now" = "$(git -C "$W/clone" rev-parse "$(cat "$W/state/lot.asis")^{tree}")" ]; then tree=lot
  elif [ "$now" = "$MAIN0" ]; then tree=before; else tree=another; fi
  cases=$((cases + 1))
  if [ "$rc" = "$2" ] && [[ "$out" == *"$3"* ]] && [ "$merged" = "$4" ] && [ "$tree" = "$5" ]; then echo "  [ok  ] $1"
  else
    echo "  [FAIL] $1"; echo "         wanted status $2, merged '$4', main's tree: $5, and the words '$3'"
    echo "         got    status $rc, merged '$merged', main's tree: $tree, and:"; echo "$out" | sed 's/^/           /'
    failed=$((failed + 1))
  fi
}
marks() {  # which mark each merge on main carries, oldest first: "skip" ([skip ci]), "objects" ([objects only]), "none"
  git -C "$W/origin.git" log --first-parent --reverse --format='%H' refs/heads/main | tail -n +2 | while read -r c; do
    m="$(git -C "$W/origin.git" log -1 --format=%B "$c")"
    case "$m" in *"[skip ci]"*) printf 'skip ' ;; *"[objects only]"*) printf 'objects ' ;; *) printf 'none ' ;; esac
  done
}
same() {  # same "what the case is" "got" "wanted"
  cases=$((cases + 1))
  if [ "$2" = "$3" ]; then echo "  [ok  ] $1"; else echo "  [FAIL] $1"; echo "         wanted '$3'"; echo "         got    '$2'"; failed=$((failed + 1)); fi
}

world && try "both green, the lot's run passed: both merged in the lot's order, and main has the tree that run passed on" 0 "its tree is the lot's" "7 8" lot
same "...one CI run on main for the lot: the first merge carries the mark that skips CI, the last runs the two jobs that save the objects" "$(marks)" "skip objects "
world && try "\`land last\`, the last lot of a watch: both merged" 0 "its tree is the lot's" "7 8" lot last
same "...and no run on main for either: the merge of the watch's own pull request follows, and runs it" "$(marks)" "skip skip "

world && checks "$LOT" going
try "the lot's run is still going: nothing merged" 1 "STOP: the lot is not READY" "" before
world && checks "$LOT" failed
try "a job of the lot's run failed: nothing merged" 1 "STOP: the lot is not READY" "" before
world && git -C "$W/work" checkout -q main && echo new > "$W/work/src/new.txt" && git -C "$W/work" add -A \
  && git -C "$W/work" commit -q -m "somebody's merge" && git -C "$W/work" push -q "$W/origin.git" main
try "main moved since the lot was built: nothing merged" 1 "MAIN MOVED" "" another

# Under the rule on main. #8 was taken while its own run was going, the
# lot's run has passed, and #8's has not yet: GitHub says BLOCKED of it.
world && checks "$(jq -r .headRefOid "$W/pr8.json")" going && pr 8 mergeStateStatus '"BLOCKED"'
try "a member's own run is still going under the rule (BLOCKED): nothing merged, the one before it neither" 6 \
    "STOP, nothing merged: #8: GitHub reports BLOCKED: the rule on main wants a passed run on its head" "" before
same "...and GitHub was not asked to merge anything" "$(grep -c '^pr merge' "$W/calls")" "0"
checks "$(jq -r .headRefOid "$W/pr8.json")" short; pr 8 mergeStateStatus '"CLEAN"'
try "...and when that run has passed, the same command lands both" 0 "its tree is the lot's" "7 8" lot
world && checks "$(jq -r .headRefOid "$W/pr8.json")" failed
try "a member's own run failed after it was taken: nothing merged, and the lot is to be built again without it" 1 "#8's own run failed" "" before
world && rm "$W/checks.$(jq -r .headRefOid "$W/pr8.json")" && pr 8 mergeStateStatus '"BLOCKED"'
try "a member with no run on its head at all: nothing merged" 3 "STOP, nothing merged: #8 is not green" "" before

# A member that is not what the lot holds any more.
world && pr 8 isDraft true
try "a member was made a draft again (GitHub says CLEAN of it, and would refuse its merge after the first): nothing merged" 1 \
    "lot: #8 is 'OPEN $(jq -r '.headRefOid[0:7]' "$W/pr8.json") WIP', the lot holds it finished" "" before
world && pr 7 title '"WIP: the change of #7"'
try "a member has \`WIP:\` in its title again (GitHub would merge it): nothing merged" 1 "is not OPEN and finished" "" before
world && pr 7 state '"MERGED"'
try "a member was merged by somebody else: nothing more merged" 1 "lot: #7 is 'MERGED" "" before
world && push_to 8 src/more.txt
try "a member's agent pushed a change under the lot: nothing merged" 1 "another tree than the lot's" "" before
world && push_to 8
try "a member's head moved and the tree did not (an empty commit; main merged in by git alone): the lot goes on" 0 "its tree is the lot's" "7 8" lot
same "...and what is merged is the head as it is now" "$(git -C "$W/origin.git" rev-parse 'refs/heads/main^2')" "$(jq -r .headRefOid "$W/pr8.json")"

# What asking every member first is there to prevent, when it happens all
# the same: GitHub said yes of both and refuses the second merge.
world && echo 8 > "$W/refuse"
try "GitHub refuses the second merge after it said yes: it stops there and says so; main has #7 alone, a tree no run passed on" 4 \
    "STOP: gh pr merge 8 failed" "7" another

echo "$((cases - failed))/$cases pass"
[ "$failed" -eq 0 ]
