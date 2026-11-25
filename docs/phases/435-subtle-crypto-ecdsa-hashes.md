# Phase 435: `SubtleCrypto` `ECDSA` `SHA-384` and `SHA-512` support

## What was built

`SubtleCrypto.sign()` and `verify()` for `ECDSA` (`P-256`) in `src/js/DomCryptoBindings.cpp` now read `algorithm.hash` and support `SHA-256`, `SHA-384`, and `SHA-512` (truncating the digest to 256 bits for `P-256` per FIPS 186-4 / WebCrypto).

## Verification

- `make test`: all 486 engine test suites pass, including `subtle_crypto_ecdsa_sha384_and_sha512`.
