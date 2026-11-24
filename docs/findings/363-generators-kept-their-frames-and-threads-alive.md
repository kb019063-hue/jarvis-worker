# Finding 363 — generators kept their frames and threads alive

**Severity:** a hard failure under load, first surfaced in
[finding 265](265-the-thread-a-generator-never-gave-back.md): every generator
call leaked 7–8 heap cells per iteration (1,400–1,600 at 200 iterations;
14,000–16,000 at 2,000), regardless of whether the generator was never
advanced, suspended mid-body, or run to completion, and creating generators
in a loop hit an OS thread exhaustion ceiling at **15,730** generators
(`RangeError: Too many suspended functions: out of threads`).

Finding 265 recorded the symptom and proposed two candidate fixes (joining the
thread on drop and starting the thread lazily on first `.next()`). Auditing
the ownership graph in Phase 363 showed that **neither thread-joining nor lazy
threads alone would have fixed the 7–8 cell leak**, because four distinct
defects kept generator heap cells and worker threads alive.

## What was actually keeping generators immortal

1. **Generator methods captured `shared_ptr<GeneratorData>` inside opaque
   `std::function` closures, and generator objects had no `trace_extra`.**
   `make_generator` (`src/js/JsEval.cpp`) installed `.next`, `.return`, and
   `.throw` native methods whose C++ lambdas captured `shared_ptr<GeneratorData>`
   by value. `GeneratorData::frame_env` held a `shared_ptr<JsEnvironment>`
   pointing up the lexical scope chain (and the worker thread lambda held a
   second copy of `env`). Because `Heap::collect_cycles` (`src/js/Heap.cpp`)
   could neither see inside those `std::function` captures nor account for
   `frame_env` ownership through `trace_extra` and `Heap::count_environment`,
   every generator's `frame_env` had external `owners > seen` and was treated
   as permanently rooted — pinning the generator's function, environment,
   iterator methods, and `GeneratorData` forever (7 cells per iteration, or 8
   when capturing a local object). And because `GeneratorData::reap()` did not
   clear `frame_env`, **even a finished generator** whose worker thread had
   already been joined still leaked 7 cells per iteration.

2. **`m_generators` and `ScopeStackRegistry` unconditionally rooted every
   suspended generator.**
   `JsInterpreter::register_gc_roots` (`src/js/JsInterpreter.cpp`) walked
   `m_generators` and marked `frame->frame_env` as a global GC root for every
   undone generator, and `ScopeStackRegistry` (`src/js/JsEval.cpp`) marked
   every live worker thread's `ScopeStack` unconditionally. A dropped
   suspended generator therefore rooted its own enclosing scope and could
   never be collected, while removing those unconditional roots without
   tracing `frame_env` and `active_scope_stack` from the generator object's
   `trace_extra` would have caused use-after-collect on *live* suspended
   generators.

3. **Eager OS thread creation at `g()` and missing `reap()` on uncaught throws.**
   `spawn_frame` (`src/js/JsEval.cpp`) started a real OS `std::thread`
   immediately when the generator function was called, even if `.next()` was
   never invoked. In addition, when a generator body threw an uncaught
   exception, `advance` rethrew before calling `reap()`, leaving the finished
   worker thread unjoined until GC.

4. **GC teardown ran user `finally` blocks and `IteratorClose`.**
   When an unreachable suspended generator was eventually torn down by
   `GeneratorData::terminate()`, waking the parked worker thread threw
   `GeneratorAbortException`. Both `try ... finally` (`src/js/JsEval.cpp`),
   `yield*` (`eval_yield` in `src/js/JsEval.cpp`), and `for...of`
   (`iterate_with` in `src/js/JsOps.cpp`) caught `GeneratorAbortException`
   unconditionally and executed user `finally` blocks and `iterator.return()`.
   Per ECMAScript, garbage-collecting an unreachable generator must **not**
   execute `finally` or `IteratorClose` (only an explicit `.return()` does),
   and running arbitrary JS while `Heap` is sweeping breaks GC re-entrancy.

## What was measured

Measured on `build/litebird` via `tools/conformance/verify_gc_residue.py` at
200 and 2,000 iterations (and on `build-asan/litebird` with ASan+UBSan at 100
and 500 iterations):

| shape | before (@200 / @2000) | after (@200 / @2000) | drift |
|---|---|---|---|
| `generator-never-advanced` | 1400 / 14000 | **0 / 0** | 0 |
| `generator-advanced-once` | 1400 / 14000 | **0 / 0** | 0 |
| `generator-finished` | 1400 / 14000 | **0 / 0** | 0 |
| `generator-holds-value` | 1600 / 16000 | **0 / 0** | 0 |
| `generator-suspended-in-for-let` | *(not measured)* | **0 / 0** | 0 |
| `generator-throws` | *(not measured)* | **0 / 0** | 0 |
| `async-generator-dropped` | *(not measured)* | **0 / 0** | 0 |

Thread-ceiling checks on `build/litebird`:

* **20,000 live unstarted generators** retained simultaneously in an array:
  passes with 0 worker threads spawned (previously died at 15,730 with
  `RangeError: Too many suspended functions: out of threads`).
* **20,000 suspended generators** stepped once and reclaimed via periodic
  `gc()`: passes; parked threads are woken, unwound without running `finally`,
  and joined cleanly.
* **20,000 finished generators** and **20,000 throwing generators** without
  calling `gc()`: both pass; `reap()` joins every finished worker thread
  immediately upon normal return or uncaught throw.

## Note on nested `yield*`

A generator suspended inside `yield* inner` holds a C++ stack local
`JsValue iterator` on its parked thread stack. When the outer generator
becomes unreachable, the first `gc()` collects the outer generator and
unwinds its worker stack (releasing `iterator`), and the next `gc()` reclaims
`inner` (0 cells residue after two `gc()` cycles).
