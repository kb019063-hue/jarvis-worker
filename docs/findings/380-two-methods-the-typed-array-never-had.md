# Finding 380 — two methods the typed array never had

Phase 380. Status: **fixed; `%TypedArray%.prototype.findLast` and
`.findLastIndex` exist.**

## 1. What was wrong

`new Uint8Array([1,2,3]).findLast` answered `undefined`. Both ES2023
§23.2.3.10 and §23.2.3.11 methods were missing from `%TypedArray%.prototype`,
while `Array.prototype` has had both since its own ES2023 family landed — so
two prototypes disagreed about a method the standard puts on both, and code
that legitimately used it on an array failed on a typed array of the same
numbers.

This is a **missed audit**, not an unexamined area. The row had been through
Phase 126 (thirteen iterating methods) and Phase 177 (`toLocaleString`,
`toString`), and both times the list it checked did not contain this pair.

The discovery method is the part worth keeping: instead of reading the file
for what was there, ask the engine for what is *not*. The 34 method names of
§23.2.3, `typeof` on a `Uint8Array`, and the complement answered
`["findLast","findLastIndex"]` — two names out of 34, and no others.

## 2. What changed

`src/js/TypedArray.cpp`: two `js_define_method` blocks beside `find` and
`findIndex`. Downward walk, live element reads, short-circuit on the first
truthy result, `thisArg` forwarded, and the standard's `undefined` / `-1` when
nothing matches.

## 3. Evidence

`tests/suites/380-typed-array-findlast.cpp` — 21 assertions, each expected
value taken from Node v22.22.3 by evaluating the same expression there. It
separates `findLast` from `find` on the same array (`3`/`2` against `1`/`0`),
pins the walk order and its short-circuit (`"3,2"`), checks `length` and
`name`, a non-callable callback, an empty array, `thisArg`, and four element
types.

Red on three engines: methods absent; a forward walk; and a downward walk with
the index counted from the wrong end. Each fails a different assertion.

`All 442 engine test suites passed successfully!`

## 4. What remains wrong

- `BigInt64Array`/`BigUint64Array` are absent from the engine entirely, so
  this pair is untested on them. That belongs to the `BigInt` row.
- A callback that detaches the array's buffer mid-walk is not asserted; no
  suite covers that for `find` either, and it is the same code path.
- The discovery was one pass over one section. It says nothing about
  `%TypedArray%` statics (`from`, `of` exist) or about §23.2.3 accessors, and
  the next audit of a prototype should be done the same way — ask for the
  complement, do not read for the presence.
