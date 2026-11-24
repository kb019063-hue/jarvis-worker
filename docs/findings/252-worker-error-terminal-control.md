# Finding 252 — a worker error reached the terminal as a command

A page-controlled worker could throw an OSC-shaped string; the default
unhandled worker-error diagnostic wrote ESC, BEL and other controls directly
to stderr. Phase 203's rendered-cell boundary and Phase 212's shell-frame
boundary do not cover it.

Phase 248 changes that diagnostic only: printable ASCII bytes are retained
and all other bytes become `?`. Worker and page ErrorEvents keep the original
message. `make verify-worker-terminal-safety` checks the public CLI: 2/3
checks on the old implementation and 3/3 after the fix. The unit suite
checks U+0000 through U+00FF, exact output and unmodified handled errors.

This is a byte-wise ASCII fallback, not Unicode terminal rendering. Other
output and input paths, notably console, general error reporting and the URL
prompt, remain unaudited and may emit terminal controls.
