# Finding 522 — the unhandled-rejection report rooted what it reported

The GC audit row's last clause said a generator aborted mid-walk leaves its
interpreter's cells reachable. Measured twice, that clause is false — and
finding out *why* a measurement that reproducibly read 38,049 instead of 498
was false about its own suspect is most of this finding.

## The suspect was cleared by running it

The shape: `Array.from` over a generator, a mapper that throws on the first
value, the generator's `return` patched to watch the close. Run against a
fresh interpreter that is then destroyed, a collection afterwards reads what
it read before. Every cell comes back: the loop's close path, the parked
frame, the teardown's `terminate()` and the thread join behave exactly as
Phases 363 and 415 left them. The defect the row described is not in the
tree, if it ever was; the row's own numbers were recorded while other rows'
fixes were still landing, and the clause outlived the bug it was written
against. It is now pinned so it cannot come back silently: the suite runs the
shape and asserts the residue is flat.

## What rooted the cells instead

Not a generator. A *rejected promise*. The unhandled-rejection report is
posted as a task that captured `shared_ptr<PromiseData>` and rooted the
result — sound for one task turn, which is the window HTML gives a late
`catch`, and a pin for ever in a process whose loop never ticks again:

| scenario (one interpreter, then destroyed) | residue |
|---|---|
| one rejected promise | ~50 cells |
| 2,000 rejected promises | 2,049 cells |
| 100 self-`await`ing coroutines abandoned mid-`await` | 151 cells |
| a generator aborted mid-walk, `return` patched | 0 cells |

Every row of that table but the last was the same single task entry —
`tick()` ran once with the interpreter still alive, and all of them read zero
afterwards. The engine's own suite harness is one process for 560 suites, so
any suite that rejects promises taxed every later GC measurement; a shell
navigating pages that reject taxed its whole session. The count the GC audit
watches (`alive_cells` before the automatic-GC tests) is exactly where an
unexplained few thousand cells shows up as a failed assertion "by one
collection".

## The fix, and the judgement inside it

The task now holds `std::weak_ptr` and roots nothing. That is not merely
smaller — it answers a question the old capture got wrong. A rejection report
is addressed to the program that can still reach the promise. When the weak
state is empty, that program is gone; reporting to it would be counting a
rejection nothing can observe, and keeping the promise alive only so the
report has something to say was the leak. The report still fires at the same
moment for every promise a script could still attach a handler to, which is
the only case the window existed for.

What made this hard to see was not the machinery but the shape of the claim:
the row described a *generator* defect with numbers attached, and the numbers
were real. The lesson the row already carried — measure residue at two sizes,
never at the bottom of a loop — deserves a sibling: **when a measured defect
outlives its proposed mechanism, re-run the mechanism against the measurement
before fixing anything.** The 2,049 came from promises that never appeared in
the clause's own repro.
