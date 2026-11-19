# Phase 94 — `Array.from` asks the source for its iterator

Task row: **`Array.from` decides for itself what is iterable**. Finding 100.

## 1. What was wrong

`Array.from` had its own idea of what could be iterated:

```cpp
const bool iterable = source.is_array() || source.type == JsType::String ||
                      (source.is_object_like() && (source.get_property(kIteratorKey).is_function() || …));
```

An array and a string were iterable because of what they *are*, and everything
else only if a lookup happened to find a function. Three things followed:

1. **A source that had lost its iterator was still iterated.** With
   `Array.prototype[Symbol.iterator]` deleted, the language reads an array as
   array-like — `[1, 2]` and `'ab'` are still values with a `length` — and this
   engine threw `TypeError: not iterable` for each:

   ```js
   delete Array.prototype[Symbol.iterator];
   Array.from([1, 2]);   // TypeError; the language says [1, 2]
   ```

2. **A source that had one was not asked.** `Array.from(3)` was `[]` even with
   `Number.prototype[Symbol.iterator]` installed, because a number is neither an
   array nor a string nor object-like, so neither branch ran.
3. **Everything was collected before anything was mapped.** A mapper that threw
   on the third value left the iterator open, and an iterator with no end was
   read to the end of it — which, for an endless one, is where the process's
   wall limit is:

   ```js
   Array.from(endless(), function (v) { if (v === 3) throw new RangeError('stop'); });
   // hung: the first v the mapper saw was after the iterator had finished
   ```

## 2. What changed

`Array.from` is now the standard's order of operations, in
`src/js/BuiltinsArray.cpp`:

- **The mapper is checked first** (`IsCallable`), as step 3 of the algorithm,
  before anything is asked of the source.
- **The source is asked**: `GetMethod(items, @@iterator)` —
  `js_iterator_method()`, which is the language's GetMethod and already knows
  that a primitive is asked through its prototype, that `undefined` and `null`
  mean "no iterator", and that anything else which is not a function is a
  `TypeError`.
- **With a method**, the values are walked by `js_iterate()` and mapped as each
  one arrives: the mapped value is computed inside the visit, so a mapper that
  throws ends the walk with the iterator closed — `js_iterate` calls `return()`
  when the visitor throws, and no further value is asked for. An endless
  iterator now costs one value per mapped element.
- **Without one**, the source is read as array-like: `ToLength(Get(items,
  "length"))` — negative, fractional, `NaN` and a missing length are none, a
  string is its number, and the result is clamped to 2⁵³−1 — then each index is
  a property read, which is one UTF-16 code unit for a string and `undefined`
  for a hole.
- **Both branches map**: `mapfn` is called with `(value, index)` and `thisArg`,
  the index counting from zero.

## 3. How it is verified

- `make test` → **248 / 248** suites, the new one being
  `array_from_asks_the_source_for_its_iterator`: 27 assertions compared inside
  the page, over the cases above — an array and a string with and without their
  prototype's method, a number given one, `{ next() }` only, `ToLength`'s five
  edge cases, a nullish source and a non-callable method, a getter for
  `Symbol.iterator` read exactly once, a mapper that throws on the third value
  closing an endless iterator at 3, a non-callable `return()` not replacing the
  thrown error, `thisArg` and the indices, and the result's arrayness.
- `make js-conformance` → **1137 / 1156** in 29 files, 0 crashes and 0
  timeouts, up from **1131 / 1152**. Five cases that failed now pass, two of
  them the two the row named (`Array.from reads what has no iterator as
  array-like`, `Array.from asks a number that was made iterable`); four cases
  were added to `tests/js-conformance/iteration.dat`, each expectation recorded
  from Node.js 22.22.3.
  - `iteration.dat` 69/78 → **75/82**.
- `make dom-conformance` 678 / 678, `make url-conformance` 1188 / 1188,
  `make xml-conformance` 1718 / 1718, `make wpt` 9567 / 9824,
  `make conformance` 1967 / 1973, `make diff-trees` 0 differing trees,
  `make diff-xml` 0 differing documents, `make deep` 0 failed: unchanged.
- `make asan` → 248 suites clean under ASan + UBSan.
- `python3 tools/ci/check_numbers.py` → 0 failures.

## 4. What is still open

- **The result is always a plain array.** `Array.from.call(class extends Array,
  …)` makes an `Array`, where the standard constructs `this`; that is the same
  hole as `Symbol.species` and `instanceof`'s `Symbol.hasInstance`, and the row
  for it is "several built-ins still do not make the subclass they were called
  on".
- **No fast path.** A million-element array is read through
  `js_iterate`'s protocol rather than by walking the elements in place;
  `js_iterates_as_builtin()` is the question that would justify the direct read,
  and `Array.from` does not ask it.
- **`Array.fromAsync` does not exist.**
- **`typeof [][Symbol.iterator]` is still a function named `values`** and the
  iterator interface is still the engine's own (see the row on what an iterator
  is not).
