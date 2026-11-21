# 150. The collector waited to be asked even after both oracles proved it sound

**Status: fixed for automatic scheduling; known native-capture leaks remain open.**

## What was wrong

Phase 103 (`make verify-gc-stress`, finding 109) verified that collecting at
statement and bytecode safe points never changed program output across 254,174
stress collections, and `make gc-oracle` verified cycle reclamation, flatness,
and retention. However, `Heap::allocate_object()` only armed a safe-point
request when `--gc-stress` was enabled. Any ordinary page or script that did
not call `gc()` grew linearly without bound: 400,000 temporary objects in a
loop reached **412.4 MiB peak RSS**, and 3,000,000 objects exceeded 3 GB.

## What changed

`Heap` now enables a per-agent automatic collection policy by default
(`kDefaultAutoGcMinThreshold = 8192` cells, growing geometrically as
`max(min_threshold, surviving_agent_cells)` after each collection) using the
existing safe points in `JsInterpreter::exec` and `VM::run`. Main-thread safe
points check a single atomic bool (`m_main_auto_pending`) lock-free; worker
agents check their own bit in `m_worker_pending_mask` so one agent's
allocations never trigger or lock another agent's safe point. Stress mode
(`set_gc_stress`) overrides automatic scheduling to stay deterministic.

See [`docs/phases/145-automatic-garbage-collection.md`](../phases/145-automatic-garbage-collection.md).

## How it was verified

- `make test` (**304/304** suites, including
  `automatic_gc_threshold_and_geometric_growth` and
  `automatic_gc_reclaims_churn_without_calling_gc`), `make asan-unit`
  (**303/303** pre-merge under ASan+UBSan).
- `make gc-oracle` (PASS), `make verify-gc-stress` (self-test 3/3, corpus
  28/28, DOM 678/678), `make bench-collections` (PASS), `make security-worker`
  (17/17).
- Paired peak RSS on `for (var i = 0; i < 400000; i++) { o = { a: i }; }`:
  **412.4 → 18.1 MiB (-95.6%)**, wall time **348.8 → 274.6 ms**; 3,000,000
  iterations finishes in **2,160.6 ms** at **18.0 MiB peak RSS**.

## What is still open

- Native-capture leaks (`o.b = f.bind(o)`, pending timers) and a full leak
  audit.
- Safe points at event-loop turn boundaries for allocation-only native
  callbacks.
- Incremental/generational collection and per-agent cell lists.
