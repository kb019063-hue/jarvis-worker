# Phase 532: `SubtleCrypto` `ECDSA` JWK `alg` (`ES256`)

## What was built

`SubtleCrypto.exportKey('jwk', ...)` and `importKey('jwk', ...)` for `ECDSA` (`P-256`) in `src/js/DomCryptoBindings.cpp` now handle `alg: 'ES256'`:

- `exportKey('jwk', ecdsaKey)` sets `alg: 'ES256'`.
- `importKey('jwk', jwk, ...)` validates `jwk.alg === 'ES256'` when present for `ECDSA` `P-256`, rejecting mismatches with `DataError`.

## Verification

- `make test`: all 562 engine test suites pass, including `subtle_crypto_ecdsa_jwk_alg_es256` (`tests/suites/532-subtle-crypto-ecdsa-jwk-alg.cpp`).
