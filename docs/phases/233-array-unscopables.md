## Phase 233 — the table a `with` block reads, and the symbol it is filed under

ECMA-262 §23.1.3.34 gives `Array.prototype` a property named by
`Symbol.unscopables`, and the engine had neither half of it: no
`Symbol.unscopables` well-known symbol, and no table.

```js
typeof Array.prototype[Symbol.unscopables];   // "undefined", must be "object"
```

The table is the language's one escape hatch for old pages. `with (arr) { … }`
resolves a name by asking `arr` for it first; the unscopables table names the
sixteen methods that a block must *not* see on the array, so a page written
before `Array.prototype.values` or `find` existed keeps reading its own
`values` and `find` variables out of the enclosing scope:

```js
Array.prototype[Symbol.unscopables].values;   // true — so `values` in a
                                              // with block is not the method
```

The standard's table is an ordinary object with a **null prototype** — the
lookup a `with` block makes must not run into `Object.prototype` — `true`
under each name, every one of them writable, enumerable and configurable,
while the property itself is neither writable nor enumerable and is
configurable. The sixteen are the twelve the 2015 edition named (`copyWithin`,
`entries`, `fill`, `find`, `findIndex`, `flat`, `flatMap`, `includes`, `keys`,
`values`, and the two less obvious: `at` arrived in 2022) plus `findLast` and
`findLastIndex` (2022) and `toReversed`, `toSorted`, `toSpliced` (2023).
Checked against Node v22 rather than taken from the edition summary: Node
lists them in exactly that order, and `Object.getOwnPropertyNames` on the
table is the order the test262 case asserts.

The two changes are small and in different files because the symbol is not an
array's to define: `Symbol.unscopables` joins the well-known symbols in
`BuiltinsObject.cpp` (`iterator`, `asyncIterator`, `hasInstance`,
`toPrimitive`, `toStringTag`, and now `unscopables`), and the table is built in
`BuiltinsArray.cpp` beside `values`, which it hides. The table's own
properties go through `define_property` with `Default` flags; the property on
`Array.prototype` goes through it with `Configurable` alone, which is exactly
`{writable: false, enumerable: false, configurable: true}`.

`built-ins/Array/prototype/Symbol.unscopables` in test262: **0/5 → 5/5**;
`built-ins/Array` **2,745 → 2,750 of 3,083** (+5, nothing lost); the pinned
slices `built-ins/Symbol` + `built-ins/Object` **3,142 → 3,143** (one Symbol
test enumerates the well-known symbols). `make -j3 test`: **373/373**, the new
suite being `array_prototype_unscopables_is_the_standards_table`, a file of its
own in `tests/suites/`. It asserts the sixteen names and their order, every
value and every attribute, the null prototype, the property's own attributes,
that `push`/`map`/`sort`/`length` are *not* in the table, and that
`Symbol.unscopables` is a symbol spelled `Symbol(Symbol.unscopables)`. With the
two source files stashed it fails; restored, it passes. `make js-conformance`
**1,149 of 1,158**, `make dom-conformance` 678/678, `make url-conformance`
1,188/1,188: unchanged.

**What is still open.** The table is observable and correct, but nothing reads
it yet: `with` is still a parse error in this engine (`JsParser.cpp`,
`case JsTokenType::With: fail(...)`), so the behaviour the table exists for
cannot be run. That row is blocked for this phase — the evaluator half is
`src/js/JsEval.cpp`, which #254 holds. `%TypedArray%.prototype` has its own
unscopables table in the standard (§23.2.3.35, the same sixteen names); it is
not written here, and `TypedArray.cpp` was out of this row's files.
