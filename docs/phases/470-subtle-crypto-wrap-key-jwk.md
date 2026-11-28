# Phase 470: `SubtleCrypto.wrapKey('jwk', ...)` and `unwrapKey('jwk', ...)`

## What was built

`SubtleCrypto.wrapKey()` and `unwrapKey()` in `src/js/DomCryptoBindings.cpp` now support `'jwk'` format for both symmetric (`kty: 'oct'`) and EC `P-256` (`kty: 'EC'`) keys:

- `wrapKey('jwk', key, wrappingKey, wrapAlgo)` serializes the key as JWK JSON and encrypts it with `AES-GCM`, `AES-CBC`, `AES-CTR`, or `AES-KW`.
- `unwrapKey('jwk', wrappedKey, unwrappingKey, unwrapAlgo, unwrappedKeyAlgo, extractable, keyUsages)` decrypts the JWK JSON and imports the resulting `CryptoKey` with `usages` populated.

## Verification

- `make test`: all 521 engine test suites pass, including `subtle_crypto_wrap_and_unwrap_jwk`.
