## Finding 237 — there was no unscopables table, and no symbol to file it under

`Array.prototype[Symbol.unscopables]` did not exist. `Symbol.unscopables` did
not exist either — the well-known symbols stopped at `iterator`,
`asyncIterator`, `hasInstance`, `toPrimitive` and `toStringTag` — so the
expression was `undefined` twice over and no test could tell which half was
missing.

```js
typeof Symbol.unscopables;                 // "undefined", must be "symbol"
typeof Array.prototype[Symbol.unscopables]; // "undefined", must be "object"
```

It is the language's compatibility mechanism for `with`. A `with` block looks a
name up in the object first, so the day `Array.prototype.values` was added
every old page with `var values = …` inside `with (someArray)` broke. The
unscopables table is the list of names the block must skip, and it is read by
the *scope* machinery, not by the array: an engine with a correct table and no
`with` still cannot run the behaviour it is for.

**Fixed**, in two files because the two halves belong to different objects:

- `Symbol.unscopables` is a well-known symbol in `BuiltinsObject.cpp`, beside
  the other five.
- the table is built in `BuiltinsArray.cpp`: a null-prototype object with the
  standard's sixteen names, each `true`, writable, enumerable and
  configurable; put on `Array.prototype` as `{writable: false, enumerable:
  false, configurable: true}`.

Verified: test262 `built-ins/Array/prototype/Symbol.unscopables` 0/5 → 5/5,
`built-ins/Array` +5 (2,750 of 3,083), `built-ins/Symbol` + `built-ins/Object`
+1 with nothing lost, and the suite
`array_prototype_unscopables_is_the_standards_table` (373/373) fails on the
tree without the change.

**Still open:**

- **`with` is a parse error**, so nothing reads the table. That is the row this
  work came out of; its evaluator half is `src/js/JsEval.cpp`, which an open
  pull request (#254) holds. When `with` lands it must consult this table —
  `HasProperty(unscopables, N)` before the object's own lookup — and a suite
  should run a `with` block over an array and see the outer binding win.
- **`%TypedArray%.prototype[@@unscopables]`** (§23.2.3.35, the same sixteen
  names) is not written. `TypedArray.cpp` was not in this row's files.
- The table is written once, by `install_array_builtins`. A second realm builds
  its own, which is right; a *shared* table between realms would be wrong and
  is not what happens — worth a test if a realm-per-frame case ever reads it.
