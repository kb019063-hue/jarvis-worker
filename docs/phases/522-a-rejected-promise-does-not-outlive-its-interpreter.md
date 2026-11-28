# Phase 522 — a rejected promise does not outlive its interpreter

The GC audit row carried one open clause, from Phase 415: an interpreter whose
generator is aborted mid-walk — a mapper throws through `Array.from` while the
source is a generator, and the generator's `return` has been patched — was
measured leaving its cells reachable after the interpreter was gone. This phase
answers it twice over: the generator shape is innocent, and the measurement had
caught something else.

The suite runs the exact shape the clause described and it leaves nothing: the
walk closes on the mapper's throw, the patched `return` runs once, no `finally`
runs, the frame is parked until the interpreter tears it down, and a collection
after the interpreter is gone reads the same count it read before the suite
began. `alive_cells` inflation of the kind Phase 415 measured cannot come from
that shape.

## What the measurement had caught

A rejected promise nobody handles is reported once, at the end of the current
task — HTML's window for a late `catch`. The report is a task on the
process-wide event loop, and that task captured the promise's state and rooted
its result:

```cpp
post_task([data]() { if (!data->handled) report(data->result); },
          {js_roots({data->result})});
```

The loop outlives every interpreter, and a host that never ticks again — a
unit test, an embedder, a page torn down without settling — never runs the
task, so the capture kept the promise, its result and everything they reach
marked for the life of the process. Two thousand rejected promises out of a
dead interpreter measured **2,049 cells** of residue; one rejection measured
about 50. Nothing script-visible was wrong: the answers were right, the count
was not, and the next GC test in line inherited the residue — which is how a
suite that rejects promises could push `alive_cells` past a threshold
`max(8192, alive)` and fail a later phase's automatic-collection assertion by
one collection, exactly the phenomenon the Phase 415 clause recorded.

The task now holds the promise weakly and roots nothing:

```cpp
post_task([weak = std::weak_ptr<PromiseData>(data)]() {
    if (auto held = weak.lock(); held && !held->handled)
        report(held->result);
});
```

## A report for a promise that is gone says nothing

The one behaviour that changes is deliberately narrow: when the promise is no
longer reachable, the report is skipped. A rejection report exists for the
program that can still see the promise; when the weak state has gone, the
promise has gone, and the report would be about nothing. Every path a page or
a script can observe is unchanged:

- A rejection whose promise is still reachable is reported as before, at the
  same moment (the suite ticks the loop with the interpreter alive and asserts
  the count moved).
- The engine's exit status for a script with an unhandled rejection is
  unchanged: the host runs its loop before it reads the count, so the task has
  always run by then.
- The wording and the terminal-safety filter are untouched — suite 269 asks
  `js_report_unhandled_rejection` directly, and still passes.

## The suite

`tests/suites/522-a-rejected-promise-does-not-outlive-its-interpreter.cpp`
pins four things: the residue across interpreter death stays flat for two
thousand rejected promises (the assertion that reads 2,049 with the old
capture and fails there); the aborted patched generator of the Phase 415
clause leaves nothing; the surviving report still fires for a reachable
promise; and the dead one reports nothing. Under the old capture the first
assertion fails with

```
RESIDUE rejected promises left 2049 cells behind a dead interpreter
```

which is both the red the fix answers and the size of what hung on it.

`make test` 560/560 with the suite in; the promise conformance corpora, the
residue harness (flat 22 of 22) and the GC oracles run unchanged.
