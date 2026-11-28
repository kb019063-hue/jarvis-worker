# Phase 549: `SubtleCrypto.deriveKey()` `AES-KW` target for `PBKDF2` and `HKDF`

## What was built

`SubtleCrypto.deriveKey()` for `PBKDF2` and `HKDF` in `src/js/DomCryptoBindings.cpp` now accepts `AES-KW` (128/192/256-bit) as `derivedKeyType`, allowing scripts to derive key-wrapping keys from passwords or HKDF secrets.

## Verification

- `make test`: all 576 engine test suites pass, including `subtle_crypto_kdf_derive_aes_kw` (`tests/suites/549-subtle-crypto-kdf-aes-kw.cpp`).
