# Phase 126 — A `DataView`, and the iterating half of `%TypedArray%.prototype`

Task row: **`DataView`, and the higher-order half of `%TypedArray%.prototype`.**
Phase 106 built the buffer and the nine typed arrays and wrote down, in its §4,
the two things it did not build: `DataView`, and `map`/`filter`/`forEach`/
`find`/`some`/`every`/`reduce`/`sort`/`toSorted` on a view. `src/js/
TypedArray.cpp` was written to be extended this way, and no pull request open
now named it. Finding 131.

## 1. What was wrong

A typed array answers every question about a buffer except two.

**It cannot read a byte that is not on its own element boundary.** A
`Uint16Array` reads at offsets 0, 2, 4; the fourth and fifth bytes of a header
are at offsets 3 and 4 of the buffer and no view of the nine can say so.
**And it cannot choose a byte order.** Its bytes are little-endian by
construction, which is right for the machine and wrong for every format that
begins with a big-endian magic number or length.

`DataView` is the interface for both, and it did not exist:

```bash
grep -c "DataView" src/js/TypedArray.cpp   # 0
```

So a page that fetched a PNG header, a font's `tableDirectory`, or a
`Float32` at offset 3 of a packet had to do the shifting and the byte swapping
itself in script, on a `Uint8Array` — correct, slow, and easy to get wrong.

The second half is smaller and was simply unbuilt. Of the twenty-five methods
`%TypedArray%.prototype` has in the standard, eleven were missing, and they are
the ones a page reaches for after `join`:

```bash
grep -c '"map"\|"filter"\|"sort"' src/js/TypedArray.cpp   # 0
```

`new Uint8Array([3, 1, 2]).sort()` was a `TypeError`, and there was no
`toSorted`, no `at`, no `with`.

Two smaller things were found while building on Phase 106, and both are fixed
here because they are in the same file:

- **`buffer` was not always the same object.** `new Uint8Array(b).buffer === b`
  answered `false` unless someone had asked the *view* for its buffer first: the
  registry that gave one object per buffer was consulted by the getter, but
  `new ArrayBuffer(n)` made its object without registering it. `ROADMAP.md` row
  8 already claimed "one object per buffer so two views of it are `===`", which
  was true of two views and not of the buffer.
- **`join` dropped `NaN`.** `new Float32Array([NaN, 2]).join()` was `"2"`, where
  a typed array has no holes and `Array.prototype.join` prints `"NaN,2"`. The
  `if (!isnan(element))` was copied from a hole-skipping body that has no
  meaning here.

## 2. What changed

Everything is in `src/js/TypedArray.cpp`; the header gained one comment. No
other translation unit changed, so this phase could be done beside the pull
requests holding `JsValue.cpp`, `BuiltinsArray.cpp`, `BuiltinsObject.cpp` and
the render files.

### `DataView`

`new DataView(buffer [, byteOffset [, byteLength]])`, with `buffer`,
`byteOffset` and `byteLength`, and the sixteen `get`/`set` methods: `Int8`,
`Uint8`, `Int16`, `Uint16`, `Int32`, `Uint32`, `Float32`, `Float64`, each
setter with the matching conversion a typed array of that width would make
(`setUint8(0, 258)` stores 2, `setInt8(0, 200)` stores -56).

The bytes are assembled into one unsigned integer in the byte order asked for
and only then reinterpreted — `assemble` and `scatter` are the only two places
the order matters, and `dataview_load`/`dataview_store` above them read as the
conversion table they are. A `DataView` carries its own `JsExotic`, like a
typed array does: it answers `byteLength`, `byteOffset` and `buffer` and no
index at all, because a buffer is read through the methods. `RangeError` past
the end of the view, on a negative offset and on the constructor's two range
cases; `TypeError` when the first argument is not an `ArrayBuffer` or the
receiver is not a `DataView`; `Object.prototype.toString.call(dv)` is
`"[object DataView]"` from the prototype's `@@toStringTag`.

`ArrayBuffer.isView` now includes it, which is the one line of Phase 106's code
`DataView` changes rather than adds to.

### The eleven methods

`at`, `map`, `filter`, `forEach`, `some`, `every`, `find`, `findIndex`,
`reduce`, `reduceRight`, `sort`, `toSorted`, `toReversed`, `with` — thirteen
names for eleven slots, since `at` and `with` were the other gaps. Every
callback is called as `(value, index, typedArray)` with `thisArg` as its
`this`, every one that makes a view makes it of the receiver's own element type
and **prototype**, so `class L extends Int16Array {}` stays an `L`, and every
one raises a `TypeError` for a callback that is not callable rather than
treating it as absent.

`sort`'s default comparison is numeric, which is the whole difference from
`Array.prototype.sort`: `new Float64Array([10, 9]).sort()` is 9, 10 where
`[10, 9].sort()` is 10, 9. `NaN` compares greater than every number, `-0` and
`+0` compare equal, and the sort is stable (`std::stable_sort`), which the test
shows with a comparator that always answers 0.

There is no `Symbol.species` in the engine, so the receiver's prototype stands
in for what `TypedArraySpeciesCreate` would have produced. For a subclass that
is the same answer; for an explicit `[Symbol.species]` override it is not, and
that is listed in §4 rather than implied away.

### The buffer's one object

The registry is gone. `ArrayBufferData` now carries the pointer to the one
script object for its bytes, set when that object is made, and every view marks
it (`JsExotic::trace` → `Heap::mark_object`) while it can still reach it. So
`new Uint8Array(b).buffer === b` is true without anyone asking the view first,
and there is no process-lifetime table of live buffers to grow — the old one
held a strong `JsValue` per buffer it had been asked about, which is a leak
that simply had not been noticed yet.

## 3. How it was verified

`make test` — **282 / 282 suites**, up from 281 on `main` at `b1b8125`; the new
suite is `dataview_and_the_iterating_typed_array_methods`.

The byte order is checked against the **bytes**, not against the getter that
wrote them: `setUint16(0, 0x1234, true)` is asserted to leave `52,18` in a
`Uint8Array` over the same buffer, and the same four bytes are read
`12345678` big-endian and `78563412` little-endian. Everything a wrong
implementation can get plausibly wrong has a case:

| What | Assertion |
| :--- | :--- |
| default order is big-endian | `getUint16` of `[0x34, 0x12]` is `0x3412` |
| unaligned read | `getUint16(1)` of `[0x12, 0x34, 0x56, 0x78]` is `0x3456` |
| conversions | `setInt8(200)` → `-56`, `setUint8(258)` → `2`, `setUint32(-1)` → `4294967295` |
| float widths | `Float64` round trips `1.5` and `-0.25`; `Float32` equals `Math.fround(0.1)` |
| the views share | an `Uint8Array` write is visible to `getUint8` and back |
| bounds | read past the end, `setFloat64(1)` in 8 bytes, negative offset, `byteOffset` past the end, `byteOffset + byteLength` past the end — all `RangeError` |
| types | a plain object as the buffer, a `Uint8Array` as the buffer, and a borrowed `getUint8` — all `TypeError` |
| sort | numeric, `NaN` last, a comparator, stability, and in-place identity |
| copies | `toSorted`/`toReversed`/`with` leave the receiver byte-identical |
| callbacks | thirteen shapes that must be `TypeError`, and `(value, index, array)` in order |
| subclass | `new L([1, 2]).map(...) instanceof L` |

The one case that needs the collector is the buffer's object:
`var v = (function () { return new Uint8Array(new ArrayBuffer(8), 2); })(); gc();
v.buffer.byteLength` is `8|6` — the reference to the buffer is gone, the view is
the only thing that can reach its object, and `trace` is what keeps it.

The suite was checked by breaking the code it holds, each break failing first
where it should:

```
assemble() with the two orders swapped
  → `getUint16(0)` answers 1234 where the bytes it was given say 3412
the RangeError check deleted from check_dataview_range
  → `new DataView(new ArrayBuffer(8)).getUint32(5)` stops throwing
sort()'s default left as a string comparison
  → `new Float64Array([10, 9, 2.5, 1]).sort().join()` is not "1,2.5,9,10"
map() building its result from realm().protos[type]
  → `new L([1, 2]).map(...) instanceof L` is false
```

`make js-conformance` is 1143 / 1158 before and after: none of this is in the
Node-recorded corpus, which is why the cases above are written by hand against
the spec rather than counted from it.

Nothing else moved:

| Command | Before | After |
| :--- | :--- | :--- |
| `make test` | 281 / 281 | **282 / 282** |
| `make js-conformance` | 1143 / 1158 | 1143 / 1158 |
| `make dom-conformance` | 678 / 678 | 678 / 678 |
| `make conformance` | 1973 / 1973 | 1973 / 1973 |
| `make url-conformance` | 1188 / 1188 | 1188 / 1188 |
| `make wpt` | 9567 / 9824 | 9567 / 9824 |

## 4. What is still open

- **`BigInt64`/`BigUint64` on a `DataView`** need `BigInt`, which the engine
  does not have; their absence is the only hole in the sixteen.
- **`Symbol.species`** does not exist, so the copying methods use the
  receiver's prototype rather than `this.constructor[Symbol.species]`. A
  subclass gets the right answer; an explicit species override is ignored.
- **`toLocaleString` on a view** is still missing (it needs the locale
  machinery, not this file).
- **`ArrayBuffer.prototype.transfer` / `SharedArrayBuffer`** are Phase 106's
  open rows and remain open: the detached flag is checked by every `DataView`
  method but nothing can set it.
- **The `DataView` receiver is not brand-checked structurally**: a
  `DataView.prototype.getUint8.call({})` is a `TypeError`, as it should be, but
  there is no `Object.getPrototypeOf` walk to a second `DataView` whose
  constructor was borrowed across realms.
