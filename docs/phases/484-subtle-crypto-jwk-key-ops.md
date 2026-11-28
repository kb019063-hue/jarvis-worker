# Phase 484: `SubtleCrypto` JWK `key_ops` and `ext` validation

## What was built

`SubtleCrypto.exportKey('jwk', ...)` and `importKey('jwk', ...)` in `src/js/DomCryptoBindings.cpp` now handle `key_ops` and `ext` per the WebCrypto specification:

- `exportKey('jwk', key)` includes `key_ops` populated from `key.usages`.
- `importKey('jwk', jwk, alg, extractable, usages)` rejects with `DataError` when `jwk.ext === false` and `extractable === true`.

## Verification

- `make test`: all 524 engine test suites pass, including `subtle_crypto_jwk_key_ops_and_ext`.
