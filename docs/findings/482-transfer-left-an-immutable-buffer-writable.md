# Finding 482: Transfer left an immutable buffer writable

`ArrayBuffer.prototype.transfer()` and `.transferToFixedLength()` could move and detach bytes, but there was no path to create a buffer that permanently forbids mutation. Returning an ordinary buffer from a `transferToImmutable` method would have transferred ownership without making the bytes immutable.

Phase 482 adds the proposal-stage `ArrayBuffer.prototype.transferToImmutable()` and `ArrayBuffer.prototype.immutable`. The source detaches, while the fixed-length result retains readable bytes and rejects resize, transfer, typed-array writes/mutators, and DataView writes. The typed-array index descriptor also reflects its read-only, non-configurable status.

The feature follows the [TC39 Immutable ArrayBuffers proposal](https://github.com/tc39/proposal-immutable-arraybuffer) (Stage 2.7 as of November 2026). It is intentionally reported as proposal support, not as an ES2025 feature. Node v22.22.3 does not implement it, so validation is against the proposal algorithm and explicit engine-suite assertions rather than a Node differential.
