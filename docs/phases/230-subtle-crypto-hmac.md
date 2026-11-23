# Phase 230: `SubtleCrypto` HMAC (`importKey`, `sign`, `verify`)

## What was built

`CryptoKey` and `SubtleCrypto.importKey('raw', ...)`, `sign('HMAC', ...)`, and `verify('HMAC', ...)` are implemented in `src/js/DomCryptoBindings.cpp`:

- `CryptoKey`: exposes `type` (`"secret"`) and `extractable`.
- `importKey('raw', keyData, { name: 'HMAC', hash }, extractable, keyUsages)`: supports `SHA-1`, `SHA-256`, `SHA-384`, and `SHA-512`.
- `sign('HMAC', key, data)` and `verify('HMAC', key, signature, data)`: compute RFC 2104 HMAC with constant-time tag comparison in `verify`.

## Verification

- `make test`: all 373 engine test suites pass, including `subtle_crypto_hmac_sign_and_verify` (verified against RFC 4231 Test Case 2).
