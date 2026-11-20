# Phase 98 — a hole is a place the prototype can fill

Task row: **Phase 81 made a hole a hole, and twelve methods of `Array.prototype`
treat one wrongly since: 14 test262 tests that passed before it do not pass.**
Finding 104.

## 1. What was wrong

Phase 81 made an index of an array either an own property or a hole, and the
copying methods right. Two things that ask `HasProperty` kept answering from the
own elements alone, and both are mine.

**The `find` family skipped holes.** `find`, `findIndex`, `findLast` and
`findLastIndex` are the one family that does *not* skip one: the standard reads
`O[k]` for every index and calls the predicate with it, so a hole is an index
the callback sees, holding `undefined`:

```js
var calls = 0;
[1, , , 4].find(function () { calls++; return false; });
calls     // 4 (the standard); this engine answered 2
```

**The other eight asked "is a hole?" where the standard asks `HasProperty`**,
which goes on to the prototype. An index `Array.prototype` has makes the index
one the callback must be called for — the standard's own tests use a
setter-only accessor, which exists and reads `undefined`:

```js
Object.defineProperty(Array.prototype, "0", { set: function () {}, configurable: true });
[, ].every(function () { return true; })   // true, callback called once (the standard)
[, ].indexOf(undefined)                    // 0 (the standard)
```

Behind both was a smaller fault that neither could work without. An array
prototype keeps its indices in `array_elements`, and four places looked them up
in the property map only, or answered a hole from the elements they had just
been told were not there:

- `lookup_chain` — so `Array.prototype[4] = 5` did not reach `[1, 2, 3][4]`.
- `has_property` — so `4 in [1, 2, 3]` was `false` with `Array.prototype[4] = 5`.
- the array branch of `get_property` — it returned for any index, hole or not,
  and never asked the prototype.
- `eval_index`'s fast path in `JsEval.cpp` — `array[number]` returned the
  element vector's slot, so `[, ][0]` was `undefined` whatever the prototype had.

Two more faults came out of writing the test, both older than Phase 81 and both
in `Object.defineProperty` and `delete` on an *index* of an array:

- a data descriptor on an index that already had an **accessor** was written
  through the dense-elements shortcut, leaving the accessor in the property map
  and the value beside it, so redefining was lost;
- `delete` of an index returned before erasing an accessor from the property
  map, and marked a hole, so `0 in [, ]` stayed `true` after
  `delete Array.prototype[0]`.

## 2. What changed

- **`src/js/BuiltinsArray.cpp`** — `has_index()` (the `HasProperty` the callback
  methods ask, through `has_property`) and `index_value()` (`O[k]`, holes
  included). `every`, `some`, `map`, `filter`, `flatMap`, `reduce`,
  `reduceRight`, `indexOf` and `lastIndexOf` ask the first and read the second.
  `find`, `findIndex`, `findLast` and `findLastIndex` call the predicate for
  every index and read `O[k]`.
- **`src/js/JsValue.cpp`** — `lookup_chain` and `has_property` look at an array
  prototype's elements, holes excluded; the array branch of `get_property` stops
  only at an index that is an element, and otherwise walks on; `delete` erases
  a mapped index and marks a hole only when there was an element.
- **`src/js/BuiltinsObject.cpp`** — the dense-elements shortcut in
  `define_from_descriptor` is not taken when the property map has an accessor on
  the index.
- **`src/js/JsEval.cpp`** — `eval_index`'s fast path answers only an index that
  is not a hole; a hole goes through `get_member`, as `HasProperty` says.
- **`tests/test_suite.cpp`** — `a_hole_is_a_place_the_prototype_can_fill`, a
  new suite: 33 assertions, the find family, the eight callback methods with a
  setter-only accessor and with a data property, `in`, `hasOwnProperty`,
  `Object.keys`, the redefine and delete cases, and what an object whose
  prototype is an array inherits.

## 3. How it is verified

- **`make test`** → **261 / 261** suites.
- **`make js-conformance`** 1137 / 1156, **`make dom-conformance`** 678 / 678,
  **`make wpt`** 9567 / 9824 with 0 unlisted, **URL** 1188 / 1188, **XML**
  1718 / 1718: none moved.
- **The 12 test262 tests the row names pass**, run one at a time through the
  runner with the harness:
  `every/15.4.4.16-7-c-i-22.js`, `filter/15.4.4.20-9-c-i-22.js`,
  `map/15.4.4.19-8-c-i-22.js`, `some/15.4.4.17-7-c-i-22.js`,
  `reduce/15.4.4.21-9-c-i-22.js`, `reduceRight/15.4.4.22-9-c-i-22.js`,
  `indexOf/15.4.4.14-9-b-i-21.js`, `lastIndexOf/15.4.4.15-8-b-i-21.js`, and the
  four `find*` `predicate-called-for-each-array-property.js`. They failed before
  and pass now.
- **`built-ins/Array` and `built-ins/Object` through test262**: 4,952 passing
  before this change, **5,010** after — 58 gained, none lost.
- With `src/js/` reverted and the test kept, the suite fails.
- A page of 11 questions compared line for line through Node.js 22 and this
  engine is identical.

## 4. What is still open

- **`length` is not inherited from an array prototype.** With
  `Object.create([7, 8, 9])`, the standard answers `3` for `.length` and this
  engine answers `undefined`. The indices are inherited now (this change); the
  `length` is a different question, because `length` is computed from
  `array_elements` rather than stored, and only for a cell that is an array.
  Asserted as it is in the new suite so a change shows up.
- **An accessor on an array index is not used by a read.** `Object.defineProperty(a, "1", { get() { return 7; } })`
  on a real element puts it in the property map, where the array branch of
  `get_property` does not look; `a[1]` is the element. That is the row after
  this one in `AGENTS.md`, and it was like that before Phase 81.
- **An array is still read by `has_own_property` before the map**, so a
  descriptor on an existing element is not reported. Same row.
