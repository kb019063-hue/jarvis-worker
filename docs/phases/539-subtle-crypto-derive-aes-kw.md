# Phase 539: `SubtleCrypto.deriveKey()` `AES-KW` target

## What was built

`SubtleCrypto.deriveKey()` in `src/js/DomCryptoBindings.cpp` now accepts `AES-KW` (128/192/256-bit) as `derivedKeyType` for `ECDH` (`P-256`), allowing scripts to derive key-wrapping keys directly from `ECDH` key agreement.

## Verification

- `make test`: all 570 engine test suites pass, including `subtle_crypto_derive_aes_kw` (`tests/suites/539-subtle-crypto-derive-aes-kw.cpp`).
