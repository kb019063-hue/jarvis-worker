# 162. `ArrayBuffer` checked `detached`, but nothing could detach or resize it

**Status: fixed for `transfer()`, `transferToFixedLength()`, `detached`, `resizable`, `maxByteLength`, and `resize()`; `SharedArrayBuffer` and `BigInt` views remain open.**

## What was wrong

`ArrayBufferData` held a `detached` boolean checked throughout
`src/js/TypedArray.cpp`, yet no JavaScript API could set it: `transfer()`,
`transferToFixedLength()`, `detached`, `resizable`, `maxByteLength`, and
`resize()` were missing. Furthermore, `byteLength`/`byteOffset`/`length`/
`buffer` lived in exotic `get()` hooks instead of prototype accessors,
`DataView` did not throw `TypeError` on detached/out-of-bounds `byteLength` or
`byteOffset`, and iterating `TypedArray` methods pre-snapshotted elements
instead of reading live across buffer resizes.

## What changed

Implemented ES2024 `ArrayBuffer` detachment and resizing in
`src/js/TypedArray.cpp`: `{ maxByteLength }`, `resize()`, `transfer()`,
`transferToFixedLength()`, non-constructor prototype accessors on
`ArrayBuffer.prototype` / `DataView.prototype` / `%TypedArray%.prototype`,
length-tracking and out-of-bounds views, live element reads in iterating
`TypedArray` methods, and post-conversion re-validation for re-entrant
`valueOf()` / callbacks.

See [`docs/phases/157-arraybuffer-transfer-and-resize.md`](../phases/157-arraybuffer-transfer-and-resize.md).

## How it was verified

- Official test262 (`2e58f87d8350`) paired against `main` (`11b1ad3`):
  **+287 gained, 0 lost** (`1,064 → 1,351 / 2,972`, `35.8% → 45.5%`, timeouts
  `1 → 0`) across `built-ins/ArrayBuffer`, `built-ins/DataView`,
  `built-ins/TypedArray`, and `built-ins/TypedArrayConstructors`.
- 59/59 differential probes match Node.js v22.22.3 byte-for-byte.
- `make test` (**314/314** suites, including
  `arraybuffer_transfer_detach_and_resize`), `make js-conformance` (1144/1158),
  `make gc-oracle` (PASS).

## What is still open

- `SharedArrayBuffer`, `BigInt64`/`BigUint64` views, `[Symbol.species]`, and
  `transferToImmutable`.
