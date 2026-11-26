# Finding 453 — The sort accessor-order report was stale

**Severity:** tracking error, not a reproduced runtime violation. The open row claimed that `Array.prototype.sort` failed two precise test262 cases, but all three cases named by the row pass on this branch.

**Found by:** taking the next open item in `AGENTS.md` row 1448 and running its exact test262 cases before editing the implementation.

## Reproduction

```text
python3 tools/conformance/run_test262.py --jobs 2 --show 20 \
  built-ins/Array/prototype/sort/precise-comparefn-throws.js \
  built-ins/Array/prototype/sort/precise-prototype-element.js \
  built-ins/Array/prototype/sort/precise-prototype-accessors.js

3 tests run, 0 skipped
litebird  3 pass (100.0%)
```

A harness-level sanity check was also made: `assert.sameValue(1, 2)` exits nonzero with the expected test262 assertion error, so a passing result means the tests' assertions ran.

## Why the cases pass

The generic `sort` path in `src/js/BuiltinsArray.cpp` first collects present values with `HasProperty` and `Get` in ascending index order. Its `dense_readable` guard keeps arrays with holes or inherited indexed properties out of the vector fast path. Sorting completes before the `Set` writeback starts; if a comparator throws, the write and delete loops are never reached. On successful sorting, indexed writes go through `set_place_or_throw`, so an inherited setter is called and an inherited writable data property is shadowed as ordinary `Set` requires.

The three test262 cases observe those separate points: the inherited data value is included and sorted, a getter precedes the setter during successful writeback, and the comparator-throw case observes the getter but no later mutation. No discrepancy with the expected property order was reproduced.

## Resolution

No engine code changed. The standalone suite `tests/suites/453-array-sort-indexed-accessor-order.cpp` now keeps these three behaviors covered. `AGENTS.md` row 1448 and the earlier Phase 116, finding 125, Phase 265, and finding 269 reports record that the previously open item has been retested and closed by Phase 453.

The full `built-ins/Array/prototype/sort` directory has five other failures on this engine: one uses BigInt literal syntax and four require `BigInt64Array`. They are outside this indexed-accessor finding.
