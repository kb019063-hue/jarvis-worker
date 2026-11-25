# Phase 408: `SubtleCrypto.deriveKey()` for `ECDH` (`P-256`)

## What was built

`SubtleCrypto.deriveKey()` in `src/js/DomCryptoBindings.cpp` now supports `ECDH` (`P-256`):

- Derives `AES-GCM`, `AES-CBC`, `AES-CTR` (128/192/256-bit), or `HMAC` (`SHA-1`/`256`/`384`/`512`) `CryptoKey` objects directly from an `ECDH` private key and peer `ECDH` public key.

## Verification

- `make test`: all 463 engine test suites pass, including `subtle_crypto_ecdh_p256_derive_key` (end-to-end ECDH key agreement -> AES-GCM encrypt/decrypt).
