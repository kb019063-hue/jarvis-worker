# Phase 437: `CryptoKey.algorithm` and `CryptoKey.usages` getters

## What was built

`CryptoKey.algorithm` and `CryptoKey.usages` getters are implemented in `src/js/DomCryptoBindings.cpp`:

- `CryptoKey.algorithm` returns an object describing the key's algorithm (`{ name, length }` for AES, `{ name, namedCurve }` for EC, `{ name, hash: { name } }` for HMAC, `{ name }` for PBKDF2/HKDF).
- `CryptoKey.usages` returns an array of the key's permitted usages, populated by `importKey()`, `generateKey()`, and `deriveKey()`.

## Verification

- `make test`: all 492 engine test suites pass, including `crypto_key_algorithm_and_usages`.
