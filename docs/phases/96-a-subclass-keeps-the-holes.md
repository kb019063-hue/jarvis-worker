# Phase 96 — a subclass keeps the holes `super()` made

Task row: **Several built-ins still do not make the subclass they were called
on** — the half that is not `Symbol.species`. Finding 102.

## 1. What was wrong

`new Array(3)` has made three holes since Phase 81. `new List(3)`, for
`class List extends Array {}`, made three elements:

```js
class List extends Array {}
var l = new List(3);
l.length          // 3
0 in l            // true    — the standard: false
Object.keys(l)    // 0,1,2   — the standard: none
```

The reason is where the two halves live. The `Array` constructor
(`src/js/BuiltinsArray.cpp`) made the holes. What a built-in parent built is
then handed to the object `new` made, by `take_over` in `src/js/JsEval.cpp`, so
that `this` becomes that kind of object; that copy took `array_elements`,
`klass`, `internal`, the property table, the exotic hook and the DOM node, and
did not take `array_holes`. The elements arrived and the shape of them did not.

It is the same function `Reflect.construct(Array, [3], List)` goes through, and
it was wrong in the same way. Every other built-in — `Map`, `Set`, `WeakMap`,
`Error`, `Date`, `RegExp`, `Promise` — took over correctly, so the fault was
invisible from everywhere except an `Array` subclass constructed with a length.

The suite Phase 81 added did not catch it because every other test that calls a
subclass constructor calls it with **no arguments** (`new List()`,
`new Plain()`, `sub.push(1, 2, 3)`), and a constructor with nothing to spread
never makes a hole.

## 2. What changed

- **`src/js/JsEval.cpp`**, `take_over` — the elements' holes travel with the
  elements. A `std::make_unique<JsObject::Holes>(*from->array_holes)` copy, so
  no two cells share the bits.
- **`tests/test_suite.cpp`** — `array_holes_are_not_elements` grew eleven
  assertions on the subclass: the length argument, `Object.keys`, writing one
  back to an element, a slice, a sort, and `Reflect.construct` with the
  subclass.

## 3. How it is verified

- **`make test`** → **259 / 259** suites.
- **`make js-conformance`** 1137 / 1156, **`make dom-conformance`** 678 / 678,
  **`make wpt`** 9567 / 9824 with 0 unlisted, **`make url-conformance`**
  1188 / 1188, **`make xml-conformance`** 1718 / 1718: none moved.
- With the line reverted and the test kept, the suite fails and names the
  assertion (`a subclass length argument is holes: got true, expected false`).
- A page of subclass questions — `List(3)`, `List(1, 2, 3)`, `new List(3)`
  sorted, sliced and written to, `Reflect.construct(Array, [3], List)`,
  `Array.apply(null, new Array(3))`, and subclasses of `Map`, `Set`, `Error`,
  `Date` and `Promise` — gives the same lines through Node.js 22.22.3 and
  through this engine.

## 4. What is still open

- **The array methods do not make the subclass.** `list.slice()`, `list.map(f)`,
  `list.filter(f)` and `List.from`/`List.of` answer plain arrays, because
  there is no `Symbol.species` and `Array.from`/`Array.of` do not read
  `this` as a constructor. That is the rest of the same `AGENTS.md` row and it
  is a change of its own (a `Symbol.species` on `Array`, `Map`, `Set`, `Promise`
  and the typed-array prototypes, and `ArraySpeciesCreate` in every method that
  returns an array).
- **`instanceof` does not ask `Symbol.hasInstance`**, so a script cannot
  override it. The third part of the row, also its own change.
- **`class X extends Date` with a `super()` that returns nothing** is right;
  the probe covered it. Nothing else about built-in subclassing was found while
  looking.
