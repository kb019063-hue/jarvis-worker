# Finding 142 — a pattern read the vector where the iterator read the place

Phase 137. The architecture and the measurements are in
[`docs/phases/137-a-pattern-reads-the-place-not-the-vector.md`](../phases/137-a-pattern-reads-the-place-not-the-vector.md).

## 1. What was wrong

This engine keeps an array's elements in a dense vector and everything else
about its places in the property map. That is an implementation detail with
one rule attached: **a place is what `a[i]` reads**, and every part of the
engine that walks an array has to ask that way or disagree with the rest of
it. Phase 81 made a hole a hole, Phase 98 made a hole a place the prototype
can fill, Phase 108 made an accessor on a place the place's property, and
Phase 116 made the methods that move and copy places ask the properties. Each
of them brought more of the engine onto the rule.

`IterationCursor` — the object a destructuring pattern reads its source with
since Phase 102 — was left out. Its array mode was:

```cpp
const auto& elements = m_source.object_val->array_elements;
if (m_index < elements.size()) return m_source.array_element(m_index++);
```

So a pattern read the vector and not the places. `var [x] = a` with a getter
at index 0 bound `undefined` and never called the getter, where `for (x of a)`
and `[...a]` in the same engine called it and bound `"g"`; a place
`Array.prototype` filled was `undefined` to it; and a nested pattern over such
a place threw `TypeError`, because the place it walked was `undefined`.

It is worth saying why nothing caught it. The corpora destructure arrays whose
places are all elements: `tests/js-conformance` has no pattern over an accessor
and no pattern over a hole a prototype fills, and test262's 651 destructuring
tests are the same. The suite that exists because of it was written for it: 13
checks, 9 of which fail without the one-line change.

## 2. What changed

`IterationCursor::pull()` reads an element place with `js_array_get`, the same
function `a[i]`, `join`, `includes`, `at`, the array's own iterator and
`JSON.stringify` read with. An element is still the vector read it was; a hole
asks the question `js_array_holes_read_undefined` answers from a remembered
prototype chain, and only then looks the index up by name.

The comment above the "not iterable" throw in the same constructor was also
wrong, and was corrected: it said `for…of` accepts an object that only has
`next`, which this engine stopped doing before this phase. Measured today:
`for (var x of { next() {} })`, `[...{ next() {} }]` and `for (var x of {})`
are `TypeError`s here and in Node.js 22, and `Array.from({ next() {} })` is `[]`
in both.

## 3. How it was verified

- the 13 checks of the new suite, in Node.js 22 and in this build: the same
  text, all thirteen; nine differ on a build of `main` at `e1414f6`.
- `make test` 297 / 297 (296 on `main`), 114 cross-origin checks.
- the corpora unchanged: js-conformance 1143 / 1158, DOM 678 / 678, tree
  construction 1973 / 1973, URL 1188 / 1188, WPT 9567 / 9824 with no unlisted
  failure.
- test262, two builds: the four destructuring directories 591 → 591 of 651,
  and `built-ins/Array` with `for-of` and `expressions/array` 3192 → 3192 of
  3884, 0 gained and 0 lost. The 15 tests whose *category* moves between the
  builds are the `length`-near-2^53 family, killed or timed out in both.
- time: ten-place patterns 200 → 197 ms over 200,000 iterations on a dense
  array, 190 → 201 ms where every place is a hole.

## 4. What is still open

- **An inherited `length`**: `Object.create([7, 8, 9]).length` is `undefined`.
  The corpus case is `tests/js-conformance/iteration.dat` line 729.
- **A data property on an index of an array keeps no attributes.**
- **`toReversed`/`toSorted` keep a hole a hole**, and `toReversed` reads the
  places in the other order.
- **`Object.getPrototypeOf(gen)[Symbol.iterator]` is `undefined`** here and a
  function in Node.js — a generator's prototype is not on `%IteratorPrototype%`.
  Found while probing iteration for this phase, written up nowhere yet.
