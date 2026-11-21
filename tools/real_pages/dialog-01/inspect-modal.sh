#!/bin/sh
# Supplementary observations on H3, NOT inputs to the frozen score.
set -eu
if [ "$#" != 3 ]; then echo 'usage: inspect-modal.sh DRIVER CAPTURE_ROOT OUTPUT_DIRECTORY' >&2; exit 2; fi
mkdir -p "$3"
page=$(python3 -c 'import json,sys; print(next(p["path"] for p in json.load(open(sys.argv[1]))["pages"] if p["id"] == "H3"))' "$(dirname "$0")/../holdout-01/manifest.json")
# Capture the physical hit at the opener, rather than synthesizing a click
# with JS. Use a fresh process for Escape in case the outside click closes it.
for action in outside escape; do
    {
        printf 'click\t#updateDetails\n'
        printf 'read\tdocument.activeElement.id\n'
        if [ "$action" = outside ]; then printf 'click\t#updateDetails\n'; else printf 'key\tEscape\n'; fi
        printf "read\tdocument.getElementById('favDialog').open\nread\tdocument.activeElement.id\n"
    } | "$1" "$2/H3" "$page" "$3/$action-" > "$3/$action.log" 2>&1
    cat "$3/$action.log"
done
