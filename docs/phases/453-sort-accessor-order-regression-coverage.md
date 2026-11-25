# Phase 453 — Array.prototype.sort already preserves indexed accessor order

Task: resolve the remaining `sort` item in `AGENTS.md` row 1448 by reproducing its named test262 cases before changing behavior. Finding 453.

## 1. The report and the reproduction

The row and the reports for Phases 116 and 265 said that `sort` still failed `precise-comparefn-throws.js` and `precise-prototype-element.js`, while `precise-prototype-accessors.js` passed. On the current base, all three named tests pass:

```text
python3 tools/conformance/run_test262.py --jobs 2 --show 20 \
  built-ins/Array/prototype/sort/precise-comparefn-throws.js \
  built-ins/Array/prototype/sort/precise-prototype-element.js \
  built-ins/Array/prototype/sort/precise-prototype-accessors.js

3 tests run, 0 skipped
litebird  3 pass (100.0%)
```

The same three sources, with test262's `assert.js` and `sta.js`, also exit successfully under Node.js v22.22.3. The complete `built-ins/Array/prototype/sort` directory runs 54 tests: 49 pass and five fail for unsupported BigInt syntax or missing `BigInt64Array`. None of those five is one of the accessor-order cases.

## 2. What the code does

The two visible `sort` registrations in `src/js/BuiltinsArray.cpp` have the same implementation; the later registration replaces the earlier one. For an array with a hole or an indexed property on its prototype, `dense_readable` rejects the vector fast path. The generic path takes `length` once, asks `HasProperty` and then `Get` for each index in ascending order, sorts the collected values, and only then writes them back with `Set` and deletes the remaining places. A comparator throw exits before either writeback loop.

That is the order the three tests exercise:

- `precise-prototype-element.js`: an inherited writable data value is read as an item, sorted, and shadowed by the writeback; the final hole remains at the end.
- `precise-prototype-accessors.js`: the inherited getter runs during collection, then the setter receives the sorted value at index 2; the array does not create an own property there.
- `precise-comparefn-throws.js`: the getter runs before comparison, and a comparator throw prevents all writeback and deletion.

No runtime defect reproduced on this branch, so no implementation change was justified.

## 3. Regression suite

`tests/suites/453-array-sort-indexed-accessor-order.cpp` locks all three behaviors in a standalone engine suite. It asserts the inherited-data result and hole, the getter/setter event order, and the unchanged own-property shape after the comparator throws. The indexed prototype property is removed before each script returns, so the cases are isolated within the page.

## 4. Verification

- `make -j2 all`: passed.
- The three named test262 cases: **3/3 passed**.
- All of `built-ins/Array/prototype/sort`: **49/54 passed**; the five unrelated failures require unsupported BigInt syntax or `BigInt64Array`.
- `./build/test_suite`: **500/500 engine suites passed**, including this suite. The count includes two tests in the pre-existing tracked `tests/suites/tmp-unforgeable.cpp`.
- The global `python3 tools/ci/check_suites.py` check still reports the pre-existing `tmp-unforgeable.cpp` name/two-TEST shape violations; this suite itself is one `TEST` in a numbered file with no includes.

## 5. Resolution

This phase corrects the stale status rather than changing `BuiltinsArray.cpp`: the named cases already pass on the branch. `AGENTS.md` and the earlier reports now point here, and the new suite keeps the observed order covered by `make test`.
