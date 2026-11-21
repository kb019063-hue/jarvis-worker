# Automatic garbage collection at safe points

*Phase 145, finding 150.*

## 1. What was wrong

Phase 103 (`docs/phases/103-collection-at-a-safe-point.md`, finding 109) proved
that the collector could fire at statement and bytecode safe points without
corrupting program output (`make verify-gc-stress`), and `make gc-oracle`
verified cycle reclamation, live-set flatness, and retention. Yet nothing in
normal execution ever triggered a collection on its own: memory was still
reclaimed only when a script called `gc()`, when the shell opened its heap
view, or when stress mode was armed on the CLI.

On `main` (`fac72e7`), running `for (var i = 0; i < 400000; i++) { o = { a: i }; }`
without calling `gc()` grew to **412.4 MiB peak RSS**, and a 3,000,000-iteration
churn loop exhausted more than 3 GB.

## 2. What changed

**Per-agent allocation-threshold policy (`include/litebird/js/Heap.hpp`,
`src/js/Heap.cpp`).** Automatic collection uses the exact safe-point path
already installed at statement boundaries (`JsInterpreter::exec` →
`gc_stress_point`) and bytecode boundaries (`VM::run`), without touching
`JsEval.cpp`, `VM.cpp`, or `JsInterpreter.cpp`.

- Each agent (main page `agent == 0`, and each worker `agent > 0`) tracks
  `allocs_since_gc` and `threshold` (default minimum `kDefaultAutoGcMinThreshold
  = 8192` cells).
- When `allocate_object()` brings an agent to its threshold, it arms that
  agent's pending flag (`m_main_auto_pending` for agent 0, or bit `agent & 63`
  of `m_worker_pending_mask` for workers).
- The safe-point hot path in `Heap::take_gc_stress_request()` checks a single
  atomic flag for the calling agent with no mutex lock on the common path. Only
  when that agent's flag is set does it lock `m_mutex`, clear the flag,
  increment `m_auto_collections`, and return `true` so the caller runs
  `collect_garbage()`.
- At the end of `collect_garbage()`, the heap counts how many cells belonging
  to `collecting_agent` survived and sets the agent's next threshold to:

  ```cpp
  st.threshold = std::max(min_threshold, agent_alive);
  ```

  Under churn (small live set), collections fire every 8,192 allocations so
  resident memory stays flat; under a growing live set, the threshold doubles
  geometrically (2× heap growth), keeping total mark work O(N) amortized rather
  than O(N²).
- Stress mode (`set_gc_stress`) overrides the automatic policy cleanly and
  keeps `gc_stress_collections()` separate from `auto_gc_collections()`, so
  seeded stress runs and `verify_gc_stress.py`'s negative controls remain
  deterministic.
- Also fixed a latent data race in `collect_garbage()`: `collecting_agent` is
  now captured in a local variable rather than reading the shared member
  `m_collecting_agent` after `m_mutex` is released during teardown.

## 3. How it was verified

Paired measurements comparing `main` at `fac72e7` (`build/litebird-before`)
against this branch (`build/litebird`), median of 3 runs each (wall clock +
Linux `wait4` `ru_maxrss`):

| Workload | Before time | After time | Before peak RSS | After peak RSS |
| :--- | ---: | ---: | ---: | ---: |
| `churn 200k (no gc)` | 211.7 ms | **181.0 ms** | 210.9 MiB | **18.0 MiB** (-91.5%) |
| `churn 400k (no gc)` | 348.8 ms | **274.6 ms** | 412.4 MiB | **18.1 MiB** (-95.6%) |
| `cycle churn 50k (no gc)` | 95.0 ms | 96.2 ms | 118.6 MiB | **18.7 MiB** (-84.2%) |
| `live array 100k` | 119.6 ms | 171.1 ms | 119.0 MiB | 119.5 MiB |
| `compute 200k (0 alloc)` | 53.3 ms | 53.8 ms | 11.2 MiB | 11.2 MiB |
| `churn 3M (no gc)` | OOM (>3 GB) | **2160.6 ms** | >3,000 MiB | **18.0 MiB** |

Checks run:

| Command | Result |
| :--- | :--- |
| `make test` | **304 / 304** engine suites + 114 cross-origin checks (new suites: `automatic_gc_threshold_and_geometric_growth`, `automatic_gc_reclaims_churn_without_calling_gc`) |
| `make gc-oracle` | PASS (`baseline 184`, all 5 rounds `+0`, `collections_count 7`, `collected_cells 9206`, `retained keep|child`) |
| `make verify-gc-stress` | self-test 3/3 controls caught; `tests/gc-conformance` 28/28 (958 collections); `tests/dom-conformance` 678/678 (11,160 collections) |
| `make js-conformance` | 1143 / 1158 (98.7%), everyday probe 90/90, 0 crashes |
| `make dom-conformance` | 678 / 678 (100.0%), everyday probe 68/68 |
| `make conformance` | 1973 / 1973 (100.0%) |
| `make url-conformance` | 1188 / 1188 (100.0%) |
| `make wpt` | 9567 / 9824, 0 unlisted |
| `make render-conformance` | 29 / 34 pass, 5 known |
| `make security-worker` | 17 / 17 (including 60/60 4× concurrent Worker startup/exit) |
| `make bench` | all 45 string benchmarks pass vs Node.js |
| `make bench-collections` | Map `90 -> 83 ms`, Set `61 -> 51 ms`, 0 failures |
| `make bench-styles` | 3 / 3 pass under RSS cap |
| `make tsan` | JsString TSan + ASan pass |
| `make asan-unit` | **303 / 303** suites + 114 cross-origin checks under ASan+UBSan |
| `make asan-js` / `asan-dom` / `asan-verify` | JS 1142/1158, DOM 678/678, URL 1188/1188, media 105/105, color/brotli/regexp/string-unicode pass; `verify_gc_stress.py --binary build-asan/litebird` 28/28 + 3/3 self-test |

## 4. What is still open

- **Known leaks remain.** Native function captures (`o.b = f.bind(o)`) and
  pending timer closures are still opaque to the collector, and a full leak
  audit has not been done (`AGENTS.md`).
- **Event-loop and native-callback boundaries.** A collection requested inside
  a native callback that never re-enters a JS statement waits until the next
  statement boundary; adding a safe point between event-loop turns would cover
  callback-only allocation loops.
- **Stop-the-world mark-sweep.** Collection is still O(heap) per cycle and
  holds `m_mutex` across the shared cell list during marking; incremental or
  generational collection and per-agent cell lists remain future work.
