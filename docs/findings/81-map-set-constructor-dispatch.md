# Finding 81 — Map and Set constructors bypassed their observable adders

**Severity:** medium — ordinary collection contents looked correct, but a
standard, user-observable constructor protocol was skipped for every iterable
Map or Set construction.

**Found by:** the remaining Map/Set case in the Phase 63 iterator corpus and
the built-in-subclass row in `AGENTS.md`. That case replaces
`Map.prototype.set` and `Set.prototype.add`, then makes each replacement throw
to check IteratorClose.

## 1. What was wrong

The constructor loops in `BuiltinsCollections.cpp` called
`CollectionData::set()` directly. They did not perform `Get(map, "set")` or
`Get(set, "add")`, did not require the result to be callable, and did not call
it with the newly constructed collection as receiver.

A subclass compounded the omission. The native parent constructed a base Map
or Set and only later copied its native state into the derived receiver. A
subclass's `set`/`add` therefore could not participate in population even if a
call had been added at that point. An abrupt overridden method could not reach
the iterator machinery, so it could not trigger IteratorClose either.

## 2. What changed

Phase 75 initializes the collection directly on the derived object that the
native construction context supplies, preserving its `new.target` prototype.
It then reads the instance method once before acquiring the source iterator
and invokes that saved method for each item. The standard ordering is now
observable for both `super(iterable)` and
`Reflect.construct(MapOrSet, iterableArgs, NewTarget)`.

The constructors keep their nullish early return. For a real iterable, a
non-callable adder throws before `Symbol.iterator` is read. If an adder call
throws after iteration begins, `js_iterate()` closes the iterator and preserves
the adder's error.

## 3. How it was verified

```bash
make -j2 test
# All 218 engine test suites passed successfully!
# Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

make js-conformance
# iteration.dat 66/78
# JS conformance, full corpus: 1127/1152 (97.8%)  [29 files, 0 crashes, 0 timeouts]

ASAN_OPTIONS=detect_leaks=0 python3 tools/conformance/run_js_conformance.py \
  --binary build-asan/litebird --quiet --timeout 60 --min-pass 1121
# JS conformance, full corpus: 1126/1152 (97.7%)  [29 files, 0 crashes, 0 timeouts]
```

The new engine suite records Node 22.22.3 behavior for Map and Set separately:
subclass and `Reflect.construct` overrides receive the correct `this`, getters
run once before iterator acquisition, nullish inputs do not read an adder,
non-callable adders fail before acquisition, and throwing adders cause one
`return()` call. The pre-existing Node-recorded `iteration.dat` dispatch and
close case now passes; reference mode confirms all 78 expectations with Node.
The ASan/UBSan build passed all 218 unit suites and the complete JavaScript
corpus without a sanitizer report.

## 4. What is still open

The broader built-in-subclass gap remains: Array and Promise factory/species
behavior and `Symbol.hasInstance` are not part of this fix. Weak collections,
and exact built-in iterator prototype/identity behavior, are separate tasks.

See [Phase 75](../phases/75-map-set-constructor-dispatch.md) for the
construction path and regression coverage.
