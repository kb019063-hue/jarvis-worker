#!/usr/bin/env bash
# The shell of a CI job's steps, when the job says
#
#     defaults:
#       run:
#         shell: bash tools/ci/logged.sh {0}
#
# It runs the step's script as CI's own default does (`bash -e`), and keeps
# what the script printed in a file when the script failed. The job's last
# step (`if: failure()`, with `shell: bash` so that it is not run through
# this) gives that file to tools/ci/annotate_failure.py, which says what
# failed as annotations on the job.
#
# A job's log cannot be fetched from every place a change is written in; its
# annotations always can:
#
#     gh api repos/OWNER/REPO/check-runs/JOB_ID/annotations --jq '.[].message'
#
# Without this, a failed step of "Build & unit tests" said `Process
# completed with exit code 2` and nothing else. On 24 November that is all
# #364's run said when an old suite failed an assertion; which suite, and
# which line, was found by building the pull request again somewhere else.
#
# The steps stay as they are written: nothing in them has to remember to
# `tee`. The file of a step that passed is removed, so the newest file left
# is the failed step's, also when a step with `if: always()` ran after it.
#
#     bash tools/ci/logged.sh --self-test
set -u

run_logged() {  # run_logged SCRIPT -> the script's exit status
  local dir="${RUNNER_TEMP:-${TMPDIR:-/tmp}}" log status
  log="$(mktemp "$dir/part-step-XXXXXX.log" 2>/dev/null)" || { bash -e "$1"; return $?; }
  set -o pipefail
  bash -e "$1" 2>&1 | tee "$log"
  status="${PIPESTATUS[0]}"
  [ "$status" -eq 0 ] && rm -f "$log"
  return "$status"
}

self_test() {
  local dir failed=0 n=0 got script
  dir="$(mktemp -d)"
  check() {  # check WHAT WANT GOT
    n=$((n + 1))
    if [ "$2" != "$3" ]; then failed=$((failed + 1)); echo "  FAIL  $1: got '$3', expected '$2'"; fi
  }
  script="$dir/step.sh"
  logs() { ls "$dir"/part-step-*.log 2>/dev/null | wc -l | tr -d ' '; }

  printf 'echo one\necho two >&2\n' > "$script"
  got="$(RUNNER_TEMP="$dir" bash "$0" "$script" 2>&1)"; check "a step that passes: status" 0 $?
  check "a step that passes: what it printed is printed, both streams" "one two" "$(echo $got)"
  check "a step that passes leaves no file" 0 "$(logs)"

  printf 'echo before\nfalse\necho after\n' > "$script"
  got="$(RUNNER_TEMP="$dir" bash "$0" "$script" 2>&1)"; check "a failing command ends the step, as bash -e does: status" 1 $?
  check "a failing command ends the step: nothing after it ran" "before" "$got"
  check "a step that fails leaves its file" 1 "$(logs)"
  check "the file holds what the step printed" "before" "$(cat "$dir"/part-step-*.log)"
  rm -f "$dir"/part-step-*.log

  printf 'echo to the end; exit 7\n' > "$script"
  RUNNER_TEMP="$dir" bash "$0" "$script" > /dev/null 2>&1; check "the step's own exit status is the status" 7 $?
  rm -f "$dir"/part-step-*.log

  printf 'set -o pipefail\nfalse | cat\necho not reached\n' > "$script"
  got="$(RUNNER_TEMP="$dir" bash "$0" "$script" 2>&1)"; check "a step's own pipefail still fails it" 1 $?
  check "a step's own pipefail: nothing after it ran" "" "$got"
  rm -f "$dir"/part-step-*.log

  printf 'false | cat\necho reached\n' > "$script"
  got="$(RUNNER_TEMP="$dir" bash "$0" "$script" 2>&1)"; check "without pipefail in the step a pipe's last command decides, as before" 0 $?
  check "without pipefail in the step: it went on" "reached" "$got"

  printf 'kill -ABRT $$\n' > "$script"
  RUNNER_TEMP="$dir" bash "$0" "$script" > /dev/null 2>&1; check "a step killed by a signal fails with 128 + the signal" 134 $?
  rm -f "$dir"/part-step-*.log

  printf 'echo first\nfalse\n' > "$script"
  RUNNER_TEMP="$dir" bash "$0" "$script" > /dev/null 2>&1
  printf 'echo a later step that passes\n' > "$script"
  RUNNER_TEMP="$dir" bash "$0" "$script" > /dev/null 2>&1
  check "after a failed step and a later one that passed, the newest file is the failed step's" "first" "$(cat "$(ls -t "$dir"/part-*.log | head -n 1)")"

  printf 'echo no place for a file\n' > "$script"
  got="$(RUNNER_TEMP="$dir/none" TMPDIR="$dir/none" bash "$0" "$script" 2>&1)"; check "with nowhere to keep a file the step still runs: status" 0 $?
  check "with nowhere to keep a file the step still runs: output" "no place for a file" "$got"

  rm -rf "$dir"
  echo "logged.sh --self-test: $((n - failed)) of $n passed"
  [ "$failed" -eq 0 ]
}

case "${1:-}" in
  --self-test) self_test ;;
  "") echo "usage: bash tools/ci/logged.sh STEP_SCRIPT | --self-test" >&2; exit 2 ;;
  *) run_logged "$1" ;;
esac
