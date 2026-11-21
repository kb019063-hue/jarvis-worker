# Phase 133 — `length` is inherited from an array prototype

Task row: **`length` is not inherited from an array prototype.**
`Object.create([7, 8, 9]).length` is `undefined` where the standard has `3`.
Finding 138.

## 1. What was wrong

An array keeps two things in `array_elements` instead of in its property map:
its indices, and its `length`. Phase 98 taught `lookup_chain` to hand out the
indices — `Object.create([7, 8, 9])[1]` is `8` — but only the indices. A read
that fell through a chain and landed on an array found its elements and not its
length:

```js
var o = Object.create([7, 8, 9]);
o[1]        // 8   (right, since Phase 98)
o.length    // undefined; the standard answers 3
```

The cause was in two functions, and both had the same shape. `get_property`
computes `length` from `array_elements.size()`, and it does so **only for the
cell that is itself an array** — a read of an array's own `length` never needed
the chain, so the chain never got a case for it. `lookup_chain` walks the
prototypes and answers an array's *indices* out of `array_elements`, with the
hole check, but had no branch for `length`. `Object.create([7, 8, 9])` is not
itself an array, so its read went to `lookup_chain`, reached the array, and
stopped one answer short.

`has_property` had the identical gap: `"length" in Object.create([7, 8, 9])`
was `false` where the standard has `true`. That one was not in the row — it
turned up while checking the fix, and it is what makes the pair below the right
pair to hold together.

## 2. What changed

- **`src/js/JsValue.cpp`** — `lookup_chain` has a `length` branch beside its
  index branch: when the object it has reached is an array and the name is
  `length`, the answer is `array_elements.size()`. `has_property` answers `true`
  for the same pair in its own walk. The computed answer for an array's *own*
  `length` in `get_property` is untouched — that path never changed and still
  returns before the chain is asked.
- **`tests/test_suite.cpp`** — the assertion in
  `a_hole_is_a_place_the_prototype_can_fill` that recorded the wrong answer
  (`its length is not inherited here`, expecting `undefined`) is changed to the
  language's answer, with the descriptor and `in` questions beside it, because
  the row says to change it and it is what notices a regression. A new suite
  `length_is_inherited_from_an_array_prototype` covers the read, its being
  inherited and not owned, the two-level walk, an array prototype that grows and
  shrinks after the reader was made, and the cases that must not change.

Nothing else moved. `Reflect.getOwnPropertyDescriptor`, `Object.keys`,
`for-in`, `JSON.stringify` and the array branch of `get_property` never read
this answer, and their results are the same as before — which is the point:
`length` is inherited and still not an own property.

## 3. How it is verified

- **The standard's own answers**, 15 questions run through Node.js 22.22.3 and
  through this engine, identical line for line: `o.length`, `o[1]`,
  `"length" in o`, `o.hasOwnProperty("length")`,
  `Object.getOwnPropertyDescriptor(o, "length")` (`undefined`),
  `Object.create([]).length` (`0`), `JSON.stringify(o)` (`{}`),
  `Object.keys(o)` (`[]`), `({}).length` (`undefined`), `[4, 5].length` (`2`),
  an array's own `length` still own, `"abc".length` (`3`), a function's arity,
  two levels of prototype, and an array with a null prototype (`0`).
- **`make test`** → **286 / 286** suites. The new suite is registered and runs;
  it was checked that it is in the binary and in the runner's list, because a
  `TEST()` that is never `RUN_TEST()` compiles and passes silently, and that
  happened once while writing this one.
- **The amended assertion fails on the old source.** With `src/js/JsValue.cpp`
  reverted and the test kept, `a_hole_is_a_place_the_prototype_can_fill` fails
  on the changed line, which is what it is for.
- **It is a read, so it was timed** (the row's own note). 100,000 reads of an
  inherited `length` against 100,000 reads of a plain array's `length`:

  | | inherited `length` | array's own `length` |
  |---|---|---|
  | before the fix | 30.6 – 31.0 ms (`sum` was `NaN`) | 24.1 – 24.6 ms |
  | after the fix | 26.4 – 27.5 ms (`sum` 1000000) | 24.2 – 26.4 ms |

  Three runs each. The read was not made slower: the walk that answers it was
  already being walked, and it answers instead of falling off the end. The
  "before" figure is not a saving — it is the same cost for a wrong answer,
  and the `NaN` is the failure, not a speed.

## 4. What is still open

- **`length` of a *typed array* through the chain** is answered by the typed
  array's own exotic path, not by this branch, and was not touched. It wants
  the same question asked of it before this is called done for every array-like
  with elements.
- **A proxy** has its own trap for `length`; `lookup_chain` is not reached for
  one, so nothing changed there.
- The neighbouring row — **an array method copies an array-like before it
  looks** — is being worked on elsewhere and is not part of this change.
