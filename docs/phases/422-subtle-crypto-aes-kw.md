# Phase 422: `SubtleCrypto` `AES-KW` (`wrapKey` and `unwrapKey`)

## What was built

Self-contained RFC 3394 AES Key Wrap (`AES-KW`, 128/192/256-bit) is implemented in `src/js/DomCryptoBindings.cpp`:

- `aes_kw_wrap()` and `aes_kw_unwrap()` with constant-time `0xA6A6A6A6A6A6A6A6` integrity check.
- Wired `'AES-KW'` into `importKey()`, `exportKey()` (`A128KW`/`A192KW`/`A256KW` JWK `alg`), `generateKey()`, `wrapKey()`, and `unwrapKey()`.

## Verification

- `make test`: all 477 engine test suites pass, including `subtle_crypto_aes_kw_wrap_and_unwrap` (verified against RFC 3394 Section 4.1).
