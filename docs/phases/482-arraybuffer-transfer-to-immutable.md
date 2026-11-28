# Phase 482: ArrayBuffer transferToImmutable

**Task:** Finish the `transferToImmutable` item in the DataView / `%TypedArray%` row.

`ArrayBuffer.prototype.transferToImmutable` was absent. The relevant upstream work is the [TC39 Immutable ArrayBuffers proposal](https://github.com/tc39/proposal-immutable-arraybuffer), currently Stage 2.7; it is not part of ES2025, and Node v22.22.3 has no implementation to use as an oracle.

The new method follows the proposal's `ArrayBufferCopyAndDetach` path: optional length is converted with `ToIndex`, bytes are copied up to the shorter length, extension bytes are zero-initialized, and the source is detached. The result is a fixed-length ArrayBuffer with `immutable === true` and `maxByteLength === byteLength`. The accessor is false on ordinary buffers. An immutable result cannot be resized or transferred/detached again.

The bytes remain readable through `DataView` and typed-array views. Indexed assignment is refused (a strict assignment throws), index descriptors report non-writable and non-configurable, and typed-array mutators (`set`, `fill`, `copyWithin`, `reverse`, and `sort`) plus DataView setters and `TextEncoder.encodeInto` refuse to alter the contents. Same-value and empty indexed property definitions remain permitted; incompatible redefinitions are rejected. Freezing a typed-array view succeeds.

Validation: `make -j2 test` passed **524/524 engine suites**; the cross-origin suite passed **114/114 checks**. The new suite is `tests/suites/482-arraybuffer-transfer-to-immutable.cpp`.
