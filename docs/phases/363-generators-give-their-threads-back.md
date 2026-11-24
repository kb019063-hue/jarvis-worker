# Phase 363 — generators give their threads back

## The row

`AGENTS.md`, "GC leak audit & native-capture tracing". Phase 261
([finding 265](../findings/265-the-thread-a-generator-never-gave-back.md))
added four generator shapes to `tools/conformance/verify_gc_residue.py` as
`known` defects: every generator leaked 7–8 cells per iteration and hit an OS
thread ceiling at 15,730 generators.

## What was done

See [finding 363](../findings/363-generators-kept-their-frames-and-threads-alive.md)
for the root-cause breakdown. Five coordinated changes close the leak and the
thread ceiling:

1. **Lazy worker thread creation (`include/litebird/js/Generator.hpp`,
   `src/js/JsEval.cpp`).**
   `spawn_frame` now stores the worker body in `GeneratorData::start_thunk`
   and starts no OS thread. `GeneratorData::resume()` spawns `worker` lazily
   on the first `.next()` or `.throw()` (running one GC cycle and retrying
   once on `std::system_error` before reporting thread exhaustion). Generators
   that are never stepped, or closed with `.return()` before starting, never
   create an OS thread.

2. **Exact generator ownership, tracing, and cycle-count accounting
   (`include/litebird/js/Heap.hpp`, `src/js/Heap.cpp`,
   `src/js/JsEval.cpp`, `src/js/JsInterpreter.cpp`).**
   * Stored `shared_ptr<GeneratorData>` solely in the generator object's
     `cell->internal`, and moved `.next`, `.return`, `.throw`, and
     `[Symbol.iterator]`/`[Symbol.asyncIterator]` to capture the generator
     object via `Extra::captured` (`JsValue::current_captured()`) instead of
     capturing `shared_ptr<GeneratorData>` inside opaque `std::function`
     lambdas. The worker thread reads `raw->frame_env` by reference instead of
     holding a duplicate `EnvPtr`.
   * Added `Heap::count_environment` alongside `mark_environment` so
     `trace_extra` callbacks that trace `JsEnvironment` graphs credit all
     heap-owned environment edges during `Heap::collect_cycles`.
   * Attached a `trace_extra` callback to every generator `JsObject` that
     traces and counts `frame->frame_env`, parked `ScopeStack` entries
     (`active_scope_stack`, via `ScopeEntry::owned_local_ref`), and
     `send_value` / `yielded_value` / `error` / `pending_throw` /
     `pending_return` whenever `cell->internal.use_count() == 1` and
     `!frame->is_running()`.
   * Stopped `JsInterpreter::register_gc_roots` and `ScopeStackRegistry` from
     unconditionally rooting parked non-coroutine generators
     (`!is_coroutine && !is_running()`); only actively running frames and
     async-function coroutines (`is_coroutine`) remain global roots.
   * Cleared `frame_env`, `start_thunk`, and value slots in
     `GeneratorData::reap()` / `terminate()`, and ensured `advance` calls
     `reap()` on uncaught throws as well as normal completion.

3. **GC-safe thread unwind without running user finalizers
   (`include/litebird/js/Generator.hpp`, `src/js/JsEval.cpp`,
   `src/js/JsOps.cpp`).**
   * Added `bool run_finalizers` to `GeneratorAbortException` (`true` for
     explicit `.return()`, `false` for GC `terminate()`).
   * Guarded `try`/`finally`, `for...of` (`iterate_with`), and `yield*` abort
     handlers so GC teardown unwinds the parked C++ stack and joins the worker
     thread without invoking user `finally` blocks or `IteratorClose`.
   * Tagged `ScopeStack` with `Heap::current_agent()` so concurrent `Worker`
     threads never cross-trace each other's parked-generator scope stacks.

4. **Residue harness updated (`tools/conformance/verify_gc_residue.py`).**
   * Removed `known` from all four Phase 261 generator cases
     (`generator-never-advanced`, `generator-advanced-once`,
     `generator-finished`, `generator-holds-value`).
   * Added three more cases: `generator-suspended-in-for-let`,
     `generator-throws`, and `async-generator-dropped`.
   * Result: `flat: 22 of 22 cases` (0 `known`, 0 drift across all 22 cases).

5. **Unit regression suite
   (`tests/suites/363-generator-gc-reclaims-frames-and-threads.cpp`).**
   Covers flat GC residue across six generator shapes, live suspended
   generator survival across `gc()`, GC discard skipping `finally` vs
   explicit `.return()` running `finally`, and creating 20,000 live unstarted
   generators without hitting OS thread limits.

## What this phase did not do

* The `holders > 3` heuristic in `Promise::trace_extra` (`src/js/Promise.cpp`)
  was not touched (`src/js/Promise.cpp` is claimed by open PR #350 and the
  existing promise residue shapes are already flat).

## Checks

* `python3 tools/conformance/verify_gc_residue.py` — **flat: 22 of 22 cases**
  (0 known, 0 drift; all 7 generator cases `0 -> 0`).
* `build-asan/litebird` (ASan + UBSan):
  * `verify_gc_residue.py --binary build-asan/litebird --small 100 --large 500`
    — **flat: 22 of 22 cases**, 0 sanitizer errors.
  * `run_js_conformance.py --binary build-asan/litebird --filter async` —
    **34/34 (100.0%)**; `--filter gc-safety` — **32/32 (100.0%)**.
  * `make asan-verify` — all suites green (including 20/20 concurrent Worker
    startup/exit stability), 0 sanitizer errors.
  * 20,000 unstarted + 2,000 suspended + live-gc stress test under ASan —
    passed with 0 errors.
* `make test` — **427/427** engine test suites and **114/114** cross-origin
  checks passed.
* `make verify-gc-stress` — **28/28** (84/84 runs, 1,266 collections) +
  **678/678** (1,356/1,356 runs, 11,830 collections), **0 diffs**.
* `make js-conformance` — **1,150/1,158 (99.3%)**, 0 crashes/timeouts.
* `make dom-conformance` — **678/678 (100.0%)**.
* `make verify-promise-iterators` — **34/34 (100.0%)**.
* `run_test262.py` across 8 generator and async-generator directories —
  **1,539/1,635 (94.1%)**, 0 crashes/timeouts.
* `make tsan` — passed.
* Thread-ceiling probes on `build/litebird`: 20,000 live unstarted, 20,000
  suspended reclaimed via `gc()`, 20,000 finished without `gc()`, and 20,000
  throwing without `gc()` all pass.
