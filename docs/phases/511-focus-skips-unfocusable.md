# Phase 511 — Tab skips candidates the DOM will not focus

Task: shell sequential focus navigation; finding 511.

## What was wrong

The shell appended legacy click targets (such as a `div` with class `btn`) to
its candidate list. Its Tab walk selected them even though the DOM focus path
refused them. From the last real button, Tab left `document.activeElement`
unchanged; the next Tab reconciled to that button and selected the same
unfocusable candidate again. Disabled buttons were also appended as legacy
candidates. A candidate removed, disabled, or made inert after a scan could
likewise be selected even though DOM focus would reject it.

## What changed

At each step the bounded forward/reverse walk now asks the same `is_focusable`
predicate and document inertness check that DOM focus uses, in addition to the
negative-tabindex rule. It reads current state, so it skips a disabled or
removed candidate and one made inert by a modal dialog even without a rebuild.
If nothing qualifies it terminates without inventing a focus. The legacy list
and pointer click dispatch remain intact; their entries are no longer treated
as Tab stops merely because they are clickable.

## Verification

`make test` passes 549 engine suites and 114 cross-origin checks. The new
`tests/suites/511-focus-skips-unfocusable.cpp` fails on the original tree at
its wraparound assertion and passes after the change. It tests both directions,
focus/blur events, disabled, removed and re-enabled candidates, an all-invalid
list, modal inertness and a legacy click handler. An older
`browser_shell_navigation_and_hit_testing` assertion treated an unfocusable
`div` as a keyboard stop; it now checks that Tab leaves no focus while direct
click dispatch still works.

## What remains open

This is not the whole HTML focus-navigation algorithm. The retained candidate
list can still include non-stops; its membership and positive-priority order
need a layout rebuild when candidates are added or attributes change. DOM
`is_focusable` itself does not cover all visibility and shadow-tree rules.
The shell's existing initial index and terminal Shift+Tab input handling are
unchanged.
