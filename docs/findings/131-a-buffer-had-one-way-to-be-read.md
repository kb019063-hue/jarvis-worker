# Finding 131 — a buffer had one way to be read

Phase 126. The architecture and the measurements are in
[`docs/phases/126-a-dataview-and-the-iterating-half.md`](../phases/126-a-dataview-and-the-iterating-half.md).
Status: **resolved** for `DataView` and the iterating methods; the parts left
out are in §3 rather than implied to work.

## 1. What was wrong

Phase 106 gave the engine `ArrayBuffer` and the nine typed arrays. A typed
array fixes three things when it is constructed — its element width, its
offset, and the fact that its bytes are little-endian — and those three are
exactly what a binary format does not promise. A PNG's length field is a
big-endian `uint32`; a font's `tableDirectory` starts at byte 12, which is on
no `Uint32Array` boundary; a protocol's `float` sits at offset 3 of a packet.

So the engine could read a buffer, but only at the offsets and in the order it
had chosen in advance. The interface for everything else — `DataView` — was not
there:

```bash
grep -c "DataView" src/js/TypedArray.cpp   # 0
```

Pages worked around it, which is the expensive way: an `Uint8Array`, four
shifts and an `|` per field, in script, at every read.

The second half is not a correctness bug but a hole: of the twenty-five methods
`%TypedArray%.prototype` has, eleven were missing — `map`, `filter`,
`forEach`, `some`, `every`, `find`, `findIndex`, `reduce`, `reduceRight`,
`sort`, `toSorted` — plus `at`, `toReversed` and `with`. `new Uint8Array([3,
1, 2]).sort()` was a `TypeError`, and the array a page gets from `fetch()` had
no `map`.

Two smaller defects in the same file came out while building on it:

- **`buffer` was one object only if you asked the view first.** The registry
  that makes `view.buffer === view.buffer` was consulted by the getter, but
  `new ArrayBuffer(n)` made its object without registering it, so
  `new Uint8Array(b).buffer === b` was `false`. `ROADMAP.md` row 8 already
  claimed "one object per buffer so two views of it are `===`" — true of two
  views, not of the buffer.
- **`join` dropped `NaN`.** `new Float32Array([NaN, 2]).join()` was `"2"`. The
  `if (!isnan(element))` belongs to a hole-skipping body; a typed array has no
  holes, and `Array.prototype.join` prints `"NaN"`.

## 2. What the fix was

- `DataView`, as its own `JsExotic` with no indexed access: `buffer`,
  `byteOffset`, `byteLength`, and `get`/`set` for `Int8`/`Uint8`/`Int16`/
  `Uint16`/`Int32`/`Uint32`/`Float32`/`Float64`, each with the `littleEndian`
  argument and each applying the conversion a typed array of that width would.
  `assemble`/`scatter` are the only two places the byte order is named; the
  load and store functions above them read as a conversion table.
- `RangeError` past the end of the view, on a negative or out-of-range offset,
  and on either of the constructor's range cases; `TypeError` for a first
  argument that is not an `ArrayBuffer` and for a receiver that is not a
  `DataView`. `ArrayBuffer.isView` learned that a `DataView` is a view, and
  `Object.prototype.toString.call(dv)` is `"[object DataView]"`.
- `at`, `map`, `filter`, `forEach`, `some`, `every`, `find`, `findIndex`,
  `reduce`, `reduceRight`, `sort`, `toSorted`, `toReversed`, `with` — called as
  `(value, index, typedArray)` with `thisArg`, copying into the receiver's own
  element type and prototype, and refusing a callback that is not callable.
  `sort`'s default comparison is numeric with `NaN` last and `-0` equal to
  `+0`, and the sort is stable.
- **One object per buffer, without a table.** `ArrayBufferData` carries the
  pointer to its one script object, set when that object is made; every view
  marks it through `JsExotic::trace` while it can still reach it, so the object
  stays alive exactly as long as something can name it. The old process-lifetime
  registry held a strong `JsValue` per buffer it had been asked about, which was
  a leak as well as an identity bug.
- `join`/`toString` print `NaN`.

## 3. What is still open

| What | Why it is left |
| :--- | :--- |
| `BigInt64`/`BigUint64` on a `DataView` | `BigInt` does not exist in the engine |
| `Symbol.species` | the receiver's prototype stands in; a subclass is right, an explicit species override is ignored |
| `toLocaleString` on a view | needs the locale machinery, not this file |
| `transfer()` / `SharedArrayBuffer` | Phase 106's own rows; the detached flag is checked but nothing sets it |
| `toLocaleString`, `toSpliced` on a view | not part of the thirteen taken here |

The oracle is the standard's own arithmetic, written out by hand, and the
byte-level assertions are the ones that matter: a byte order is checked against
the bytes another view of the same buffer sees, not against the getter that
wrote them, so a `DataView` whose reader and writer agree on the wrong order
fails. Each of the four properties above was also confirmed by breaking the
code — swapped byte order, the range check deleted, `sort` left as a string
sort, `map` building from the realm's prototype — and each failed in its own
case. What is *not* independently checked is another implementation's opinion
of the same bytes: no browser runs in this repository, and the Node-recorded
corpus has no `DataView` in it.
