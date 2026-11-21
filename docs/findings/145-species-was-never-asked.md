# Finding 145 — `Symbol.species` was never asked

Phase 140. Status: **found and fixed for Array** (the static and instance
methods that make an array, and the species getters on `Array`, `Map`, `Set`,
`Promise`). **Still open for `Promise`**, whose instances are not promises
internally, and for the four copying methods.

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

The standard's answer is one abstract operation, `SpeciesConstructor`
(§7.3.21), plus one derived from it, `ArraySpeciesCreate` (§23.1.5.1): look at
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
subclass of the object they are called on (Symbol.species)") and it was one of
the two recorded failures this phase turned green.

### 1.2 Where the standard and the reference engine disagree

ES2023's `Array.from` throws a `TypeError` when `this` is not a constructor.
Node v22 does not — it makes a plain array, which is the ES2015 rule that V8
still implements:

```
$ node -e 'console.log(JSON.stringify(Array.from.call({}, [1])))'
[1]
$ node -e 'var o = Array.from.call(function () {}, [1]); console.log(o.length, o instanceof Array)'
1 false
```

The corpus's expected outputs are Node's, and a divergence from the engine the
corpus was *written from* is a bug in this tree whether or not the standard
agrees. The first version of this phase threw the `TypeError` and the unit
suite compared against Node and said so; the engine now follows Node, and the
suite pins all three shapes (an object receiver, a function receiver, `null`).

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

Measured: `prototypes.dat` 70/76, `js-conformance` 1143 → 1144 of 1158, one
new unit suite, 300 suites in total. Details in
`docs/phases/140-a-subclass-is-made-by-the-builtin-it-was-called-on.md`.

## 3. What is still wrong

- **`Promise` instances made by a subclass are not promises.** `new P(f)` is
  an object whose prototype chain is right and whose internal promise state was
  never attached: `p.then` throws "called on incompatible receiver".
  `Promise.create_promise_instance` sets `promise_proto` and ignores
  `new.target`, so `P.resolve(1)` and `p.then(f)` cannot make a `P` until that
  function can be handed a prototype — and `NewPromiseCapability` can construct
  an arbitrary constructor.
- **`toSorted`, `toReversed`, `toSpliced`, `with`** build plain arrays; they
  were not on the row's list when it was written.
