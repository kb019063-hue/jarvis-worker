# Finding 505: Set.prototype lacked ES2025 set methods

## What was wrong

`Set.prototype` implemented ES2015 methods (`add`, `has`, `delete`, `clear`,
`forEach`, `values`, `keys`, `entries`, `[Symbol.iterator]`) but none of the
seven ES2025 set methods (`union`, `intersection`, `difference`,
`symmetricDifference`, `isSubsetOf`, `isSupersetOf`, `isDisjointFrom`). Across
the seven `built-ins/Set/prototype/*` directories of `test262`, only 28 of 186
tests passed (specifically the tests for missing or non-applicable errors).

## What changed

`src/js/BuiltinsCollections.cpp` adds `get_set_record`,
`get_iterator_from_method`, `iterator_step_value`, `iterator_close_normal`,
`canonicalize_key`, `copy_live_entries`, an RAII `CursorGuard`, and all seven
methods on `Set.prototype`. Early returns (`isSubsetOf` when `O.live > size`,
`isSupersetOf` when `O.live < size`), size-selected branch routing (`has` when
`O.live <= size`, `keys` otherwise), live receiver mutation / compaction via
`CollectionData::Cursor`, `-0` -> `+0` normalization, `IteratorClose` on early
`false` completion, plain `%Set%` creation, and `IsCallable` checks follow
ECMA-262 2025 §24.2.1 / §24.2.3 and agree with Node v22.22.3.

## Verification

- `test262` (`2e58f87d8350`): **28 / 186 -> 165 / 186** across the seven
  `Set.prototype` method directories (+137 passes, 0 regressions; remaining 21
  failures are `BigInt` literal syntax errors).
- `python3 tools/conformance/verify_set_methods.py --binary build/litebird`:
  **11 / 11** cases match Node v22.22.3.
- `tests/suites/505-set-methods.cpp`: **542 / 542** engine test suites pass;
  zero ASan/UBSan findings under `verify_set_methods.py`, `test262`, and GC
  churn.

See [Phase 505](../phases/505-set-methods.md) for the full test matrix and
notes.
