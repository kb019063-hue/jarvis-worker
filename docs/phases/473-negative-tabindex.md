# Phase 473 — Negative tabindex is not a Tab stop

Task: sequential focus navigation; finding 473.

## What was wrong

The shell's focus walk treated `tabindex=-1` like zero. Tab and Shift+Tab could
land on an element that a page meant to focus only by script. This was still
true after Phase 461 ordered positive values.

## What changed

The shell still keeps negative entries in its candidate list: a script can
focus one, and the shell needs its position when the next key moves away. A
bounded forward or reverse walk skips entries whose current `tabindex` parses
as a negative integer, using the same parser as the reflected `tabIndex`.
Missing, invalid and zero values retain their previous behavior. A sign change
is read on the next key, without a layout rebuild. If every candidate is
negative the walk terminates without moving focus or emitting events. Focus
reconciliation now also consults the document when the previous index was -1,
so script focus after such a walk is not lost.

## Verification

`make test` passes 522 engine suites and 114 cross-origin checks. The new
`tests/suites/473-negative-tabindex.cpp` fails its forward-navigation assertion
against the original scan and passes with the change. It tests both directions,
wraparound, positive values, invalid values and `-0`, focus/blur events, script
focus and the next stop, live sign changes, and an all-negative page.

## What remains open

The candidate list intentionally still contains negative entries as navigation
anchors; it is not a list of only Tab stops. This is not the full HTML focus
navigation algorithm. Legacy click candidates may not be DOM-focusable;
visibility, shadow-tree focus scopes, dynamic changes that alter priority or
add/remove candidates without a layout rebuild, and exact navigation starting
points for elements not in the list remain open. The test exercises
`focus_prev()`; actual Shift+Tab is decoded by the terminal loop, not by
`handle_key`.
