# Phase 75 — Map and Set constructors dispatch through `set` and `add`

## 1. What was wrong

`new Map(iterable)` and `new Set(iterable)` made a `CollectionData` object and
wrote every item straight into that native data structure. That supplied the
right final entries for the ordinary case, but skipped two observable parts of
the constructors' contract:

- the freshly made object's `set` or `add` property was never read or called;
- a subclass override could neither inspect each item nor throw to stop the
  iteration.

The placement of the allocation hid a related problem. A native `Map` or `Set`
parent originally made a base-prototype object and `super(...)` copied its
state into the subclass receiver after the constructor returned. Even if the
constructor had looked up `set` or `add`, it would have found the base method,
not the override on the object that the subclass is meant to construct.

This was visible to normal JavaScript. An override that counted calls stayed at
zero, a non-callable `set`/`add` did not throw, and an override which threw
could not make the source iterator's `return()` run.

## 2. What changed

`make_collection()` can now initialize the object supplied by
`JsValue::current_derived_this()`. A call reached through `super(...)` keeps
that object's already-selected `new.target.prototype`; a call through
`Reflect.construct(Map, iterable, NewTarget)` has the same object made by the
construction helper before any user code runs. A direct `new Map` or `new Set`
still allocates a normal object at its intrinsic prototype.

With the receiver in hand, each constructor now follows the observable order:

1. if the iterable is `null` or `undefined`, return it without looking up an
   adder;
2. read `map.set` or `set.add` exactly once and require that it is callable;
3. obtain and consume the source iterator;
4. call the saved method with the constructed collection as `this` for every
   entry/value.

The existing `js_iterate()` protocol owns abrupt completion. A thrown getter,
entry read, or adder call escapes its visitor, so its IteratorClose path calls
the source iterator's `return()` while retaining the original thrown error.
No collection-specific close loop was added.

## 3. How it was verified

`js_map_set_constructors_dispatch_to_instance_methods` records Node 22.22.3's
answers for both kinds of collection. It checks subclass overrides, a getter
whose read must precede `Symbol.iterator`, the nullish early return,
non-callable adders that must fail before the iterator is acquired, IteratorClose
when an override throws, and `Reflect.construct` with a prototype that overrides
the adder.

```bash
make -j2 test
# All 218 engine test suites passed successfully!
# Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

python3 tools/conformance/run_js_conformance.py --filter iteration --reference 'node -e'
# JS conformance, full corpus: 78/78 (100.0%)  [1 files, 0 crashes, 0 timeouts]

make js-conformance
# iteration.dat 66/78
# JS conformance, full corpus: 1127/1152 (97.8%)  [29 files, 0 crashes, 0 timeouts]

ASAN_OPTIONS=detect_leaks=0 python3 tools/conformance/run_js_conformance.py \
  --binary build-asan/litebird --quiet --timeout 60 --min-pass 1121
# JS conformance, full corpus: 1126/1152 (97.7%)  [29 files, 0 crashes, 0 timeouts]

python3 tools/conformance/run_test262.py \
  --binary before=/tmp/litebird-main --binary now=build/litebird --timeout 12
# test262 at 2e58f87d8350: 15686 tests run, 14 skipped (modules)
# before 12422 pass; now 12422 pass
# 0 pass in now and not in before; 0 pass in before and not in now
```

The one additional passing `iteration.dat` case is the existing Node-recorded
case which replaces `Map.prototype.set` and `Set.prototype.add`, then verifies
both dispatch and IteratorClose when either replacement throws. Expectations
were rechecked against Node rather than changed to match the engine. The
sanitizer build ran all 218 engine suites and its whole JavaScript corpus
without an ASan/UBSan report. The test262 comparison used a release binary
built from the pre-change `main` commit; it found no passing standard test that
regressed.

## 4. What is still open

This phase intentionally covers only the Map/Set constructor part of the
larger built-in-subclass row. `Array.from`, `Array.of`, array methods,
`Promise.resolve`, `Symbol.species`, and `Symbol.hasInstance` still have their
separate construction/species work. `WeakMap` and `WeakSet` are also outside
this task.

The distinct iterator-object/prototype fidelity work remains open as well:
this change uses the iterator protocol correctly, but does not alter the shape
or identities of built-in iterator objects.
