# Phase 227 — the GC row counted a queue and called it garbage

The AGENTS.md row for the garbage collector's leak audit named three things as
open. One of them was specific enough to test:

> a promise reaction — `Promise.resolve(1).then(f)` in a loop is still two
> cells an iteration, and `then(f).then(g)` four (a settled `new Promise` with
> no handler is clean, so it is the reaction list, not the promise)

Every one of those numbers reproduces. They are also all wrong, in the same
way, and the engine has no leak in any of them.

## What the row measured

`Promise.resolve(1).then(f)` at the bottom of a loop, followed by `gc()`,
really does show about four `alive_cells` for every iteration: one cell for the
promise `Promise.resolve` makes and one for the reaction record that `then`
queues, and `Promise.resolve(1)` makes a fresh promise each time it runs.

A queued reaction is not garbage. It is work outstanding, and HTML says a
microtask checkpoint drains the queue before the next task runs. The collector
is right to leave it alone; `EventLoop::collect_gc_roots` roots the microtask
queue, so the cells are reachable by definition.

Measured on this tree, 2,000 iterations of `Promise.resolve(1).then(f)`:

| when the count is taken | cells above baseline |
| --- | --- |
| bottom of the loop, queue still full | 8,000 (4 an iteration) |
| one task turn later, after `gc()` | **1** |

Eight thousand is the queue's depth, and it is bounded: the drain is what makes
it zero. A leak estimated from the same loop at two sizes would have to grow
with the loop. This one does not.

## What was checked, and how

The mistake is invisible to one measurement — a leak and a backlog are both a
positive number that grows with the loop. It is visible across two sizes,
because a leak holds everything it ever made while a backlog holds one batch:

* **leak:** residue(10N) − residue(N) ≈ 9 × the per-iteration cost, so 2 cells
  an iteration is about 3,600 extra cells between N=200 and N=2,000;
* **backlog:** both residues are the same, because the queue drained between
  batches either way.

`tools/conformance/verify_gc_residue.py` runs fifteen churn shapes at 200 and
at 2,000 iterations and requires the residue after the queue drains to be the
same at both sizes (within 64 cells). All fifteen are flat, and the drift
between the two sizes is **0 cells for every case**:

```
case                            @200     @2000   drift
bind-null-target                   0         0       0
bind-object-target                 0         0       0
bind-with-arguments                0         0       0
interval-cleared                   0         0       0
listener-pair                      1         1       0
object-literal-control             0         0       0
promise-chain-deep                 0         0       0
promise-handler-captures           0         0       0
promise-reaction-pair              0         0       0
promise-reaction-single            0         0       0
promise-settled-from-timer         0         0       0
promise-settled-no-handler         0         0       0
timer-cleared                      0         0       0
timer-fired                        0         0       0
timer-pending-one                  0         0       0
```

The row's second claim, that a pending timer was suspect, is answered by the
`timer-fired`, `timer-cleared` and `timer-pending-one` cases: a timer that has
not fired roots its callback, which is what a pending timer is for, and both
firing and clearing give the cells back. The row's third claim — "the rest of
the audit of native-function captures and event-loop safe points" — is a static
question, and it is answered by counting the places that could get it wrong:
of 181 `make_native_function` call sites, 171 capture something explicitly, and
every capture of a **`JsValue` by value** is declared through
`set_payload_captured` (5 call sites: `bind`, and four `Fetch` constructors).
The only two uncounted `JsValue` captures in the tree are `map_inst` and
`set_inst` in `src/js/VM.cpp`, which are the `Map`/`Set` instances captured by
their own accessors at construction — the function and the object are built
together and neither ever outlives the other.

## Why the harness is trusted

A residue check that passes everything is not evidence. So the checker was run
against a loop that really does leak — pushing an object into a global array
every iteration — and it failed the way it should:

```
injected-leak:    @200=200   @2000=2000   drift=1800
injected-backlog: @200=0     @2000=0      drift=0
```

1,800 cells of drift is exactly the per-iteration cost times the difference in
sizes; the harness sees a leak of one cell an iteration and rejects it. The
same discrimination is in `tests/test_suite.cpp` as
`a_promise_reaction_is_a_queue_not_a_leak`: it holds the cells mid-loop (which
is the row's measurement) and asserts they are gone one turn later, and it was
checked to fail when the loop leaks (`hoard.push(...)`) and to pass when it
does not.

## What changed

* `tools/conformance/verify_gc_residue.py` — the two-size residue checker, 15
  cases, wired into CI.
* `tests/test_suite.cpp` — `a_promise_reaction_is_a_queue_not_a_leak`.
* `AGENTS.md` — the row's three "still open" claims replaced with what is true:
  reactions are a queue, timers are covered, and the capture audit is done.

No engine code changed. That is the finding: there was nothing to fix.

## What is still open

The row keeps what it got right — the `bind` fix in Phase 168 (finding 173) is
real work and stays described. What remains genuinely unmeasured is narrower
than what the row claimed:

* the collector's `holders > 3` heuristic in `Promise::trace_extra` — it
  decides that a promise with native owners keeps its reactions as roots, which
  can only *retain* more than necessary, never collect something still needed.
  The `promise-settled-from-timer` case covers the path and is flat, but a
  long-lived resolver held by native code for a promise whose reactions have
  already run is reachable by design and is not measured here;
* whether the `holders > 3` guess is the right one at all is a design question
  with no failure behind it, so it is left alone rather than changed
  speculatively.
