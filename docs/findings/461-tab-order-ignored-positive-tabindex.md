# Finding 461 — Positive tabindex did not affect Tab order

The shell's focus ring visited elements in document order even when positive
`tabindex` values required a different order. Phase 461 sorts the focusable
stops by positive value, stably within a value, before the zero/default group;
the separate legacy candidates are still appended.

`make test` passes 506 engine suites and 114 cross-origin checks. The new
suite fails against the original scan and checks forward and reverse focus,
events, parsing and a reordered focused element after layout.

Negative tabindex exclusion and the rest of full sequential focus navigation
remain open, as described in the phase write-up.
