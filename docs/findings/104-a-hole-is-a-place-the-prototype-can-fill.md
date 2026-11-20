# Finding 104 — a hole is a place the prototype can fill

Phase 98. The architecture is in
[`docs/phases/98-a-hole-is-a-place-the-prototype-can-fill.md`](../phases/98-a-hole-is-a-place-the-prototype-can-fill.md).

## 1. What was wrong

Phase 81 taught the engine that an index of an array is either an own property
or a hole, and that everything walking the own keys must skip a hole. Two
things that ask `HasProperty` were taught the wrong half of it, and both were
written by that phase.

`find` and its three relatives skip holes. They are the one family that does
not: the standard reads `O[k]` for every index:

```js
var calls = 0;
[1, , , 4].find(function () { calls++; return false; });
calls    // 4 (the standard); this engine answered 2
```

The other eight ask "is this a hole?" where the standard asks `HasProperty`,
which walks to the prototype:

```js
Object.defineProperty(Array.prototype, "0", { set: function () {}, configurable: true });
[, ].every(function () { return true; })    // true, one call
[, ].indexOf(undefined)                     // 0
```

Both needed something neither of them could fix: an array prototype keeps its
indices in `array_elements`, and `lookup_chain`, `has_property`, the array
branch of `get_property` and `eval_index`'s fast path all answered from the
property map or from the element vector alone. So `Array.prototype[4] = 5` left
`[1, 2, 3][4]` undefined and `4 in [1, 2, 3]` false, and `[, ][0]` was
`undefined` with a value on the prototype.

## 2. What changed

- **`src/js/BuiltinsArray.cpp`** — two questions the standard asks, written
  once: `has_index` (`HasProperty`) and `index_value` (`O[k]`, a hole being
  `undefined` unless the prototype has it). The nine methods that ask
  `HasProperty` use them; the four `find*` call the predicate for every index.
- **`src/js/JsValue.cpp`** — `lookup_chain` and `has_property` look at an array's
  elements for an index, holes excluded; `get_property`'s array branch stops at
  an element and walks on for a hole; `delete_property` erases an index that is
  in the property map and marks a hole only where there is an element.
- **`src/js/BuiltinsObject.cpp`** — `define_from_descriptor`'s dense-elements
  shortcut is skipped when the map holds an accessor on that index.
- **`src/js/JsEval.cpp`** — `eval_index` does not answer a hole from the
  elements; it falls through to `get_member`.

## 3. How it is verified

- `make test` → **261 / 261**; the new suite is
  `a_hole_is_a_place_the_prototype_can_fill` (33 assertions).
- The 12 test262 tests the `AGENTS.md` row names, each run through the runner
  with its harness: `every/15.4.4.16-7-c-i-22.js`,
  `filter/15.4.4.20-9-c-i-22.js`, `map/15.4.4.19-8-c-i-22.js`,
  `some/15.4.4.17-7-c-i-22.js`, `reduce/15.4.4.21-9-c-i-22.js`,
  `reduceRight/15.4.4.22-9-c-i-22.js`, `indexOf/15.4.4.14-9-b-i-21.js`,
  `lastIndexOf/15.4.4.15-8-b-i-21.js`, and the four `find*`
  `predicate-called-for-each-array-property.js`. All failed before, all pass now.
- `built-ins/Array` and `built-ins/Object` through test262: 4,952 → **5,010**,
  58 gained and none lost. The directory list `make test262` walks: 13,654 →
  **13,660**.
- `make js-conformance` 1137/1156, DOM 678/678, wpt 9567/9824 with 0 unlisted,
  URL 1188/1188, XML 1718/1718 — unchanged.
- With `src/js/` reverted and the test kept, the suite fails and names its
  first assertion.

## 4. Two faults found while writing the test

Both are older than Phase 81, both on an *index* of an array, and both are in
this change because the test could not be made to pass without them:

- **Redefining an accessor as a data property was lost.**
  `Object.defineProperty(Array.prototype, "0", { set: f })` then
  `{ value: "p" }` left the accessor in the property map and wrote the value
  into the elements, so the read answered `undefined`.
- **`delete` of an index erased nothing and marked a hole.** With an accessor on
  `Array.prototype[0]`, `delete Array.prototype[0]` returned `true`, the read
  was `undefined`, and `0 in [, ]` stayed `true` — the element was never there,
  and a hole was marked in its place.

## 5. What is not here

- **`length` is not inherited from an array prototype**: `Object.create([7, 8, 9]).length`
  is `3` in Node and `undefined` here. The indices are inherited now; `length`
  is computed for a cell that is an array, and is a separate question. The new
  suite asserts the current answer, with a comment, so a change to it shows up.
- **A descriptor on an existing element is not used.** `Object.defineProperty(a, "1", { get() { return 7; } })`
  puts an accessor in the property map and `a[1]` still reads the element. That
  is the next row of `AGENTS.md`, and it was like that before Phase 81.
