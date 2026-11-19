# Finding 100 — `Array.from` decided for itself what was iterable

Phase 94. The architecture is in
[`docs/phases/94-array-from-asks-the-source.md`](../phases/94-array-from-asks-the-source.md).

## 1. What was wrong

`Array.from` did not ask the source whether it could be iterated. It asked
what kind of value it was:

```cpp
const bool iterable = source.is_array() || source.type == JsType::String ||
                      (source.is_object_like() && source.get_property(kIteratorKey).is_function());
```

so an array and a string were iterable by identity, and nothing else was asked
unless it was an object with a function under the iterator key. Both halves of
that are wrong, and the second half of the defect was in what happened after:

- `delete Array.prototype[Symbol.iterator]; Array.from([1, 2])` was a
  `TypeError`, where the language reads the array as array-like and gives
  `[1, 2]`; the same for `'ab'`. Two cases of `iteration.dat`.
- `Number.prototype[Symbol.iterator] = …; Array.from(3)` was `[]`: a number is
  not object-like, so neither branch ran and the iterator was never asked. One
  case of `iteration.dat` said so.
- the values were collected before any of them was mapped, so a mapper that
  threw on the third value never closed the iterator, and an iterator with no
  end was followed to the end of it. Node stops at the throw.

## 2. What changed

- **`src/js/BuiltinsArray.cpp`** — `Array.from` is the standard's order:
  callable check for `mapfn`, then `GetMethod(@@iterator)` via
  `js_iterator_method()`, then either `js_iterate()` with the mapping inside the
  visit (so a throwing mapper closes the iterator and the walk stops there) or
  `ToLength(Get(items, "length"))` and one property read per index. Both
  branches call the mapper with `(value, index)` and `thisArg`.
- **`tests/js-conformance/iteration.dat`** — four cases added, expectations
  recorded from Node.js 22.22.3: a mapper that throws closing an endless
  iterator at the value it threw on; a string that lost its iterator read as
  array-like (`a,b | 3`, the 3 being the UTF-16 length of `'a😀'`); `ToLength`
  (`-1`, `'2'`, `2.9`, `undefined`) with the mapper applied to an array-like;
  and a non-callable iterator (`TypeError`) beside a `null` one (read as
  array-like).
- **`tests/test_suite.cpp`** — `array_from_asks_the_source_for_its_iterator`,
  27 assertions.
- **`README.md`** — 247 suites → 248; the JavaScript corpus 1131 / 1152 →
  1137 / 1156.

## 3. How it is verified

- `make test` → **248 / 248** suites.
- `make js-conformance` → **1137 / 1156 (98.4%)** in 29 files, up from
  1131 / 1152; `iteration.dat` 69/78 → **75/82**. Five cases pass that did not,
  four of them written for this phase.
- `make dom-conformance` 678 / 678, `make url-conformance` 1188 / 1188,
  `make xml-conformance` 1718 / 1718, `make wpt` 9567 / 9824 with 0 unlisted
  failures, `make conformance` 1967 / 1973, `make diff-trees` 0 differing
  trees, `make diff-xml` 0 differing documents, `make deep` 0 failed — all
  unchanged.
- `make asan` → 0 sanitizer reports over the 248 suites and the corpora.

## 4. What is still open

- **`Array.from` makes a plain `Array`, not a `this`.** `class List extends
  Array {}; List.from([1])` is an `Array` (the row on the subclass a built-in is
  called on, unchanged by this phase).
- **No direct read of a plain array's elements.** The values are taken through
  the iteration protocol; `js_iterates_as_builtin()` exists for exactly the case
  that would allow the fast path and `Array.from` does not consult it yet.
- **`Array.fromAsync` is absent**, and so are the typed-array constructors
  (`Array.from(new Set([1]))` works; `Uint8Array.from` cannot).
- The seven `iteration.dat` failures left are about the iterator's *identity*
  (`Symbol.iterator` being the string `@@iterator`, `arguments` having no
  iterator of its own, an object whose prototype is an array, and array
  destructuring asking the array), not about `Array.from`.
