# Phase 140 — a subclass is made by the built-in it was called on

Task row: **Several built-ins still do not make the subclass they were called
on.** Finding 145.

`class List extends Array {}` was a class whose instances were lists and whose
*methods* were not: `List.from([1, 2])`, `List.of(1)`, `new List().map(f)` and
`new List().slice(0)` were all plain arrays. The standard has one operation for
this — `ArraySpeciesCreate`, ECMA-262 §10.4.2.3. The related
`SpeciesConstructor` operation was private to the `RegExp` implementation.
Every array method made its result with `JsValue::make_array`.

## 1. What was wrong

```
$ litebird --run-js 'class A extends Array {}; console.log(A.from([1]) instanceof A)'
false
$ litebird --run-js 'console.log(typeof Array[Symbol.species])'
undefined
```

`Symbol.species` did not exist as a property of any constructor. The RegExp
file installed the *symbol* (`Symbol.species` itself), because `@@split` and
`@@match` needed it, and its `species_constructor()` helper handled the three
questions the standard asks — `constructor`, `constructor[Symbol.species]`,
`IsConstructor` — but it sat in an anonymous namespace, so nothing else could
ask them.

Two of the corpus's recorded failures were exactly this:

```
prototypes.dat: static methods of a built-in make the subclass they are called
                on (Array.from, Promise.resolve)
prototypes.dat: methods of a built-in make the subclass of the object they are
                called on (Symbol.species)
```

The second one passing is the whole test of whether the property is *read*,
not merely present: `class Plain extends Array { static get [Symbol.species]()
{ return Array; } }` must make `q.map(f)` an `Array` and not a `Plain`.

## 2. What changed

- **`species_constructor()` moved to `JsOps.cpp`** and is declared in
  `JsBuiltins.hpp`, so the array methods and the RegExp methods ask the same
  three questions in the same order — one definition, not two.
- **`ArraySpeciesCreate` in `BuiltinsArray.cpp`**: a receiver that is not an
  array makes a plain array (`IsArray` is asked first, so an array-like's
  `constructor` is never consulted), and an array asks `constructor[Symbol.
  species]` and constructs the result with the length. `ArrayCreate`'s own
  2^32 − 1 limit is applied to the two paths that make the array here, and not
  to a species constructor, which may answer with whatever it likes.
- **The methods that make an array** now make it that way: `map`, `filter`,
  `slice`, `concat`, `flat`, `flatMap`, `splice`, and the static `from` and
  `of` (which use `this` rather than a species — the standard's difference).
  Each fills its result with `CreateDataPropertyOrThrow` and sets `length`,
  which is why a frozen species result is a `TypeError` rather than a silently
  dropped element.
- **The species getters**: `Array[Symbol.species]`, `Map[Symbol.species]`,
  `Set[Symbol.species]` and `Promise[Symbol.species]` — each the constructor
  itself, each a getter with `[[Call]]` and not `[[Construct]]`. `RegExp` had
  one already.

### 2.1 A non-constructor receiver

Both the standard and Node v22 make a plain array when the receiver of
`Array.from` or `Array.of` is not a constructor. The first version of this
phase threw a `TypeError`; the Node comparison caught it:

```
$ node -e 'console.log(JSON.stringify(Array.from.call({}, [1])))'
[1]
$ litebird --run-js 'console.log(JSON.stringify(Array.from.call({}, [1])))'
[1]
```

## 3. Numbers

```
$ python3 tools/conformance/run_js_conformance.py --binary build/litebird
JS conformance, full corpus: 1144/1158 (98.8%)  [29 files, 0 crashes, 0 timeouts]
```

1143 → **1144** on the branch as it was written: the `Symbol.species` case of
`prototypes.dat` passes, and `prototypes.dat` has six recorded failures left
rather than seven. The other case — `Array.from` *and* `Promise.resolve` in one
script — still fails on its `Promise` half (see §4). Main was merged before this
landed and had fixed one `iteration.dat` case of its own, so the tree this
first merged tree printed **1145/1158 (98.9%)**. A later merge of main
fixed another iteration case; the current tree prints **1146/1158 (99.0%)**,
and the ledger and CI floor carry that number.

```
$ make test
All 327 engine test suites passed successfully!
Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).
```

The new suite `a_subclass_is_made_by_the_builtin_it_was_called_on`
checks subclassing, species overrides, holes, failures, and the copying methods;
expected results were compared with Node v22.22.3. The suite count reflects
main merged into this branch.
Everything else is unchanged: dom 678/678, url 1188/1188, xml 1718/1718,
tree-construction 1973/1973, wpt 9567/9824, render reftests 29/34 with five
recorded.

## 4. What is still open

The row is not empty, and it is now written down as what it is:

- **`Promise` is the other half.** `class P extends Promise {}` makes an
  instance, but the instance is not a *promise* internally: `new P(f).then`
  throws "called on incompatible receiver", because the promise's state lives
  in a `PromiseData` that only `Promise.create_promise_instance` attaches, and
  that function ignores `new.target`. `P.resolve(1)`, `P.all([1])` and
  `p.then(f)` are therefore still plain promises — and `Promise[Symbol.species]`
  is installed but nothing reads it. This is the bigger half and it needs
  `NewPromiseCapability` to work for a constructor that is not `%Promise%`.
- **Copying methods deliberately do not ask species**: `toSorted`,
  `toReversed`, `toSpliced` and `with` use `ArrayCreate` and make a plain
  array. That is the specified behavior, not remaining subclass work.
- **`Map` and `Set` have the getter and no user**: no method of either asks
  `Symbol.species` (`Map`/`Set` methods do not create maps), so the property is
  correct and unexercised.
