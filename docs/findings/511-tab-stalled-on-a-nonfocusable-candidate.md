# Finding 511 — Tab stalled on a nonfocusable candidate

The shell's Tab list included legacy click targets and disabled buttons. Its
walk chose them although DOM focus refused them, leaving focus unchanged and
repeating the same failed choice. A candidate that became detached, disabled
or inert after scanning had the same problem.

Phase 511 checks current DOM focusability and inertness at each candidate,
while keeping the candidate list and pointer click dispatch. `make test` passes
549 engine suites and 114 cross-origin checks; the new regression fails on the
old implementation and covers both directions, live state, modal inertness,
focus events, an all-invalid list and click dispatch.

The list may still include non-stops, and full visibility, shadow-tree and
sequential-focus semantics remain open (see the phase write-up).
