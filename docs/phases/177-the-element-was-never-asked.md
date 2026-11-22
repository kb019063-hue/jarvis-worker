# Phase 177 — the element was never asked

Task row: `DataView` and `%TypedArray%.prototype`, which after Phases 126 and
157 lists five separable gaps. This is `toLocaleString` on a view, and the
identity of `toString`. Finding 181.

Everything here is in `src/js/TypedArray.cpp`. `Array.prototype.toLocaleString`
has the same defect and **is not fixed by this phase** — see section 5.

## 1. What the row said, and what was really there

The row said "`toLocaleString` on a view" was missing. It existed, which is what
made it hard to see: `new Uint8Array([1,2]).toLocaleString()` answered `"1,2"`,
and `"1,2"` is the right answer for `[1,2]` in an English locale, so nothing in
the output said otherwise.

What was missing is what the method *is*. §23.2.3.29 is not `join`: for each
element it asks the element for its own `toLocaleString` and calls that method,
with the **boxed** element as `this`. The engine formatted the numbers itself.
Probed against Node v22.22.3:

```
Number.prototype.toLocaleString = () => "X";
new Uint8Array([1,2]).toLocaleString()   engine "1,2"   Node "X,X"
new Float32Array([NaN]).toLocaleString() engine "NaN"   Node "X"
```

The second line matters twice over: a typed array has no holes, so every element
is a value and every element is asked — the hole exception that makes
`[1,,3].toLocaleString()` `"1,,3"` does not apply to a view at all.

The other defect is identity, and only `===` finds it. §23.2.3.30 says
`%TypedArray%.prototype.toString` **is** `Array.prototype.toString` — the same
function object. The engine gave the typed array its own copy, which answered
the same text and was the wrong value:

```
Uint8Array.prototype.toString === Array.prototype.toString   // was false
```

## 2. The two are not the same function, and the difference is the receiver

The obvious move, to make both `toLocaleString`s one function, is wrong, and
test262 says so. `TypedArray/prototype/toLocaleString/this-is-not-object` spends
thirteen assertions on `call(undefined)`, `call(null)`, `call(42)`,
`call("1")`, `call(true)` and `call(Symbol())` all being TypeErrors. Array's
boxes a primitive and joins its indices, so `Array.prototype
.toLocaleString.call("abc")` is `"a,b,c"` and `call(42)` is `""`. Node agrees on
every one.

So the typed array's stays its own function, and it refuses everything Array's
accepts:

| receiver | `Uint8Array.prototype.toLocaleString` | `Array.prototype.toLocaleString` |
|---|---|---|
| a view | its elements, each asked | its elements |
| `42`, `"1"`, `true`, `null` | TypeError | `""` (boxed, no indices) |
| `"abc"` | TypeError | `"a,b,c"` (boxed String has indices) |
| `{length:1,0:5}` | TypeError | `"5"` |
| an `ArrayBuffer` | TypeError | `""` |
| a `DataView` | TypeError | `""` |
| a detached view | TypeError | TypeError |

## 3. Length is the internal slot

The first version read the `length` **property**, and test262 caught it:
`get-length-uses-internal-arraylength` redefines `length` on a view, and a
typed-array method must read `[[ArrayLength]]` instead. The method now takes the
element count from the view's own slot and never looks at the property, which
the test asserts by defining `length: 9` on a three-element view and getting
`9|1,2,3` back (the property reads 9, the method walks 3).

## 4. What was measured

Run against the pinned test262 (`2e58f87d8350`), old binary beside new:

| directory | before | after |
|---|---|---|
| `built-ins/TypedArray/prototype/toLocaleString` | 11 / 39 | 24 / 39 |
| `built-ins/TypedArray/prototype/toString` | 2 / 11 | 4 / 11 |
| **all of `built-ins/TypedArray` + `built-ins/Array`** (4,536) | **3,315** | **3,329** |

**+14, and no test that passed before failed after.** The whole-tree run is the
one that matters, because `%TypedArray%.prototype` is inherited by nine
constructors.

The 25 still failing in `toLocaleString` are counted, not waved away:

- **20 need `BigInt`** — `BigInt64Array`/`BigUint64Array`, `BigInt` literals,
  the whole `BigInt/*` subdirectory. The row lists `BigInt64`/`BigUint64` on a
  `DataView` as a separate gap and this phase does not add `BigInt`.
- **3 need `$262`** — `detached-buffer.js` in both directories and
  `resizable-buffer.js` call the harness's `$262.detachArrayBuffer`, which this
  runner does not provide. The detachment the tests are about is asserted
  directly in `tests/test_suite.cpp` (`b.transfer()`, then `toString()` and
  `toLocaleString()` are TypeErrors).
- **2 need the `this`-binding of a method call** — see section 6.

## 5. What this phase found and did not fix: Array's half

`Array.prototype.toLocaleString` has **the same defect**, in the same wrong
shape, and this phase does not fix it:

```
Number.prototype.toLocaleString = () => "X";
[1,2].toLocaleString()     engine "1,2"   Node "X,X"
[1,,3].toLocaleString()    engine "1,,3"  Node "X,,X"
```

The fix is known and was written and measured — it took the whole-tree number to
**+17** — but it needs `src/js/BuiltinsArray.cpp` and `src/js/JsOps.cpp`, and
pull request #176 (Phase 140, species) already has both files open and is older.
AGENTS.md says an older claim keeps its files. Array's `toLocaleString` is not
this row either; this row is `%TypedArray%.prototype`.

So the fix was narrowed to the typed array, which is strictly this row's
subject, and the Array half is recorded in `AGENTS.md` as its own row, with the
measurement, so it is not lost: **`+3` of the `+14` above is what Array's half
would add**, and `built-ins/Array/prototype/toLocaleString` would go from 0/4 to
1/4.

## 6. What this phase found and did not fix: `this` and primitives

`(function () { return typeof this; }).call(42)` answers `"object"` in this
engine and `"boolean"` in Node. So does
`Boolean.prototype.toString = function () { return typeof this; }` followed by
`(true).toString()`. **Every** call in this engine passes a boxed `this` for a
primitive, strict or sloppy; §10.2.1.2 boxes only sloppy functions.

That is the interpreter's call path, not this row, and it is the last two
non-`BigInt` failures above (`Array/prototype/toLocaleString/primitive_this_value`
and `primitive_this_value_getter`). It gets its own row in `AGENTS.md`: it
changes every call in the engine and wants its own before/after run.

`Invoke` inside the walker does box, correctly and deliberately — that is a
different place, and `typeof this` inside an element's `toLocaleString` is the
one `this` this phase can get right today.

## 7. What is left on the row

`BigInt64`/`BigUint64` on a `DataView` (needs `BigInt`), `SharedArrayBuffer`,
and `transferToImmutable`.

**`Symbol.species` is retired as a gap, not implemented.** The 2025 edition
removed `@@species` from `%TypedArray%` (tc39/ecma262#3450), so
`TypedArray[Symbol.species]` being `undefined`, and `.map`/`.slice` building
from the receiver's own constructor, is now the standard. Node v22.22.3 does the
same — measured, not assumed:

```
Uint8Array[Symbol.species] === Uint8Array   false
Sub.hasOwnProperty(Symbol.species)          false
new Sub([1,2]).map(x => x) instanceof Sub   true
```
