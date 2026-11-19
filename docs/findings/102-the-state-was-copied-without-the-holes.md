# Finding 102 — the state was copied without the holes

Phase 96. The architecture is in
[`docs/phases/96-a-subclass-keeps-the-holes.md`](../phases/96-a-subclass-keeps-the-holes.md).

## 1. What was wrong

`new Array(3)` makes three holes. `new List(3)` — `class List extends Array {}`
— made three **elements**:

```js
class List extends Array {}
var l = new List(3);
0 in l            // true    (the standard: false)
Object.keys(l)    // 0,1,2   (the standard: none)
```

The two halves of a subclass construction are in different files. The `Array`
constructor is `src/js/BuiltinsArray.cpp`, and it made the holes correctly. The
handing-on of what a built-in parent built is `take_over` in `src/js/JsEval.cpp`,
which copies a cell's state onto the object `new` created so that `this` becomes
that kind of object — and it copied `array_elements` and `klass` and `internal`
and everything else, and did not copy `array_holes`, which Phase 81 added beside
them. So the elements arrived and the shape of them did not.

`Reflect.construct(Array, [3], List)` goes through the same function and was
wrong in the same way. Every other built-in took over cleanly, which is why
nothing else noticed: `Map`, `Set`, `Error`, `Date`, `Promise` and `RegExp`
subclasses all carry their state, and the probe that found this checked them.

This is a fault of Phase 81 (finding 87), shipped by it. The suite it added did
not catch it because every other place that calls a subclass constructor calls
it with no arguments — `new List()`, `new Plain()`, `sub.push(1, 2, 3)` — and a
constructor with nothing to spread never makes a hole.

## 2. What changed

- **`src/js/JsEval.cpp`** — the one line. `take_over` copies the bitmap with
  the elements: `to->array_holes = from->array_holes ? std::make_unique<JsObject::Holes>(*from->array_holes) : nullptr`.
  A copy, not the pointer, so the two cells never share the bits.
- **`tests/test_suite.cpp`** — `array_holes_are_not_elements` gained eleven
  assertions about the subclass: the length argument is holes, `Object.keys`
  sees none, writing one makes an element, a slice carries the holes, sorting
  keeps them where the standard puts them, and `Reflect.construct` with the
  subclass as `new.target` is holes too.

## 3. How it is verified

- `make test` → **248 / 248** suites; the new assertions are in
  `array_holes_are_not_elements`.
- `make js-conformance` 1131 / 1152, `make dom-conformance` 678 / 678,
  `make wpt` 9567 / 9824 with 0 unlisted, URL 1188 / 1188, XML 1718 / 1718 —
  none of them moved.
- With the line removed and the test kept, the suite fails and names the
  assertion: `a subclass length argument is holes: got true, expected false`.
- A probe run through Node.js 22.22.3 and through this engine gives the same
  answers for `Map`, `Set`, `Error`, `Date` and `Promise` subclasses, for
  `new List(3)`, for `List(1, 2, 3)`, for a sort and a slice of a holed
  subclass, and for `Reflect.construct(Array, [3], List)`.

## 4. What is not here

- **The subclass does not survive the array methods.** `list.slice()`,
  `list.map(f)` and `list.filter(f)` are plain `Array`s, and `List.from([1])`
  and `List.of(1)` are too, because there is no `Symbol.species`. That is the
  same row of `AGENTS.md` this finding came from, and it is the larger half of
  it; it is not a hole question and it is not in this change.
- **`Symbol.hasInstance` is not asked**, so `1 instanceof f` ignores a
  `f[Symbol.hasInstance]` the script installed. Also that row, also not this.
- `class M extends Map` and its relatives were checked and are right, but they
  have no test of their own; the probe covered them.
