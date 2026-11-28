# Phase 505: ES2025 Set.prototype set methods

**Finding:** [`docs/findings/505-set-prototype-lacked-es2025-set-methods.md`](../findings/505-set-prototype-lacked-es2025-set-methods.md)

ECMA-262 2025 (§24.2.3) defines seven set operations on `Set.prototype`:
`union`, `intersection`, `difference`, `symmetricDifference`, `isSubsetOf`,
`isSupersetOf`, and `isDisjointFrom`. None of them were installed, so scripts
and `test262` failed with `TypeError` or `undefined`.

## What changed

All seven methods are implemented and installed on `Set.prototype` in
`src/js/BuiltinsCollections.cpp` via `js_define_method(..., 1, ...)` (`length`
1, `writable: true`, `enumerable: false`, `configurable: true`, non-constructible):

- **`get_set_record`** (§24.2.1.2): validates object-like argument, accesses
  `size` -> `ToNumber` -> throws `TypeError` on `NaN` -> `ToIntegerOrInfinity`
  (`js_to_integer` with `-0` canonicalized to `+0`) -> throws `RangeError` on
  `< 0` (permitting `-0.5` -> `0`, `0.5` -> `0`, and `+Infinity`), then gets and
  checks callability of `has` before `keys`.
- **Iterator protocol** (§7.4.3 / §7.4.8 / §7.4.11): `get_iterator_from_method`
  calls `keys` with `setRec.set` as `this`, validates object iterator, and
  reads `next`; `iterator_step_value` validates `next` callability before calling
  `call_with_this` (because `JsValue::call_with_this` returns `undefined` on
  non-functions), checks object step result, reads `ToBoolean(done)` before
  `value`, and leaves `return()` uncalled on abrupt `next`/`done`/`value`
  completions; `iterator_close_normal` invokes `return()` on early `false`
  exits in `isSupersetOf` and `isDisjointFrom`, validating `IsCallable` and
  object return value.
- **Canonicalization, mutation, and cursor safety**: incoming values from
  `keys()` are canonicalized (`-0` -> `+0`), and keys passed from `O` into
  `has()` are already `+0`-normalized in `CollectionData`. Live receiver walks
  (`intersection` `<=`, `isSubsetOf`, `isDisjointFrom` `<=`, and `difference`
  `<=` on `result_data`) hold an RAII `CursorGuard` registered with
  `CollectionData` so tombstones and `compact_if_worthwhile()` / `clear()`
  remapping cannot skip elements or leak cursors on exceptions. `union` and
  `difference` snapshot `O` into `result_data` after `GetSetRecord` (matching
  Node v22.22.3 / V8 on `keys()` receiver mutation in `union`), while
  `symmetricDifference` calls `get_iterator_from_method` before snapshotting `O`
  and checks both live `O` and `result_data`.
- **Plain `Set` return and isolation**: returning methods allocate a fresh
  `Set` via `make_collection(ObjectClass::Set, JsIntrinsics::get().set_proto)`
  (with `trace_extra` GC marking), ignoring receiver `size`/`has`/`keys`,
  `Set[Symbol.species]`, and `Set.prototype.add` overrides.

## Verification

Measured on the rebased merge head (`origin/main` `83dba0cd`):

- `tests/suites/505-set-methods.cpp` (`set_prototype_es2025_set_methods`):
  `make -j"$(nproc)" build/test_suite && ./build/test_suite` — **542 / 542**
  engine suites passed (including `forceGc()` triggered inside `keys()`/`next()`).
- `python3 tools/conformance/verify_set_methods.py --binary build/litebird` —
  **11 / 11** differential test groups matched Node v22.22.3 byte-for-byte
  (metadata, core algebra, self/`Map`/array-like, receiver ordering, `size`
  coercion, access/branch order, `-0`/truthiness, live mutation + compaction,
  `IteratorClose`, subclass isolation, insertion order).
- `test262` (`2e58f87d8350ad51224617a041c6e041d52f8444`):
  - Seven `built-ins/Set/prototype/{union,intersection,difference,symmetricDifference,isSubsetOf,isSupersetOf,isDisjointFrom}` directories: **28 / 186 (15.1%) -> 165 / 186 (88.7%)**, **+137 fixed, 0 lost**. All 21 remaining failures are `SyntaxError: BigInt literals are not supported` (`called-with-object.js`, `require-internal-slot.js`, `size-is-a-number.js` across the seven directories).
  - Full `built-ins/Set`: **354 / 383 (92.4%)**.
- Regression & perf checks on the same build:
  - `python3 tools/conformance/run_js_conformance.py --binary build/litebird`: **1,153 / 1,158 (99.6%)**, 90 / 90 everyday.
  - `python3 tools/conformance/run_dom_conformance.py --binary build/litebird`: **678 / 678 (100.0%)**, 68 / 68 everyday.
  - `make verify-collection-iterators`: **33 / 34 (97.1%)**; `make bench-collections`: `Map` 67 -> 61 ms, `Set` 49 -> 44 ms, checksum 98304; `make verify-gc-stress`: 28 / 28 JS and 678 / 678 DOM cases agreed under stress GC.
- Sanitizers (`-fsanitize=address,undefined`, `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`): **11 / 11** `verify_set_methods.py`, **165 / 186** `test262` across the seven directories, and standalone `505-set-methods.cpp` (`forceGc()`) passed with zero sanitizer errors.

## Later closure and still open

Phase 556 ([phase](556-set-union-keys-before-snapshot.md),
[finding](../findings/556-the-snapshot-preceded-the-side-effect.md)) closes the
one documented specification deviation: `Set.prototype.union` now calls
`keys()` and gets the iterator's `next` property before snapshotting `O`, as
ECMA-262 requires, rather than preserving Node v22.22.3's early snapshot.

The 21 `BigInt`-literal `test262` cases in the seven directories remain blocked
on engine-wide `BigInt` support (`SyntaxError`).
