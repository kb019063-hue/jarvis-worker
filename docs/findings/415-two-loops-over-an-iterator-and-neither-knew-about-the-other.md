# Finding 415 — two loops over an iterator, and neither knew about the other

**Severity:** no wrong answer reachable from a script. The defect is *shape*:
two implementations of GetIterator + IteratorStep that could be edited apart,
one of which had already been. What it cost was a guarantee — that every walk
in the engine closes an iterator the same way — and the next divergence would
have been invisible until some suite happened to ask.

**Found by:** the open clause of the row "`Array.from` asks the source once,
and maps as the values arrive": *"that loop is a copy of `iterate_with` in
`src/js/JsOps.cpp` and the two belong together as one exported function"*.

## What it was

`src/js/JsOps.cpp` had `iterate_with`: GetIterator, then IteratorStep in a
loop, with `next` read once and two close paths, `close_iterator` (§7.4.11's
checks) for a loop that stops early, and `close_iterator_quietly` for a loop an
error is already ending. It carried one subtlety the other loop did not: a
`GeneratorAbortException` out of the visit callback means the generator has
already finished, and `return` must not run a second time.

`src/js/BuiltinsArray.cpp` had a near-copy, written in Phase 225 so that
`Array.from` could ask a source for `@@iterator` once. It had the same
`next`-read-once rule and its own hand-written close logic, and it was missing
the generator branch.

## Why nothing saw it

`GeneratorAbortException` never reaches a script: it is thrown by the
interpreter's own yield machinery and caught below the evaluation of the
statement that started the walk. A generator closed by its own abort, and one
closed by the loop's `return`, both leave the observable state Node v22.22.3
leaves — `finally` runs once, a patched `return` is called once, the mapper's
error is the one the script catches. So the drift was real and unobservable,
which is the worst kind: it cannot be caught by a test that only checks
answers.

## What replaced it

One function, one export. `iterate_with_method` is declared in
`include/litebird/js/JsOps.hpp` and wraps the two paths that already existed:

- `js_iterates_as_builtin(iterable)` → `js_iterate`, for an array, a string or
  an arguments object, where there is no protocol to run;
- otherwise `iterate_with(iterable, method, visit)`, with the method the caller
  already has.

The header could not simply declare `iterate_with`: it lives in an unnamed
namespace, and an attempt to declare it in the header made every call in the
engine ambiguous between the declaration and the internal definition. A
differently named wrapper is the answer, and the name says what it takes.

## Two other loops and a retention, found while probing

1. **`Array.fromAsync` walks an async iterator with a third copy** (Phase 412,
   one day earlier). Measured against Node v22.22.3:

   | | Node | this tree |
   |---|---|---|
   | `next` getter reads for a 2-value async iterable | 1 | 3 |
   | `return` calls, mapper throws | 1 | 0 |
   | `return` calls, `next()` rejects | 1 | 0 |
   | `return` calls, walk completes | 0 | 0 |

   The async walk needs its own close path (§27.6.3.8's `AsyncIteratorClose`),
   which this tree does not have at all. Filed against the `Array.fromAsync`
   row.

2. **An aborted generator whose `return` is patched leaves cells reachable**
   beyond the interpreter that aborted it. Measured: with the suite running, a
   `collect_garbage()` immediately before the phase-145 GC tests reports
   `alive_cells = 38,049` where a run without the suite reports 498; the
   phase-145 test then fails its automatic-collection assertion because its
   threshold is `max(8192, alive)` and the VM loop no longer crosses it. Filed
   against the GC leak audit row. It is a retention of the interpreter, not of
   the loop, which is why this phase's suite pins the same close rule with a
   plain source and leaves the generator shape to whoever fixes the retention.

## The lesson, written down

A copy of a loop is not a copy of a *rule*. The three loops here agreed on
every answer a test could ask and disagreed on what happens on the way to it —
and the only reason the disagreement surfaced at all is that consolidating them
put a suite next to the shared code that had to state the rule out loud.

## Numbers

`make test` **471/471**. test262 **15,182 of 15,686, unchanged** (two binaries:
`0 pass in now and not in before; 0 pass in before and not in now`),
js-conformance **1,153/1,158** and WPT **9,570/9,824** unchanged — a
consolidation may not move them, and did not.
