# Phase 556 — Set union gets keys before snapshotting

Phase 505 implemented the seven ES2025 Set methods. Its `union` algorithm made
one deliberate compatibility choice: it copied the receiver immediately after
`GetSetRecord(other)`, then called `other.keys()`, matching Node v22.22.3.
ECMA-262 orders those operations the other way around.

That difference is observable whenever obtaining the iterator mutates the
receiver:

```js
const receiver = new Set(['kept', 'removed']);
const other = {
  size: 1,
  has() { return false; },
  keys() {
    receiver.delete('removed');
    receiver.add('from-keys');
    return ['other'].values();
  }
};

[...receiver.union(other)];
```

The old implementation, and Node v22.22.3, answer
`['kept', 'removed', 'other']`. ECMA-262 requires
`['kept', 'from-keys', 'other']` because `GetIteratorFromMethod` completes
before the receiver's `[[SetData]]` is copied.

## What changed

The existing operations are unchanged; one line moved:

```cpp
SetIteratorRecord keys_iter = get_iterator_from_method(set_rec.set, set_rec.keys);
JsValue result = make_collection(ObjectClass::Set, JsIntrinsics::get().set_proto);
auto result_data = data_of(result, ObjectClass::Set, "Set.prototype.union");
copy_live_entries(*O, *result_data);
```

`get_set_record` still reads and validates `size`, `has`, and `keys` first.
`get_iterator_from_method` then calls `keys` with `other` as its receiver,
requires an object result, and gets its `next` property. Only after those
observable operations does union copy the receiver into a new plain Set. The
existing loop then consumes the iterator and appends absent values.

## Boundary oracle

`tests/suites/556-set-union-keys-before-snapshot.cpp` places mutations on all
three sides of the boundary:

1. `other.keys()` deletes `removed` and adds `from-keys`;
2. the returned iterator's `next` getter adds `from-next-getter`;
3. the first call of that `next` method adds `too-late` and yields `other`.

The required result is:

```
kept, from-keys, from-next-getter, other
```

The receiver itself also contains `too-late`, proving that mutation happened,
but the result does not: iterator stepping begins after the snapshot. The suite
also pins `GetSetRecord` property order, `ToNumber`, and the `this` values of
both `keys` and `next`.

A final-value-only test with ordinary Sets cannot distinguish either order. The
side-effect log and the two receiver views locate the precise algorithm step.

## Bite proof

Restoring Phase 505's original order—`copy_live_entries` before
`get_iterator_from_method`—was compiled and run. The standalone suite aborts at
its exact result assertion. Restoring the fix makes the isolated suite pass.

## Verification

* implementation checkpoint: **585/585** engine suites;
* the Phase 556 suite passes alone after the mutation is restored;
* `python3 tools/ci/check_suites.py` confirms one standalone suite and no new
  `RUN_TEST` line.

Final merged-tree and repository-wide gate results are recorded before landing.

## Files

* `src/js/BuiltinsCollections.cpp` — obtain the union iterator before copying
  the receiver.
* `tests/suites/556-set-union-keys-before-snapshot.cpp` — exact side-effect
  boundary and result-order oracle.

## Still open

* The 21 BigInt-literal Set-method test262 cases still await engine-wide BigInt
  syntax and semantics.
