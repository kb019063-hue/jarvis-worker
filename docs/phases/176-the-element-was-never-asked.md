# Phase 176 — the element was never asked

Task row: `DataView` and `%TypedArray%.prototype`, which after Phases 126 and 157
lists five separable gaps. This is `toLocaleString`, and the identity of
`toString`. Finding 180.

## 1. What the row said, and what was really there

The row said "`toLocaleString` on a view" was missing. It was there, so the row
was half wrong: `new Uint8Array([1,2]).toLocaleString()` answered "1,2". What
was missing is what the method *is*.

§23.1.3.28 does not join anything. For each element it asks the element for its
own `toLocaleString` and calls that method, so a patched
`Number.prototype.toLocaleString` shows in `[1,2].toLocaleString()`. The engine
formatted the numbers itself, and the same wrong code was in **both**
`Array.prototype.toLocaleString` and the typed array's copy — one defect with
two homes. Probed against Node v22.22.3:

```
Number.prototype.toLocaleString = () => "X";
[1,2].toLocaleString()               engine "1,2"   Node "X,X"
new Uint8Array([1,2]).toLocaleString() engine "1,2"   Node "X,X"
[1,,3].toLocaleString()              engine "1,,3"  Node "X,,X"
```

The third line is the exception the standard makes and the reason the fix is
not `join`: a hole and `undefined` join as the empty string **without asking
anything**, so the patched method does not appear for them.

The third defect is identity. §23.2.3.30 says `%TypedArray%.prototype.toString`
**is** `Array.prototype.toString` — the same function object — and the engine
gave the typed array its own copy, so `Uint8Array.prototype.toString ===
Array.prototype.toString` was `false` where Node says `true`.

## 2. The two are not the same function, and the difference is the receiver

The obvious move, to make both `toLocaleString`s one function, is wrong, and
test262 says so: `TypedArray/prototype/toLocaleString/this-is-not-object` spends
thirteen assertions on `call(undefined)`, `call(null)`, `call(42)`,
`call("1")`, `call(true)` and `call(Symbol())` all being TypeErrors. Array's
boxes a primitive and joins its indices, so `Array.prototype
.toLocaleString.call("abc")` is "a,b,c" and `call(42)` is "". Node agrees on
every one.

So there are two functions that share one walker:

- `Array.prototype.toLocaleString` — ToObject first. A primitive is boxed and
  walked, so a String object's indices join as "a,b,c". A bare `ArrayBuffer` is
  not a list of elements and answers "".
- `%TypedArray%.prototype.toLocaleString` — ValidateTypedArray first, so a
  non-object, a plain object, an `ArrayBuffer` and a `DataView` are all
  TypeErrors, and a detached buffer is the TypeError the row's
  `transfer()` work already produces for every other method.
- Shared: the walk, and the boxing of each **element** before its method is
  asked (`Invoke(element, "toLocaleString")` boxes, which is why a method that
  answers `typeof this` must see "boolean").

## 3. Length is the internal slot for a view

The first version read the `length` **property** for both, and test262 caught
it: `get-length-uses-internal-arraylength` redefines `length` on a view, and a
typed-array method must read `[[ArrayLength]]` instead. So the typed array's
path takes the element count from the view's own slot and never looks at the
property. Array's path takes Len(Get(O, "length")), which is what an ordinary
list is.

Failing test262, before and after the fix for it:

```
LOST built-ins/TypedArray/prototype/toLocaleString/
     get-length-uses-internal-arraylength.js
   Expected SameValue(«1», «0») to be true
```

## 4. What was measured

Run against the pinned test262 (`2e58f87d8350`), old binary beside new:

| directory | before | after | |
|---|---|---|---|
| `built-ins/TypedArray/prototype/toLocaleString` | 11 / 39 | 24 / 39 | |
| `built-ins/TypedArray/prototype/toString` | 2 / 11 | 4 / 11 | |
| `built-ins/Array/prototype/toLocaleString` | 0 / 4 | 1 / 4 | |
| **all three** | **13 / 54** | **29 / 54** | +16, 0 lost |
| **all of `built-ins/TypedArray` + `built-ins/Array`** | **3315 / 4536** | **3332 / 4536** | **+17, 0 lost** |

The whole-tree run is the one that matters: changing
`Array.prototype.toLocaleString` is a change every script that calls it sees,
and 4,536 tests found no case that got worse.

The 25 tests still failing in the three directories are counted, not waved away:

- **20 need `BigInt`** — `BigInt64Array`/`BigUint64Array`, `BigInt` literals,
  the `BigInt/*` subdirectory. The row lists "`BigInt64`/`BigUint64` on a
  `DataView` (needs `BigInt`)" as a separate gap, and this phase does not add
  `BigInt`.
- **3 need `$262`** — `detached-buffer.js` in both directories and
  `resizable-buffer.js` call the harness's `$262.detachArrayBuffer`, which this
  engine's runner does not provide. The detachment the tests are about is
  asserted directly in `tests/test_suite.cpp` instead (`b.transfer()` then
  `v.toString()`/`v.toLocaleString()` is a TypeError).
- **2 need the `this`-binding of a method call**, and that is a finding of its
  own — see below.

## 5. What this phase found and did not fix

`(true).toString()` with `Boolean.prototype.toString = function () { return
typeof this; }` answers `"object"` in this engine and `"boolean"` in Node. So
does `(function () { return typeof this; }).call(42)`. **Every** call in this
engine passes a boxed `this`, in strict code and sloppy, for primitives — a
defect of the interpreter's call path and not of this row. It is the last two
failures in the run above (`Array/prototype/toLocaleString/primitive_this_value`
and `primitive_this_value_getter`), and it will be there after this phase. It is
recorded in `AGENTS.md` as its own row because fixing it touches every function
call in the engine and deserves its own branch.

`toLocaleString`'s own boxes are correct: the shared walker boxes each element
before asking it, because `Invoke` boxes — that is a different, correct place.

Still open on this row after this phase: `BigInt64`/`BigUint64` on a `DataView`
(needs `BigInt`), `Symbol.species`, `SharedArrayBuffer`, and
`transferToImmutable`.

**`Symbol.species` is not a gap any more, and this phase retired it.** The
2025 edition removed `@@species` from `%TypedArray%` (tc39/ecma262#3450), so
`TypedArray[Symbol.species]` being `undefined` and `.map`/`.slice` making the
subclass from the receiver's own constructor is now the standard. Node v22.22.3
does the same, which is checked in the row rather than assumed.
