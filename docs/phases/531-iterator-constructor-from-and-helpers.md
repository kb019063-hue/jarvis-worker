# Phase 531: ES2025 `Iterator`, `Iterator.from`, and `%IteratorPrototype%` Helpers

Implemented the ES2025 Iterator Helpers specification (ECMA-262 §27.1) in `src/js/BuiltinsIterator.cpp`, wired through `install_iterator_builtins(JsIntrinsics& realm)` in `include/litebird/js/JsBuiltins.hpp`, `src/js/BuiltinsArray.cpp`, and `Makefile`.

## What changed

- **Abstract `Iterator` constructor and `Iterator.prototype` metadata (`src/js/BuiltinsIterator.cpp`)**:
  - Added global `Iterator` constructor throwing `TypeError` on direct call or `new Iterator()`, preserving `Derived` receivers for `class Sub extends Iterator {}` / `Reflect.construct`.
  - Installed `%IteratorPrototype%.constructor` and `%IteratorPrototype%[Symbol.toStringTag]` (`"Iterator"`) as configurable accessors following `SetterIgnoringPrototypeProperties`, and shared a single `"[Symbol.iterator]"` function across `kIteratorKey` (`"@@iterator"`) and legacy `"Symbol.iterator"`.
  - Stored per-realm `IteratorRealmData` (`iterator_ctor`, `wrap_proto`, `helper_proto`) with `weak_ptr` `trace_extra` marking on `%IteratorPrototype%`.
- **`Iterator.from(O)` and `%WrapForValidIteratorPrototype%`**:
  - Validates `GetIteratorFlattenable(O, iterate-strings)` (calling `@@iterator` if present and non-nullish, otherwise wrapping iterator objects directly while rejecting non-objects).
  - Returns `O` directly when `O instanceof Iterator` (`OrdinaryHasInstance`), otherwise wraps `{ iterator, nextMethod }` in `%WrapForValidIteratorPrototype%` (`next`, `return`) without holding permanent GC roots.
- **Lazy helper generators and eager consumers on `%IteratorPrototype%`**:
  - Implemented lazy helpers `.map(mapper)`, `.filter(predicate)`, `.take(limit)`, `.drop(limit)`, and `.flatMap(mapper)` returning `%IteratorHelperPrototype%` (`@@toStringTag: "Iterator Helper"`) iterators with `SuspendedStart` / `SuspendedYield` / `Executing` / `Completed` state machine, abrupt `IteratorCloseAll` propagation (skipping engine-internal unwind exceptions via `js_is_internal_unwind`), nested inner-iterator closure on `.flatMap()` `.return()` / throw, and eager validation closing the receiver via `IteratorClose` when arguments fail validation.
  - Implemented `.toArray()`, `.forEach(fn)`, `.some(predicate)`, `.every(predicate)`, `.find(predicate)`, and `.reduce(reducer[, initialValue])` with spec-compliant short-circuit `IteratorClose` (`some`/`every`/`find`) and abrupt-completion `IteratorClose`.
  - Validated receivers per-cell using `IteratorSlotRegistry` so forged `{ __proto__: helper_proto }` or wrapper prototypes throw `TypeError`.
- **Own-property iteration result & test-harness threshold fix**:
  - Updated `iteration_result()` in `src/js/BuiltinsArray.cpp` to define own `value` and `done` data properties directly rather than `set_property()` (`CreateIterResultObject`), preventing `Object.prototype` setters on `value` or `done` from firing during iteration.
  - Updated `tests/test_suite.cpp` (`automatic_gc_reclaims_churn_without_calling_gc`) to restore `Heap::kDefaultAutoGcMinThreshold` before the VM churn loop so prior suite cells do not push `threshold` past 12,000 before the loop runs.

## Verification

- `tests/suites/531-iterator-helpers.cpp` (`make -j"$(nproc)" test`): **579 / 579** engine test suites + **114 / 114** cross-origin checks pass. Covers constructor/subclassing, `Iterator.from`, lazy `.map`/`.filter`/`.take`/`.drop`/`.flatMap` short-circuiting and abrupt `return()`, re-entrancy guards, `SetterIgnoringPrototypeProperties`, forged-receiver brand checks, eager consumers, `Object.prototype.value`/`done` setter immunity, Node v22.22.3 parity probes, bytecode VM `for (var x of ...)`, Web Worker global `Iterator`, and cyclic GC reclamation (`2,000` `Iterator.from`/helper cycles reclaimed cleanly, `after == baseline`).
- `python3 tools/ci/check_suites.py --base origin/main`: **0 failure(s)** across 189 standalone suites.
- `python3 tools/conformance/run_test262.py --binary build/litebird built-ins/Iterator`: **15 / 684 (2.2%) -> 315 / 684 (46.1%)** (+300 passes; the 272 tests outside the 412 ES2025 targets belong to Stage 3 proposals such as `concat`, `zip`, `zipKeyed`, `Symbol.dispose`, `chunks`, `includes`, `join`, `windows`).
  - Targeted 24 ES2025 `built-ins/Iterator` dirs/files: **297 / 412 (72.1%)**; excluding `function*`/`$262`/BigInt: **262 / 269 (97.4%)** (3 `temporalHelpers.js` BigInt failures, 4 obsolete 2020-draft `Number.MAX_SAFE_INTEGER + 1` tests).
  - `built-ins/{Array,Map,Set,String,RegExpString}IteratorPrototype`: **72 / 73 (98.6%)** (remaining 1 uses `$262.detachArrayBuffer`).
- Differential checks against Node.js `v22.22.3`: **31 / 32** + **7 / 7** probes match (`Iterator.from`/`.take`/`.map`/`.flatMap` invalid-argument `IteratorClose` follows ES2025/test262 where V8 differs).
- Suite & oracle checks on this branch (`78bccd3` + Phase 531):
  - `make gc-oracle`: PASS (`570` baseline, `+0` across 5 churn rounds, `13,812` cells collected).
  - `make verify-gc-stress`: **28 / 28** JS (`84 / 84` runs, `1,266` collections) and **678 / 678** DOM (`1,356 / 1,356` runs, `11,830` collections), `0` differences.
  - `make js-conformance`: **1154 / 1158 (99.7%)**, `90 / 90` everyday, `0` crashes (`1153/1158` on `22aa96b`, with Phase 526 on `origin/main` supplying the +1 in `prototypes.dat`).
  - `make dom-conformance`: **678 / 678 (100.0%)**, `68 / 68` everyday.
  - `make conformance`: **1973 / 1973 (100.0%)**; `make url-conformance`: **1188 / 1188 (100.0%)**; `make wpt`: **9570 / 9824** (`0` unlisted); `make diff-trees`: **1500 / 1500** x 4 (`0` differ); `make diff-strings`: **50,580 / 50,580** (`0` differ); `make render-conformance`: **67 / 68** (+ 1 known AVIF) + **41 / 41** body-origin + **45,016 / 45,016** canvas checks; `make verify-bytecode`: **188 / 188** agree, **8 / 8** refused; `make verify-collection-iterators`: **33 / 34**; `make verify-promise-iterators`: **34 / 34**; `deep_pages.py --only script`: **15 / 15** runs pass.
- ASan + UBSan (`build-asan/litebird`, `build-asan/cross_origin_test`, `ASAN_OPTIONS=detect_leaks=0`):
  - `cross_origin_test`: **114 / 114**; `make asan-js`: **1153 / 1158** (`JS_ASAN_FLOOR=1122`); `make asan-dom`: **678 / 678** + **1188 / 1188**; `verify_gc_stress.py --seeds 1,2`: **28 / 28** (`56 / 56` runs, `838` collections); `make fuzz-pages`: **400 / 400** (`0` findings); `make fuzz-pipeline`: **400 / 400** (`0` findings); `test262` (`built-ins/Iterator` + 5 `*IteratorPrototype` dirs): **387 / 757**; standalone `531-iterator-helpers.cpp` linked against `build-asan/*/*.o`: PASS with zero ASan/UBSan diagnostics. (Local limits: full `make deep` timed out at 600s after `table` / `details` layouts on this 2-core sandbox, `make xml-conformance` / `make diff-xml` require external W3C corpus / `lxml`, and compiling full monolithic `tests/test_suite.cpp` under ASan+UBSan timed out on this 2-core / 3.8 GiB sandbox; CI runs those full jobs.)

## Known open follow-ups

- `js_make_iterator` (`src/js/BuiltinsArray.cpp`) still retains duplicate `JsValue` handles for `Array`/`Map`/`Set`/`String`/`RegExp`/`FormData`/`URLSearchParams` iterators (preventing GC reclamation of unreferenced built-in iterators).
- Generator instances (`src/js/JsEval.cpp`) still have `null` prototype instead of `%GeneratorPrototype%` inheriting `%IteratorPrototype%`, `JsEval.cpp` / `TypedArray.cpp` still create `{ value, done }` via `set_property()`, and `TypedArray` iterators (`src/js/TypedArray.cpp`) remain plain objects rather than inheriting `%ArrayIteratorPrototype%`.
- `RegExp String Iterator` still lacks `.return()`, `BytecodeCompiler.cpp` still rejects `for (let/const ... of ...)`, and `PropertyMap` symbol keys remain string-encoded.

See [Finding 531](../findings/531-iterator-prototype-lacked-es2025-helpers-and-constructor.md).
