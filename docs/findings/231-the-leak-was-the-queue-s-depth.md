# Finding 231 — the leak was the queue's depth

**Severity:** documentation, not code. A row told every agent that promise
reactions leaked two cells an iteration and that chained reactions leaked four.
Both numbers are real. Neither is a leak.

**Found by:** taking the row at its word and probing it before writing any
code. `Promise.resolve(1).then(f)` in a loop showed exactly the four cells an
iteration the row promised, so the row looked confirmed. It stayed confirmed
until the same loop was measured a second time, after the microtask queue had
been given a chance to drain.

## What the row said

> **Still open, same mechanism:** a promise reaction —
> `Promise.resolve(1).then(f)` in a loop is still two cells an iteration, and
> `then(f).then(g)` four (a settled `new Promise` with no handler is clean, so
> it is the reaction list, not the promise); what a pending timer holds; and
> the rest of the audit of native-function captures and event-loop safe points

The parenthetical is the tell. The row had already reasoned correctly — the
promise is clean, so it is the reaction list — and then drew the wrong
conclusion from it. A reaction list that has not run yet is a **queue**.

## The measurement that made it look true

```
var base = gc().alive_cells;
for (var i = 0; i < 2000; i++) Promise.resolve(1).then(function f(v) { return v; });
gc();
gc().alive_cells - base          // 8000, i.e. four an iteration
```

Eight thousand is four an iteration, and the row's two/one ratio for
`then(f)` against `then(f).then(g)` shows up in the same way. It is a faithful
reading of the heap.

It is not a reading of *garbage*. `EventLoop::collect_gc_roots` walks the
microtask queue, so every one of those reactions is reachable: not by accident,
not conservatively, but because it is a callback that has been scheduled and
has not been called. Calling it garbage asserts that the collector should free
work that is about to run.

## The measurement that made it false

One task turn later, the queue has drained and the same loop is flat:

```
2000 iterations, count taken one turn later  ->  +1 cell
```

The whole 8,000-cell "leak" is released. A leak cannot do that; a queue can.

## Why one number cannot tell them apart

This is the part worth keeping. A leak and a backlog both look like a positive
number that grows when the loop grows, and a single measurement of a single
loop size cannot distinguish them:

| | at 200 iterations | at 2,000 iterations |
| --- | --- | --- |
| a real leak of 1 cell an iteration | 200 | 2,000 |
| a backlog that drains each batch | ~0 | ~0 |
| **the same two, both measured mid-loop** | **both positive** | **both positive** |

Size is what separates them. So the checker built for this measures every case
at two sizes and asserts the *difference* is flat, not the value:

```
injected-leak:    drift=1800      <- caught
injected-backlog: drift=0
promise-reaction: drift=0         <- and it is the same shape as the row's
```

The first run of the two-size checker reported the row's case at 200 and at
2,000 and got 0 both times. The row's numbers only appear when the queue is
counted before it drains, which no correct leak test does.

## The other two claims

* **Pending timers.** `timer-fired`, `timer-cleared` and `timer-pending-one`
  are all flat. A pending timer rooting its callback is the timer working.
* **Native-function captures.** 181 `make_native_function` sites; every
  by-value `JsValue` capture goes through `set_payload_captured` except two,
  `map_inst` and `set_inst` in `src/js/VM.cpp`, where the captured instance and
  the accessor that captures it are built together and share a lifetime by
  construction. Event-loop safe points are covered by the same residue check,
  since every case runs on the real loop.

## What this says about the row

The row was written from a mid-loop measurement and never re-run after a drain.
That is the whole of it — there was no regression, no half-finished fix, and no
code to change. Phase 168's `bind` fix, which the same row describes, is
unaffected and still real: `bind-null-target`, `bind-object-target` and
`bind-with-arguments` are three of the fifteen flat cases.

## Guarding against it

* `tools/conformance/verify_gc_residue.py` — fifteen shapes at two sizes, in
  CI. The checker was verified to catch a genuine leak before it was trusted.
* `tests/test_suite.cpp::a_promise_reaction_is_a_queue_not_a_leak` — holds the
  cells mid-loop, requires them gone one turn later; verified to fail when the
  loop is made to leak.
