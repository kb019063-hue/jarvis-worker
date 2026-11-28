# Phase 515: `SubtleCrypto` JWK `alg` validation

## What was built

`SubtleCrypto.importKey('jwk', ...)` in `src/js/DomCryptoBindings.cpp` now validates `jwk.alg` when present against the target AES algorithm (`A128GCM`/`A192GCM`/`A256GCM`, `A128CBC`/`A192CBC`/`A256CBC`, `A128CTR`/`A192CTR`/`A256CTR`, `A128KW`/`A192KW`/`A256KW`) and key length, rejecting mismatches with `DataError`.

## Verification

- `make test`: all 554 engine test suites pass, including `subtle_crypto_jwk_alg_validation` (`tests/suites/515-subtle-crypto-jwk-alg.cpp`).
