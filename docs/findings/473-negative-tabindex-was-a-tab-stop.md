# Finding 473 — Negative tabindex was a Tab stop

The terminal shell walked to focusable elements with negative `tabindex` as if
they were ordinary Tab stops. Phase 473 skips them in both directions while
keeping them as positions for script-driven focus. The walk is bounded even
when no eligible stop exists and reads sign changes at the keypress.

`make test` passes 522 engine suites and 114 cross-origin checks; the new suite
fails against the original implementation and covers navigation, events,
script focus, mutations and an all-negative page.

The list still contains negative positions and legacy click candidates; the
full HTML sequential focus algorithm remains open (see the phase write-up).
