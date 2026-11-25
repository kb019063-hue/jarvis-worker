# Finding 430 — a promise reaction was rooted by a use count

**Severity:** an over-retention bug in the collector’s root finder, in the
sense that matters for leak audits: `Promise::trace_extra` decided whether the
reactions of a promise belonged to the cell from `state.use_count()`. That
number is not an ownership graph. A promise whose resolving pair was still
held anywhere — a timer closure, an executor, an `async` function suspended
at `await` — had a use count above the threshold even after settlement, so its
reactions and result were treated as native-rooted and kept alive. The
`promise-settled-from-timer` residue case stayed flat, which is why the harness
did not flag it; the over-retention was in the common case of a promise whose
resolvers are retained past settlement.

## What was wrong

`Promise::trace_extra` did this, in effect:

```cpp
const long holders = state.use_count();
// The value's payload and this cell's own handle are the two owners a
// promise has when only script refers to it. Any more and native code
// can settle it after the object is gone, so its reactions must not be
// counted as belonging to this cell: they stay roots.
if (heap.counting() && holders > 3) return;
```

Three problems came out of that one guess:

1. **`use_count()` is a snapshot of shared-pointer copies, not reachability.**
   A resolver function carries its `PromiseData` in an opaque `std::function`;
   the count tells the collector nothing about whether the promise object, the
   resolver pair, the microtask queue, or an event-loop callback is what holds
   the copy.
2. **It over-retained settled promises.** Once `resolve(v)` had been called,
   the resolving pair remained fully functional until it was called, and the
   pair held `PromiseData`. A script that built promises in a loop and kept
   the resolver functions left every result object alive, even though no
   reaction could observe those results through the dropped promise objects.
3. **It inferred “native-held” from “more than two owners”.** The registry
   already existed for the real case — a pending promise whose only reference
   is native code — but `trace_extra` baked a second, coarser guess into the
   cell instead of asking the registry.

## What changed

- `include/litebird/js/Promise.hpp`
  - New `ResolverState`: the state shared by one promise’s resolving pair
    (`shared_ptr<PromiseData> data`, `bool called`).
  - `PromiseData` now records weak references to the resolver states made for
    it, and declares `release_resolvers()`.
- `src/js/Promise.cpp`
  - `PromiseData::release_resolvers()` locks each weak resolver state and
    clears its strong `data` reference.
  - `settle()` calls `release_resolvers()` before moving handlers to the
    microtask queue. After settlement a retained resolver is inert: called or
    not, it no longer owns the settled promise’s state.
  - `PendingRegistry` entries are `std::weak_ptr<PromiseData>`. The root
    provider locks each entry and, while the promise is pending and has
    handlers, pushes the stored result and every handler slot that can carry a
    value — `on_fulfilled`, `on_rejected`, `resolve`, `reject` — plus every
    `keep_alive` value.
  - `PendingRegistry::release(data)` removes the entry in `settle()`, so a
    settled promise is not kept by the registry either. The doubling prune now
    also drops entries with no handlers.
  - `make_resolving_functions()` captures a shared `ResolverState` in both the
    resolve and the reject closure instead of capturing `PromiseData`
    directly.
  - `Promise::trace_extra` no longer consults `use_count()`. If the cell is
    reached, its stored result and handlers are traced, like any other
    reference stored in the cell.

The ownership rule is now: a **pending** promise with reactions is rooted
through the registry; a **settled** promise is traced from whoever still
reaches its cell/value; a **resolver** owns the state only until settlement.

## What was measured

On this branch, with `build/litebird` built from the changed tree:

| command | result |
|---|---|
| `make test` | All **485** engine test suites passed; cross-origin suite **114 checks, 0 failures**. |
| `LITEBIRD_SUITE_SHARD=96/485 build/test_suite` | `a_promise_reaction_is_rooted_not_guessed` passed (stretch 96 of 485). |
| `python3 tools/conformance/verify_gc_residue.py` | **flat: 22 of 22 cases**, drift 0. |
| `python3 tools/conformance/verify_gc_residue.py --filter promise` | **flat: 6 of 6 cases**, 0 cells at 200 and 2,000 iterations. |

The new unit suite asserts the differential that made the bug visible:
`(large_object - small_object) - (large_primitive - small_primitive) == 0` for
settled promises whose resolvers are retained. It also asserts that a promise
reachable only from native code still runs its `.then` reaction after a
collection, and that the ordinary reachable settled path still produces its
result.

## What is still open

A promise that is pending forever and reachable only through a resolver handed
to native code will, correctly, stay alive until that resolver is called or
dropped: there is no way for the collector to know the native code will never
use it. That is the registry’s job, and it is not measured as a leak because
it is not one. What Phase 430 removes is the cell-local guess that made even
settled promises uncollectable when their resolving pairs were retained.
