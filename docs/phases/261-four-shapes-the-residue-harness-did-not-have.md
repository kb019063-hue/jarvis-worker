# Phase 261 — four shapes the residue harness did not have

## The row

`AGENTS.md`, "GC leak audit & native-capture tracing". Its open items were
three unmeasured shapes and the `holders > 3` heuristic in
`Promise::trace_extra`. Collision check first
(`tools/ci/check_pr.py claim 324`): the row was free, 8 of 42 rows were, and
this one does not touch `src/js/Dom*Bindings.cpp`.

## What was done

`tools/conformance/verify_gc_residue.py` certified fifteen churn shapes flat.
All fifteen were promises, timers, `bind` or listeners. **No shape made an
iterator.** Four were added:

* `generator-never-advanced`
* `generator-advanced-once`
* `generator-finished`
* `generator-holds-value`

All four fail, at 7–8 cells an iteration. See finding 265.

## The mechanism the harness needed

A residue harness that only knows how to say "flat" or "drifting" cannot land a
check that fails, so `Case` grew a `known` field:

* a case with a `known` reason that drifts prints `KNOWN` and is not a failure;
* a case with a `known` reason that comes back **flat** prints `FIXED` and
  fails the run, so the reason cannot outlive the defect.

Both branches were exercised: the four generator cases report `KNOWN` at the
numbers in finding 265, and forcing one to a flat step reports
`FIXED … the case is flat but still carries a known reason` and exits 1.

## Result

    flat: 15 of 19 cases, 4 known

Exit 0. Before this phase it was `flat: 15 of 15 cases` — the corpus was
smaller than it looked.

## What this phase did not do

The defect is not fixed. The two candidate fixes are in finding 265 and both
need a full cycle; the frame is shared with async functions, which were not
measured. The row's `holders > 3` item is untouched and still open.

## Checks

* `python3 tools/conformance/verify_gc_residue.py` — exit 0, 15 of 19 flat,
  4 known.
* Node v22.22.3 as oracle for the thread ceiling: 20,000 generators vs the
  engine's 15,730.
* The control shapes (object literal, promise, generator declaration only) are
  flat in the same binary, so the harness is not measuring its own scaffolding.
