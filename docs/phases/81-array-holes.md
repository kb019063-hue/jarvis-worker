# Phase 81 — a hole in an array is not an element

Task row: **Language tier 1** — "array-hole semantics". Finding 87.

## 1. What was wrong

An array in Litebird was dense. Every index below `length` was an element, so
`delete a[1]` wrote `undefined` into the slot and called it done:

```js
var a = [1, 2, 3];
delete a[1];
1 in a               // true     — the standard: false
Object.keys(a)       // 0,1,2    — the standard: 0,2
a.forEach(function (v, i) { seen.push(i) })   // 0,1,2 — the standard: 0,2
new Array(3)         // three elements holding undefined — the standard: three holes
```

The standard (ECMA-262 §10.4.2) has no such thing as an element whose value is
`undefined` unless somebody wrote `undefined` there. An index is an **own
property** or it is not, and the array's *length* counts places, not
properties. Everything a script can ask about an array is one question or the
other:

| Asks about own keys (`HasProperty`) | Asks about places (`Get`) |
| :--- | :--- |
| `1 in a`, `hasOwnProperty`, `Object.keys`, `getOwnPropertyDescriptor` | `a[1]` → `undefined` |
| `forEach`, `map`, `filter`, `some`, `every`, `reduce`, `find*` | `includes`, the iterator, spread |
| `indexOf`, `lastIndexOf` | `join`, `toString`, `JSON.stringify` (`null`) |

Litebird answered both questions with "there is an element" and so got both
wrong: the first set should say the index is absent, and the second set should
still see `undefined` there.

`ARCHITECTURE.md` had carried "holes in arrays" in its list of missing language
features since Phase 23, and `ROADMAP.md` listed array-hole semantics as tier 1
of the language work. It was the largest remaining JavaScript gap that did not
need BigInt, `Proxy` or typed arrays first.

## 2. What changed, piece by piece

- **`include/litebird/js/Heap.hpp`** — `JsObject` gained
  `std::unique_ptr<Holes> array_holes`: a bitmap with one bit per place, set
  where the place is a hole. It is a `unique_ptr` so that an array without a
  hole allocates nothing and `JsObject` does not grow — 99.9% of the arrays a
  page makes are in that case. The elements vector is unchanged: a hole keeps a
  dead `undefined` in its slot, so indexing costs exactly what it did before.
- **`include/litebird/js/JsValue.hpp`** — `is_array_hole(index)`,
  `set_array_hole`, `clear_array_hole`, `array_element(index)` (the read, which
  answers `undefined` for a hole) and `array_snapshot()` (every place as a
  value — what the iterator, spread and `Array.from` see).
- **`src/js/JsValue.cpp`** —
  - `get_property` on an index reads through `array_element`, so a hole is
    `undefined`;
  - `set_property` on an index clears the bit: writing a hole, even with
    `undefined`, makes an element of it;
  - `has_own_property` asks the bit, which is what `1 in a` becomes;
  - `delete_property` sets it instead of writing `undefined`;
  - `own_keys` leaves a hole out of `Object.keys`, `Object.getOwnPropertyNames`
    and `JSON.stringify`'s key walk;
  - `length = n` sets the bits of the places it grows by, and clears those it
    drops;
  - an index written past the end marks the places it skips as holes;
  - `to_string` (the array branch of `js_to_string`) joins a hole as empty.
  - `Holes::shift` moves the bits when an insert or an erase moves the places,
    which is what `splice`, `unshift` and `shift` need.
- **`src/js/BuiltinsArray.cpp`** — every method on the right side of the table
  above:
  - `slice`, `splice`, `concat`, `reverse`, `toReversed`, `toSpliced`, `sort`,
    `toSorted`, `copyWithin` and `flat` **carry** the holes into their answer
    (a copy of a hole is a hole);
  - `forEach` (which already asked `has_property` per index, so it came right
    for free), `map`, `filter`, `flatMap`, `some`, `every`, `reduce`,
    `reduceRight`, `find*` and `indexOf`/`lastIndexOf` **skip** them;
  - `includes` and `at` **read** them as `undefined`;
  - `sort` puts holes after every element (and after `undefined`), and the
    comparator is never called with one;
  - `reduce` without a seed starts at the first place that is an element, and
    throws for an array that is nothing but holes;
  - `new Array(n)` makes `n` holes;
  - `fill` writes the holes in its range as elements.
- **`src/js/JsOps.cpp`** — the array iterator reads a hole as `undefined` (so
  spread, `for…of`, destructuring and `Array.from` turn it into an element, as
  the standard's `IteratorNext` does), and `js_iterable_to_vector` copies
  through `array_snapshot()`. The structured-clone writer reads every place the
  same way.
- **`src/js/BuiltinsObject.cpp`** — `getOwnPropertyDescriptor` answers
  `undefined` for a hole; `CreateListFromArrayLike` (`apply`, `Reflect.apply`,
  `Function.prototype.call`) reads through `array_snapshot()`; the
  `structuredClone` built-in keeps a hole a hole.
- **`src/js/Json.cpp`** and **`src/js/BuiltinsJson.cpp`** — both JSON writers
  write a hole as `null`, which is what JSON has instead of a way to leave a
  place out.
- **`tests/test_suite.cpp`** — `array_holes_are_not_elements`.

## 3. How it is verified

- **`make test`** → **235 / 235** suites. The new one,
  `array_holes_are_not_elements`, asserts **60** facts, every one of them a
  comparison made in the page. The same script text was run through this
  engine and through Node.js 22.22.3 before it was written down: 28 lines, the
  same 28.
- **`make js-conformance`** → **1129 / 1152** (was 1128), with 0 crashes and 0
  timeouts. The case that flips is `arrays.dat`'s *delete leaves a hole*
  (`a.length, a[1], 1 in a` → `3 undefined false`), which was written for the
  standard long before the engine agreed with it. The rest of that file's
  array cases were already passing and still pass.
- **`make dom-conformance`** 678 / 678, **`make wpt`** 9567 / 9824 with 0
  unlisted failures, **`make url-conformance`** 1188 / 1188,
  **`make xml-conformance`** 1718 / 1718 — none of them changed.
- The regression is real: with the engine files reverted and the new test kept,
  `./build/test_suite array_holes` fails on its own first assertion
  (`failures.length` is not 0).

## 4. What is still open

- **An array *literal* with a hole still makes an element of it.** `[1, , 3]`
  and `[, ,]` build their elements in `src/js/JsEval.cpp`
  (`NodeKind::ArrayLiteral`), and that file belongs to an open pull request
  (#103, direct-eval semantics). The work here is everything else: the
  representation, `delete`, `new Array(n)`, a grown `length`, `pop`/`shift` past
  a hole, and every method. The one line that changes in `JsEval.cpp` is that
  the `elements.push_back(JsValue::make_undefined())` the parser emits for an
  elision becomes a hole on the array it builds; until it does, `[1, , 3]`
  reports `1 in a === true`. The write-up and the test say so rather than
  pretending the phase is complete. This is the one place the probe of §1 still
  differs from Node.
- **An array-like is not an array.** `Array.prototype.forEach.call({length: 2,
  0: 'a'}, …)` reads through property access and has no holes to skip, which is
  right — the same code path, `has_property` on the receiver.
- **`Object.assign`, destructuring and `Reflect.ownKeys`** are all
  `own_keys`/`get_property` now and so inherit the fix; none of them needed a
  line of their own. `Object.entries`/`Object.values` call the same helpers.
- **A hole in a typed array** is not a thing (the standard has no holes there),
  and typed arrays do not exist yet.
