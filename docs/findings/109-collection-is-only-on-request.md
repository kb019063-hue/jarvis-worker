# 109. Collection happens only when asked, and until Phase 103 nothing measured it

**Status: partly fixed.** The oracle exists (`make verify-gc-stress`); the
automatic policy does not.

## What was wrong

Two things, and the second is why the first survived.

1. **Memory is reclaimed only on request.** `Heap::collect_garbage` runs when a
   script calls `gc()`, when the shell's heap view is opened, or when a test
   calls it. A page that runs for a long time grows until one of those happens,
   which for a page nobody is watching is never. This is Tier 1 item 5 in
   `ROADMAP.md` and it is still open.

2. **No test had ever collected without being asked.** The suites that touched
   the collector called it themselves, at a moment they chose, with a root set
   they passed in by hand — so they measured it with the interpreter idle and
   the stack empty. The question a collector has to answer, *does a collection
   free something still alive*, was never put to it under the conditions a
   program creates. The same shape of gap that `README.md` records for the DOM
   scoreboard: "It was 100% before it was right."

## What changed

The oracle, in full in
[`docs/phases/103-collection-at-a-safe-point.md`](../phases/103-collection-at-a-safe-point.md):
a seeded allocation count raises a request, the interpreter serves it at the
statement boundary and the bytecode VM at the instruction boundary, and a
program must print exactly what it prints when nothing is collected. The
expectation is the program's own un-stressed output — the one oracle in this
repository that is a property rather than another implementation — and it is
backed by a negative control that must be seen to catch three known-bad cases
before any green run counts.

What it found: nothing. 158,355 collections across 1,172 JavaScript programs
and 11,176 across the 678 DOM pages, at intervals down to 1, and no case
printed anything different. That is a result about the collector's root
counting, and it is worth having in the same way that "the parser agrees with
html5lib on 1,972 of 1,973" is: it converts a belief into a measurement.

## What is still open

- **Scheduling.** When to collect — a threshold, a growth rate, a time slice —
  is untouched. Nothing calls the collector automatically. This finding stays
  open for it, and the phase above is the thing that makes it safe to try:
  changing *when* collection happens is exactly the variable now held fixed.
- **Leaks.** The oracle finds premature frees, not retained garbage. Known
  leaks (`o.b = f.bind(o)`, what a pending timer holds) are unchanged and
  invisible to it. The missing half is a growth bound per workload — the shape
  `make bench-styles` uses for peak RSS — not a stress mode.
- **Large live sets.** Cases slower than 2 s un-stressed are excluded, because
  a collection is O(heap) and stressing them measures the interval. An
  incremental or generational collector would remove the exclusion.
- **Threads and the event loop.** A request raised while a worker agent runs is
  served by that agent's next statement; the event loop has no boundary of its
  own, so a program whose allocation happens entirely inside one native
  callback defers its collection to the next statement. Sound, but it means
  callbacks are under-collected.
