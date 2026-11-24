# Phase 247: `SubtleCrypto` PBKDF2 (`importKey`, `deriveBits`, `deriveKey`)

## What was built

PBKDF2 key derivation is implemented in `src/js/DomCryptoBindings.cpp`:

- `importKey('raw', passwordBytes, 'PBKDF2', extractable, keyUsages)` imports a base `CryptoKey` for PBKDF2.
- `deriveBits({ name: 'PBKDF2', salt, iterations, hash }, baseKey, length)` derives `length` bits using RFC 2898 / RFC 8018 PBKDF2 with `SHA-1`, `SHA-256`, `SHA-384`, or `SHA-512` and returns a `Promise<ArrayBuffer>`.
- `deriveKey({ name: 'PBKDF2', salt, iterations, hash }, baseKey, { name: 'HMAC', hash, length? }, extractable, keyUsages)` derives an HMAC `CryptoKey`.

## Verification

- `make test`: all 394 engine test suites pass, including `subtle_crypto_pbkdf2_derive_bits_and_key` (verified against RFC 6070 / RFC 7914 PBKDF2-HMAC-SHA256 `"password"` / `"salt"` vector).
