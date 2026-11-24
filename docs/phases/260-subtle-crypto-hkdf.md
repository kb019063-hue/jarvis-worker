# Phase 260: `SubtleCrypto` HKDF (`importKey`, `deriveBits`, `deriveKey`)

## What was built

HKDF (RFC 5869) key derivation is implemented in `src/js/DomCryptoBindings.cpp`:

- `importKey('raw', ikm, 'HKDF', extractable, keyUsages)` imports a base `CryptoKey` for HKDF.
- `deriveBits({ name: 'HKDF', hash, salt, info }, baseKey, length)` runs HKDF-Extract and HKDF-Expand with `SHA-1`, `SHA-256`, `SHA-384`, or `SHA-512` and returns a `Promise<ArrayBuffer>`.
- `deriveKey({ name: 'HKDF', hash, salt, info }, baseKey, { name: 'HMAC', hash, length? }, extractable, keyUsages)` derives an HMAC `CryptoKey`.

## Verification

- `make test`: all 396 engine test suites pass, including `subtle_crypto_hkdf_derive_bits_and_key` (verified against RFC 5869 Test Case 1).
