# Phase 216: `SubtleCrypto.digest()` (`SHA-256`)

## What was built

`SubtleCrypto` (`crypto.subtle`) and `crypto.subtle.digest(algorithm, data)` are implemented in `src/js/DomCryptoBindings.cpp`:

- Exposes `crypto.subtle` on `Crypto`.
- `digest(algorithm, data)` accepts `'SHA-256'` or `{ name: 'SHA-256' }` (case-insensitive) and a `BufferSource` (`ArrayBuffer`, `TypedArray`, or `DataView`).
- Computes the FIPS 180-4 SHA-256 digest and returns a `Promise` resolving to a 32-byte `ArrayBuffer`, or rejecting with `NotSupportedError` / `TypeError` for unsupported algorithms or invalid arguments.

## Verification

- `make test`: all 355 engine test suites pass, including `subtle_crypto_digest_sha256`.
