# Phase 460: `SubtleCrypto` `ECDSA` `SHA-1` support

## What was built

`SubtleCrypto.sign()` and `verify()` for `ECDSA` (`P-256`) in `src/js/DomCryptoBindings.cpp` now support `SHA-1` (in addition to `SHA-256`, `SHA-384`, and `SHA-512`), left-padding the 20-byte `SHA-1` digest with 12 zero bytes to 32 bytes for `P-256` scalar arithmetic.

## Verification

- `make test`: all 506 engine test suites pass, including `subtle_crypto_ecdsa_sha1`.
