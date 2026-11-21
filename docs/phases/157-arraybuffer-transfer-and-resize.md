# ES2024 `ArrayBuffer` transfer, detachment, and resizing

*Phase 157, finding 162.*

## 1. What was wrong

Phase 106 and Phase 126 gave `ArrayBufferData` a `bool detached` field and
checked it inside `TypedArray` and `DataView` methods, but nothing in the
engine could ever detach a buffer (`transfer()` and `transferToFixedLength()`
did not exist, and neither did the `detached` getter) or create a resizable
`ArrayBuffer` (`{ maxByteLength }`, `resizable`, `maxByteLength`, `resize()`).
In addition:

- `byteLength`, `byteOffset`, `length`, and `buffer` were answered directly by
  exotic `get()` hooks instead of prototype accessors, so
  `Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, ...)` was `undefined`.
- `TypedArray`'s `byteOffset` did not zero out on a detached view, and
  `DataView`'s `byteLength`/`byteOffset` returned `0` / the old offset instead
  of throwing `TypeError` on detached or out-of-bounds views.
- `TypedArray` iterating methods (`map`, `filter`, `forEach`, `some`, `every`,
  `find`, `findIndex`, `reduce`, `reduceRight`) pre-snapshotted the whole
  backing store before invoking user callbacks, so shrinking or detaching a
  buffer inside a callback returned stale numbers instead of `undefined`.
- `DataView` construction did not reject a detached buffer, and `DataView`
  getters/setters did not re-check bounds after `valueOf()` argument conversion.

## 2. What changed

All implementation changes are confined to `src/js/TypedArray.cpp`:

- **`ArrayBuffer` constructor & prototype (`ECMA-262 §25.1`):**
  - `new ArrayBuffer(byteLength, options)` parses `options.maxByteLength` via
    `ToIndex`, validates `byteLength <= maxByteLength` (`RangeError`), and
    supports subclass construction (`derived_this`).
  - Installed configurable, non-enumerable, non-constructor prototype accessors
    on `ArrayBuffer.prototype` (`byteLength`, `maxByteLength`, `resizable`,
    `detached`), `DataView.prototype` (`buffer`, `byteLength`, `byteOffset`),
    and `%TypedArray%.prototype` (`buffer`, `byteLength`, `byteOffset`,
    `length`).
  - Implemented `ArrayBuffer.prototype.resize(newLength)`,
    `ArrayBuffer.prototype.transfer([newLength])`, and
    `ArrayBuffer.prototype.transferToFixedLength([newLength])` (each marked
    `not_constructor = true`). `transfer` preserves resizability and
    `maxByteLength` (throwing `RangeError` without detaching if
    `newLength > maxByteLength`); `transferToFixedLength` always produces a
    fixed-length buffer; both evaluate `ToIndex(newLength)` before checking
    `detached` and free the old vector on detach.
- **Length-tracking & out-of-bounds views (`TypedArrayData`, `DataViewData`):**
  - Views constructed over a resizable `ArrayBuffer` without an explicit
    length/byteLength track the buffer's live size (`length_tracking = true`),
    flooring multi-byte `TypedArray` lengths on odd byte lengths; `subarray`
    preserves length-tracking when `end` is omitted.
  - `is_out_of_bounds()` / `effective_length()` / `effective_byte_length()`
    handle detached buffers and shrunk buffers across every view method,
    restoring fixed-length views when the buffer grows back into range.
  - Iterating methods read each index live (returning `undefined` past the
    current effective length), and `set` / `sort` / `DataView` getters &
    setters / `slice` / `TypedArrayView::set` re-validate bounds after user
    callbacks and `valueOf()` conversions so no path can read or write past
    freed/shrunk storage.
  - Detached `TypedArray` iterators throw `TypeError` on `.next()`, matching
    ECMA-262 and Node.js.
  - `typed_array_bytes()` and `typed_array_write()` check `is_out_of_bounds()`
    and use `effective_byte_length()` (and `typed_array_bytes()` also accepts
    `DataView`).

## 3. How it was verified

Paired two-binary test262 run (`2e58f87d8350`) comparing `main` (`11b1ad3`)
against this branch across `built-ins/ArrayBuffer`, `built-ins/DataView`,
`built-ins/TypedArray`, and `built-ins/TypedArrayConstructors` (2,972 tests,
0 skipped):

- **Before (`11b1ad3`):** `1,064 / 2,972` (`35.8%`, 1 timeout)
- **After (Phase 157):** **`1,351 / 2,972` (`45.5%`, 0 timeouts)**
- **Net delta:** **`+287` pass in after and not before; `0` pass in before and not after**

Additional checks:

- **Differential check vs Node.js v22.22.3:** 59/59 `ArrayBuffer`/`TypedArray`/`DataView`
  probes match byte-for-byte.
- `make test`: **316 / 316** engine suites + 114 cross-origin checks (new
  suite: `arraybuffer_transfer_detach_and_resize`).
- `make js-conformance`: 1144 / 1158 (98.8%), everyday probe 90/90, 0 crashes.
- `make gc-oracle`: PASS.
- `make check-numbers` & `python3 tools/ci/check_docs.py`: 0 failures.

## 4. What is still open

- `SharedArrayBuffer` and growable shared buffers.
- `BigInt64Array` / `BigUint64Array` and `DataView`'s `BigInt64`/`BigUint64`
  methods (waiting on `BigInt`).
- `ArrayBuffer[Symbol.species]` and `TypedArray[Symbol.species]`.
- `ArrayBuffer.prototype.transferToImmutable` (Stage 3 proposal).
- `$262.detachArrayBuffer` in the test262 harness (the remaining detached
  tests in `built-ins/ArrayBuffer/prototype/*` call `$262.detachArrayBuffer`).
