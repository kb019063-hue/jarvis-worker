# Phase 521: `SubtleCrypto` `HMAC` JWK `alg` validation

## What was built

`SubtleCrypto.importKey('jwk', ...)` and `exportKey('jwk', ...)` for `HMAC` in `src/js/DomCryptoBindings.cpp` now handle `HS1`/`HS256`/`HS384`/`HS512`:

- `importKey('jwk', ...)` validates `jwk.alg` against the target `HMAC` hash (`HS1` for `SHA-1`, `HS256` for `SHA-256`, `HS384` for `SHA-384`, `HS512` for `SHA-512`), rejecting mismatches with `DataError`.
- `exportKey('jwk', ...)` exports `alg: 'HS1'` for `HMAC` `SHA-1` keys.

## Verification

- `make test`: all 560 engine test suites pass, including `subtle_crypto_hmac_jwk_alg_validation` (`tests/suites/521-subtle-crypto-hmac-jwk-alg.cpp`).
