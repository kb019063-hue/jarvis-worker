# Phase 430 — a promise reaction is rooted, not guessed

## The row

`AGENTS.md`, “GC leak audit & native-capture tracing”. The row carried one
open item after Phase 363: **the `holders > 3` heuristic in
`Promise::trace_extra` guessed that a promise with native owners must keep its
reactions as roots.** It could only over-retain: a promise whose resolvers
were still held could not release the settled result, and the decision was
made from `use_count()`, which is not an ownership graph.

## What was done

See [finding 430](../findings/430-a-promise-reaction-is-rooted-not-guessed.md)
for the root-cause breakdown. The short version:

1. `Promise::trace_extra` no longer reads `state.use_count()`. A promise cell
   that is reached marks its result and its handlers unconditionally.
2. A **pending** promise with reactions is kept by the pending-promise
   registry. The registry now holds `weak_ptr<PromiseData>`; its root provider
   pushes the stored result and every handler slot (`on_fulfilled`,
   `on_rejected`, `resolve`, `reject`) plus `keep_alive` while the promise is
   pending and has handlers. `settle()` releases the registry entry before the
   reactions are queued as microtasks.
3. Resolving pairs are explicit. `make_resolving_functions` now makes one
   `ResolverState` shared by `resolve` and `reject`; `PromiseData` keeps weak
   references to those states, and `settle()` calls `release_resolvers()`,
   dropping the strong reference from each pair. A resolver held after
   settlement is inert and does not keep the settled state alive.
4. The regression suite is
   `tests/suites/430-a-promise-reaction-is-rooted-not-guessed.cpp`: settled
   promises held only by retained resolvers do not keep result objects alive,
   a pending promise held only by native code still runs its reaction after a
   collection, and the ordinary reachable path still sees its result.

## Checks

- `make test` — **All 477 engine test suites passed successfully!** Cross-origin Window/Location and MessageChannel suite: **114 checks, 0 failure(s)**.
- `LITEBIRD_SUITE_SHARD=92/477 build/test_suite` — `a_promise_reaction_is_rooted_not_guessed` passed; stretch 92 of 477, 1 of 477 engine test suites passed.
- `python3 tools/conformance/verify_gc_residue.py` — **flat: 22 of 22 cases** (drift 0; `listener-pair` prints 1/1 with drift 0).
- `python3 tools/conformance/verify_gc_residue.py --filter promise --jobs 6 --timeout 30` — **flat: 6 of 6 cases**, all promise shapes 0 → 0.

## What this phase did not do

A resolver deliberately handed to native code still owns its `PromiseData`
while the promise is pending — that is what “pending” means. The change is
that this ownership is explicit and ends at settlement, not that native code
may settle a promise after its state has been reclaimed.

No DOM bindings were touched. No test262 run was needed for a collector-only
change; behaviour is pinned by the engine test suite and the residue oracle.
