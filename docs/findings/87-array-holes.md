# Finding 87 — an array had no holes

Phase 81. The architecture is in
[`docs/phases/81-array-holes.md`](../phases/81-array-holes.md).

## 1. What was wrong

`delete a[1]` wrote `undefined` into the place and left it an element. So did
`new Array(3)`, and so did `a.length = 5`. There was no way for a script to
make a hole, and no way to see one:

```js
var a = [1, 2, 3]; delete a[1];
1 in a            // true   (the standard: false)
Object.keys(a)    // 0,1,2  (the standard: 0,2)
a.forEach(f)      // f is called for index 1 (the standard: it is not)
```

The standard's array is a list of *places*, each of which either has a property
or does not. Litebird's was a vector of values, and every place had one. The
difference is visible from about thirty built-ins, in two opposite directions:
the methods that ask for the property (`forEach`, `map`, `indexOf`,
`Object.keys`) must not find a hole, and the ones that read the place
(`includes`, `join`, the iterator, `JSON.stringify`) must find `undefined`
there.

It had been on the list since Phase 23 (`ARCHITECTURE.md`, "Missing language
features … and holes in arrays (`delete a[1]` leaves `undefined`, and `1 in a`
stays true)") and in `ROADMAP.md` tier 1. A probe against Node.js found 15 of
20 questions wrong on the first page.

## 2. What changed

- **`include/litebird/js/Heap.hpp`** — the bits. `JsObject::array_holes` is a
  `unique_ptr<Holes>` with one bit per place, allocated only by an array that
  has a hole; the elements vector is unchanged, so an index costs what it did
  and `JsObject` does not grow for the common array.
- **`include/litebird/js/JsValue.hpp`**, **`src/js/JsValue.cpp`** — the five
  questions: `is_array_hole`, `set_array_hole`, `clear_array_hole`,
  `array_element` (read), `array_snapshot` (every place as a value). With them,
  `get_property`, `set_property`, `has_own_property`, `delete_property`,
  `own_keys`, `length = n`, an index written past the end, and the array branch
  of `js_to_string`.
- **`src/js/BuiltinsArray.cpp`** — the methods. Nine carry a hole into their
  answer, twelve skip it, two read it as `undefined`, `sort` puts holes last
  and never gives one to the comparator, `reduce` seeds from the first element
  there is, `new Array(n)` makes holes, `fill` writes one in.
- **`src/js/JsOps.cpp`** — the iterator, and so spread, `for…of`,
  destructuring, `Array.from` and the message-clone writer: every place is
  read, so a hole travels as `undefined`.
- **`src/js/BuiltinsObject.cpp`**, **`src/js/Json.cpp`**,
  **`src/js/BuiltinsJson.cpp`** — the descriptor (`undefined` for a hole), the
  list of a `fn.apply(this, a)` (`undefined` for a hole), `structuredClone`
  (keeps the hole) and both JSON writers (`null` for a hole).
- **`tests/test_suite.cpp`** — `array_holes_are_not_elements`.

## 3. How it is verified

- `make test` → **235 / 235** suites (`array_holes_are_not_elements` asserts
  67 facts, all of them compared in the page; the same text run through
  Node.js 22.22.3 gives the same 28 lines).
- `make js-conformance` → **1131 / 1152** (was 1130): `arrays.dat`'s *delete
  leaves a hole* now passes.
- `make dom-conformance` 678 / 678, `make wpt` 9567 / 9824 with 0 unlisted,
  `make url-conformance` 1188 / 1188, `make xml-conformance` 1718 / 1718.
- With the engine reverted and the test kept, the test fails.

## 4. What is not here

Nothing of the ROADMAP row is left. An earlier draft left the array *literal*
(`[1, , 3]`) out because `src/js/JsEval.cpp` belonged to open pull request #103;
that merged first, so the literal is in this work as well. `Proxy` and typed
arrays do not exist yet, so a hole seen through either is not a question this
engine can be asked.
