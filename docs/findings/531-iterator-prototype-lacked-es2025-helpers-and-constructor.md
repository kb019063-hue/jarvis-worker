# Finding 531: `Iterator`, `Iterator.from`, and `%IteratorPrototype%` helpers were absent

## What was wrong

1. `%IteratorPrototype%` carried only `[Symbol.iterator]` (plus a `"Symbol.iterator"` alias) installed inside `src/js/BuiltinsArray.cpp`, so `Iterator` was `undefined` on the global object and scripts calling `Iterator.from(...)` or chaining `.map()`, `.filter()`, `.take()`, `.drop()`, `.flatMap()`, `.reduce()`, `.toArray()`, `.forEach()`, `.some()`, `.every()`, or `.find()` threw `ReferenceError` or `TypeError`. Across `test262`'s `built-ins/Iterator` directory, only **15 / 684 (2.2%)** tests passed.
2. `iteration_result()` in `src/js/BuiltinsArray.cpp` populated `{ value, done }` using `set_property()` instead of defining own data properties (§7.4.9 `CreateIterResultObject`), so setter accessors installed on `Object.prototype.value` or `Object.prototype.done` intercepted iterator results.
3. Three sibling iterator issues surfaced during audit and are left open separately:
   - `js_make_iterator` in `src/js/BuiltinsArray.cpp` holds duplicate `JsValue` handles to each created iterator cell (in the `step` capture / `roots` storage), so the handle-counting GC treats every iterator created via `js_make_iterator` (`Array`, `Map`, `Set`, `String`, `RegExp`, `FormData`, `URLSearchParams`) as externally rooted (88 cells retained in `[1,2,3].values()` probes).
   - Generator objects in `src/js/JsEval.cpp` still have `null` prototype rather than inheriting `%GeneratorPrototype%` (`%IteratorPrototype%`), and `JsEval.cpp` / `TypedArray.cpp` still build iterator result objects via `set_property()`.
   - `TypedArray` `.values()` / `.keys()` / `.entries()` in `src/js/TypedArray.cpp` still return plain `{ next }` objects rather than objects inheriting `%ArrayIteratorPrototype%` / `%IteratorPrototype%`.
   - `BytecodeCompiler::compile_stmt` in `src/js/BytecodeCompiler.cpp` still rejects `for (let/const ... of ...)` loops, and `tests/test_suite.cpp`'s `automatic_gc_reclaims_churn_without_calling_gc` did not reset `set_auto_gc_min_threshold` before its bytecode VM churn loop after prior collections raised `threshold` above 12,000.

## What changed

- Added `src/js/BuiltinsIterator.cpp` (`install_iterator_builtins(JsIntrinsics& realm)`, declared in `include/litebird/js/JsBuiltins.hpp`, linked via `Makefile`, invoked from `install_array_builtins` in `src/js/BuiltinsArray.cpp`):
  - Abstract `Iterator` constructor (`%Iterator%`) with `new.target` validation, subclass `Derived` receiver preservation, and `Iterator.prototype` linkage.
  - `Iterator.from(O)` returning existing `Iterator` instances unchanged and otherwise wrapping valid iterables/iterators under `%WrapForValidIteratorPrototype%` (`next`, `return`).
  - `%IteratorPrototype%.constructor` and `[Symbol.toStringTag]` (`"Iterator"`) writable-by-override accessors ignoring prototype properties via `SetterIgnoringPrototypeProperties`, plus `[Symbol.iterator]` and `"Symbol.iterator"` sharing one `[Symbol.iterator]` method identity.
  - Lazy `%IteratorHelperPrototype%` (`@@toStringTag: "Iterator Helper"`, `next`, `return`) backing `.map`, `.filter`, `.take`, `.drop`, and `.flatMap`, plus eager consumers `.toArray`, `.forEach`, `.some`, `.every`, and `.find` and `.reduce`.
  - Per-cell internal slot type validation via `IteratorSlotRegistry` and cycle-safe `Heap` `trace_extra` marking over `WrapIteratorData`, `IteratorHelperData`, and `IteratorRealmData`.
- Updated `iteration_result()` in `src/js/BuiltinsArray.cpp` to allocate `{ value, done }` as own writable/enumerable/configurable data properties directly.
- Updated `automatic_gc_reclaims_churn_without_calling_gc` in `tests/test_suite.cpp` to restore `kDefaultAutoGcMinThreshold` before its VM churn loop.
- Added standalone regression suite `tests/suites/531-iterator-helpers.cpp`.

## Verification

- `make -j"$(nproc)" test`: **573 / 573** engine suites passed + **114 / 114** cross-origin checks (including `tests/suites/531-iterator-helpers.cpp`: Node-parity JS semantics, bytecode VM `for (var x of ...)` chain, Web Worker `Iterator` exposure, and 2,000-iteration `Iterator.from`/helper cycle GC reclamation with `after == baseline`).
- `python3 tools/conformance/run_test262.py --binary build/litebird built-ins/Iterator`: **15 / 684 -> 315 / 684 (46.1%)** (+300 passes; 272 tests outside the 412 ES2025 targets belong to Stage 3 proposals); **297 / 412 (72.1%)** across the 24 ES2025 directories/files; **72 / 73 (98.6%)** across `built-ins/{Array,Map,Set,String,RegExpString}IteratorPrototype`.
- `make gc-oracle`, `make verify-gc-stress`, `make js-conformance` (`1154/1158`), `make dom-conformance` (`678/678`), `make url-conformance` (`1188/1188`), `make conformance` (`1973/1973`), `make wpt` (`9570/9824`, 0 unlisted), `make verify-bytecode` (`188/188` agree, `8/8` refused), `make fuzz-pages` / `make fuzz-pipeline` (400 runs each, 0 findings), and ASan+UBSan conformance/GC/`531` checks all pass.

See [Phase 531](../phases/531-iterator-constructor-from-and-helpers.md) for the full breakdown and open follow-ups.
