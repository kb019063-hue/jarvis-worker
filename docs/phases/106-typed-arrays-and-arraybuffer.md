# Phase 106 — ArrayBuffer and the typed arrays

## 1. What was wrong

`ArrayBuffer` and every typed array were absent from the script environment.
`new ArrayBuffer(8)` was a `ReferenceError`, and with it went every interface
whose surface is defined on a `BufferSource`:

- `fetch()` could hand back a string body and nothing else, so a page that
  reads a binary response had no way to ask for one;
- `WebSocket` could send and receive text frames only — `binaryType` had no
  `arraybuffer` to name, which `AGENTS.md` recorded as blocked on this row;
- `CanvasRenderingContext2D` had no `ImageData`, so a pixel buffer could not
  be built in script, read back or put into the canvas;
- `TextDecoder.decode()` and `TextEncoder.encode()` had nothing to be defined
  on. The 40 decoders and 228 labels of the Encoding Standard landed in
  Phase 47 and were reachable only from the engine.

`ROADMAP.md` calls this row, Wave 2 #8, *"blocks `fetch()` bodies, WebSocket
binary frames, Canvas `ImageData`"* — which is why it was worth taking ahead of
the narrower rows around it.

## 2. What changed

A new translation unit, `src/js/TypedArray.cpp`, and its header. It is
additive: no existing engine source file was edited except one line in
`JsInterpreter.cpp` that installs the globals, and one line in the `Makefile`
that compiles the file.

### The bytes have a home that is not the property table

An `ArrayBuffer` owns a `std::vector<uint8_t>`. A typed array is a window on
one: a shared pointer to the buffer, a byte offset, an element count and an
element type. The indexed behaviour — `ta[0]`, a write that converts,
`ta.length`, `in`, `delete`, `Object.keys` order — is answered by a `JsExotic`
attached to the object, which is the hook `Heap.hpp` already keeps for exactly
this purpose (*"a `NodeList` also answers `list[3]`"*). `JsValue.cpp`,
`Heap.cpp` and `JsEval.cpp` were therefore not touched, which is what let this
phase be done beside the pull requests that hold all three.

It also means the read and the write are a property access and nothing more:
no array of boxed elements is built, and a 100,000-element `Float64Array` is
800 KB rather than 100,000 heap cells.

### The conversions, per element type

Every write stores the element type's conversion of the value, and that is the
whole of what an element type *is*:

| Type | 258 is | 200 is | −4 is |
| :--- | :--- | :--- | :--- |
| `Uint8Array` | `2` | `200` | `252` |
| `Int8Array` | `2` | `-56` | `-4` |
| `Uint8ClampedArray` | `255` | `200` | `0` |

`Uint8ClampedArray` is the one type whose conversion is not modulo a power of
two: it rounds half to even and clamps, and never wraps. `Float32Array`
narrows through `float` and widens back, so `1.5` survives and `0.1` does not
come back as the double it was.

Bytes are little-endian regardless of the host, as every browser's are.

Two results look wrong and are not, which is why they are in the suite:

- `Object.getOwnPropertyDescriptor(new Uint8Array([7]), "0")` reports
  `configurable: true`, and `delete a[0]` returns `false`. Both are what
  ECMA-262 says: `IntegerIndexedExoticObject.[[GetOwnProperty]]` reports a
  configurable element, and its `[[Delete]]` answers `false` for an index that
  is in range. A browser agrees; a reader does not expect it.
- `new Uint8Array(1)[0] = -0.5` is `0` and `0.5` into a `Uint8ClampedArray` is
  `0`, not `1`: `ToUint8` truncates toward zero before the modulo, and the
  clamped type rounds half to **even**, so `0.5 -> 0`, `1.5 -> 2`, `2.5 -> 2`,
  `3.5 -> 4`.

### Windows

`new Uint8Array(buffer)`, `new Uint8Array(buffer, byteOffset)` and
`new Uint16Array(buffer, 4, 2)` check what the standard checks: the offset must
be a multiple of the element size and the view must fit. One buffer has **one**
script object — `new Uint8Array(b).buffer === new Uint16Array(b).buffer` is
`true`, which needed a registry of weak pointer to object, because two views
are made at different times and each would otherwise have made its own.

`subarray()` shares its bytes and `slice()` copies them, and `set()` from an
overlapping window of the same buffer reads before it writes — the standard's
requirement, and the one that is silently wrong if the copy is done in place.

### The chain

`Object.getPrototypeOf(Uint8Array)` is `%TypedArray%`, the abstract class that
throws if it is constructed directly; `Object.getPrototypeOf(Uint8Array) ===
Object.getPrototypeOf(Int16Array)`; `Object.prototype.toString.call(new
Float32Array(0))` is `[object Float32Array]`; and a subclass of a typed array
is one, because the element type is read from `new.target`:

```js
class L extends Uint8Array {}
var l = new L(2);            // a Uint8Array view with L's prototype
l instanceof L;              // true
```

## 3. How it was verified

```
make test              # 262 suites, all passing — arraybuffer_and_typed_arrays is the 262nd
make js-conformance    # 1138/1156 (98.4%), up from 1137 — the Uint8Array case in
                       #   collections-iterators.dat was waiting for this and now passes
make conformance       # 1972/1973, unchanged
make dom-conformance   # 678/678, unchanged
make url-conformance   # 1188/1188, unchanged
make xml-conformance   # 1718/1718, unchanged
make wpt               # 9567/9824 in 190 files, unchanged
python3 tools/ci/check_numbers.py   # 0 failures
```

The unit suite checks 54 assertions against the standard's conversions rather
than against another engine: `Uint8Array`'s 258 is 2 and `Int8Array`'s 200 is
−56 because those are the functions the standard applies, not because a
browser was asked. The clamp-and-round case and the overlapping-`set` case are
there because they are the two where a plausible implementation is wrong.

The one regression this phase nearly shipped is worth recording: the first
build of this file segfaulted on every script, `console.log(1)` included.
A native function in this engine keeps its properties in a table and has **no
heap cell** until `JsValue::function_cell()` makes one, so the line that set
`constructor.object_val->prototype` dereferenced a null pointer. The
`make js-conformance` run reported it as `0/1156 — 1156 crashes`. It is fixed
by asking for the cell.

## 4. What is still open

This is a solid fraction of the standard's typed-array surface, not all of it.
Deliberately not done here, each of which is a row of its own:

- **`DataView`** — the other way to read a buffer, and the one that allows an
  unaligned read. It needs this phase's buffer and nothing else.
- **`ArrayBuffer.prototype.transfer` / `transferToFixedLength` /
  `transfer()` and `SharedArrayBuffer`** — detaching is represented
  (`ArrayBufferData::detached`) and checked on every read, but no constructor
  or method can set it, and a `SharedArrayBuffer` has no atomics.
- **`%TypedArray%.prototype` beyond the everyday methods** — `map`, `filter`,
  `forEach`, `find`, `some`, `every`, `reduce`, `sort` and `toSorted` are not
  here, and `toLocaleString` is not either. What is here is `length`,
  `byteLength`, `byteOffset`, `buffer`, `set`, `subarray`, `slice`, `fill`,
  `copyWithin`, `indexOf`, `lastIndexOf`, `includes`, `join`, `reverse`,
  `toString`, `keys`, `values`, `entries`, `@@iterator`, `from`, `of` and
  `BYTES_PER_ELEMENT`.
- **The consumers this unblocks** — `fetch()` returning an `ArrayBuffer`,
  `WebSocket` binary frames, `ImageData`, and `TextDecoder`/`TextEncoder` —
  are separate rows and are now possible. Taking this row did not take them,
  which is the point of `AGENTS.md`'s *"a bug found outside the task becomes a
  new row"*.
- **A source that is an array-like is read through `length` and the indices**,
  which is right, but an *iterable* other than an array or a typed array is
  read through `js_iterate` in the constructor and through `length` in
  `from()` in one branch; a generator passed to `Uint8Array.from` is exercised
  by the iteration corpus but the two paths could be one.
- The realm's typed-array objects live in a function-local static, as the
  other built-ins' do, and the buffer registry is process-lifetime. Neither is
  torn down, which matches the realm rules of Phase 66.
