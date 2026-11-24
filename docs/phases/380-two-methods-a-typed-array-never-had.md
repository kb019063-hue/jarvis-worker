# Phase 380 — two methods a typed array never had

Task row: "What is left of `DataView` and `%TypedArray%.prototype` after
Phases 126, 157 and 177." Finding 380.

## 1. What was wrong

`%TypedArray%.prototype.findLast` and `.findLastIndex` did not exist. On a
`Uint8Array`, both answered `undefined`:

```
$ ./build/litebird --run-js "console.log(typeof new Uint8Array([1,2,3]).findLast)"
undefined
```

`Array.prototype` has had both since the ES2023 family (`src/js/BuiltinsArray.cpp`
installs `findLast`/`findLastIndex` beside `find`/`findIndex`), so the two
prototypes disagreed about a method the standard puts on both. The row had
audited the typed-array methods twice — Phase 126 added the thirteen iterating
ones, Phase 177 fixed `toLocaleString` and `toString` — and both times this
pair was not in the list it checked.

It was found by asking, rather than by reading: take the 34 method names of
ECMA-262 §23.2.3, ask `typeof` of each on a `Uint8Array`, and look at what is
not a function. That answered `["findLast","findLastIndex"]` and nothing else.

## 2. What changed

Two methods in `src/js/TypedArray.cpp`, beside the `find`/`findIndex` they
mirror. Each validates the receiver with `require_view`, `.length` is 1, the
name is the standard's, and the callback is checked before any element is
read. The walk runs `len - 1` down to 0, reading each element **live** through
`read_live` — the same call `find` makes — and stops at the first truthy
result, so a callback that writes to the array sees its own writes and an
element above the match is never visited.

`thisArg` is passed through as the callback's `this`, and both return the
standard's no-match answers: `undefined` and `-1`.

## 3. How it was verified

`tests/suites/380-typed-array-findlast.cpp`, 21 assertions, every expected
value taken from **Node v22.22.3** by running the same expression there. It
covers: both are functions with `length` 1 and their own `name`; the
value/absent distinctions against `find`/`findIndex` on the same array; the
no-match answers; the downward walk and its short-circuit (`"3,2"` for a match
at index 2 of four); the callback's three arguments; `thisArg`; a non-callable
callback; an empty array; and four element types, including a
`Uint8ClampedArray` whose stored value differs from what was written.

```
$ ./build/test_suite
Running test: a_typed_array_can_be_searched_from_the_end... PASSED
All 442 engine test suites passed successfully!
```

Red three ways, each at a different assertion:

| engine | fails at |
|---|---|
| both methods absent | `typeof … .findLast` is not `"function"` |
| forward walk (what "add the two methods" looks like if `Last` is missed) | `findLast(x => x < 4)` answers `1`, wanted `3` |
| downward walk but the index counted from the wrong end | `findLastIndex` answers `3`, wanted `2` |

## 4. What is still open

- `BigInt64Array`/`BigUint64Array` do not exist in this engine (`typeof`
  answers `undefined`), so those two methods are untested on them. That is the
  `BigInt` row, not this one.
- `reset`-style detachment: a callback that detaches the buffer mid-walk is
  not asserted, because `ArrayBuffer.prototype.transfer` to a detached state
  from inside a typed-array callback is not exercised by any suite here.
- The other open items of this row are unchanged: `DataView`'s `BigInt64`
  accessors, `SharedArrayBuffer`, `transferToImmutable`.
