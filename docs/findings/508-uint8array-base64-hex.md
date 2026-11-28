# Finding 508 — `Uint8Array` had no base64 or hex

**Severity:** missing API, stage 3 TC39 proposal. Binary-to-text helpers required manual loops or `btoa` with a Latin-1 intermediate.

**Found by:** checking `typeof Uint8Array.prototype.toHex` and `Uint8Array.fromBase64` — both `undefined` where the proposal says `function`.

**Repro:**

```js
new Uint8Array([72,101,108,108,111]).toHex() // TypeError: is not a function
Uint8Array.fromBase64("SGVsbG8=") // TypeError: is not a function
```

**What changed:** `src/js/TypedArray.cpp` implements the six `Uint8Array` methods per `tc39.es/proposal-arraybuffer-base64`: base64 encode/decode with `alphabet`/`omitPadding`/`lastChunkHandling` and `SyntaxError` on invalid input, hex encode/decode with odd-length and digit validation, and `setFrom*` truncation.

**Still open:** `ArrayBuffer` base64/hex not implemented; `SharedArrayBuffer` not implemented.
