# Finding 145 — `Symbol.species` was never asked

Phase 140. Status: **found and fixed for Array** (the static and instance
methods that make an array, and the species getters on `Array`, `Map`, `Set`,
`Promise`). **Still open for `Promise`**, whose instances are not promises
internally.

## 1. What was wrong

The engine could make a subclass of `Array` and then lose it at the first
method call:

```js
class List extends Array {}
new List().map(x => x) instanceof List   // false
List.from([1, 2]) instanceof List         // false
typeof Array[Symbol.species]              // "undefined"
```

Nothing about this is an error. Each result is a perfectly good array, and
every textual question about it answers correctly — that is why no scoreboard
saw it: `Array.isArray(result)` is true, `result.length` is the length, and the
elements are right. What is lost is the *identity of the constructor*, and the
only way to notice is to ask `instanceof`.

The standard's `ArraySpeciesCreate` (§10.4.2.3) asks a similar question
to `SpeciesConstructor` (§7.3.21): look at
`O.constructor`, then at that constructor's `Symbol.species`, and make the
result with *that*. The engine had both operations written out inside
`BuiltinsRegExp.cpp`, twice used and private to the file, because
`RegExp.prototype[@@split]` needs them; nothing else could ask.

### 1.1 The property that proves the property is read

A species getter that returns the constructor itself changes nothing observable
— `new List().map(f)` would be a `List` even if the engine never looked, if it
simply constructed `this.constructor`. What proves it looked is a subclass that
*names a different species*:

```js
class Plain extends Array { static get [Symbol.species]() { return Array; } }
new Plain().map(x => x) instanceof Plain   // must be false
new Plain().map(x => x) instanceof Array   // must be true
```

That case is in the corpus (`prototypes.dat`, "methods of a built-in make the
subclass of the object they are called on (Symbol.species)") and it was the recorded failure this phase turned green.

### 1.2 Non-constructor receivers

The standard and Node v22 both make a plain array when `Array.from` or
`Array.of` is called with a receiver that is not a constructor:

```
$ node -e 'console.log(JSON.stringify(Array.from.call({}, [1])))'
[1]
$ node -e 'var o = Array.from.call(function () {}, [1]); console.log(o.length, o instanceof Array)'
1 false
```

The first version of this phase threw a `TypeError`; the Node comparison
caught it, and the suite now checks object and function receivers.

## 2. What was fixed

- `species_constructor()` lives in `JsOps.cpp` and is declared in
  `JsBuiltins.hpp`: one definition, asked by the array methods and the RegExp
  methods both.
- `ArraySpeciesCreate` in `BuiltinsArray.cpp`, with the `IsArray` question
  first (an array-like's `constructor` is never consulted) and `ArrayCreate`'s
  2^32 − 1 limit kept, so the unit test that a four-billion-place `slice` is a
  `RangeError` and not an allocation still passes.
- `map`, `filter`, `slice`, `concat`, `flat`, `flatMap` and `splice` fill the
  array the species made with `CreateDataPropertyOrThrow`, and set `length`:
  a frozen species result is a `TypeError`, a hole stays a hole, and a
  subclass's constructor sees the length the standard passes it (the
  array-like path of `Array.from` passes the count, the iterable path passes
  nothing).
- `Array[Symbol.species]`, `Map[Symbol.species]`, `Set[Symbol.species]` and
  `Promise[Symbol.species]`.

Measured: `prototypes.dat` 70/76, `js-conformance` 1143 → 1144 of 1158 on the
branch (1146 on the latest merged tree, where main had fixed two more cases), one new
unit suite; 327 suites in the current merged tree. Details in
`docs/phases/140-a-subclass-is-made-by-the-builtin-it-was-called-on.md`.

## 3. What is still wrong

- **`Promise` instances made by a subclass are not promises.** `new P(f)` is
  an object whose prototype chain is right and whose internal promise state was
  never attached: `p.then` throws "called on incompatible receiver".
  `Promise.create_promise_instance` sets `promise_proto` and ignores
  `new.target`, so `P.resolve(1)` and `p.then(f)` cannot make a `P` until that
  function can be handed a prototype — and `NewPromiseCapability` can construct
  an arbitrary constructor.
- **`toSorted`, `toReversed`, `toSpliced`, `with`** build plain arrays, as
  specified; these change-by-copy methods do not consult `Symbol.species`.
