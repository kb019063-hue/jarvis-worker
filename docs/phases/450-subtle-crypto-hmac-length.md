# Phase 450: `CryptoKey.algorithm.length` for `HMAC`

## What was built

`CryptoKey.algorithm` in `src/js/DomCryptoBindings.cpp` now includes `length` (in bits, `key_bytes.size() * 8`) for `HMAC` keys, matching the WebCrypto `HmacKeyAlgorithm` dictionary.

## Verification

- `make test`: all 498 engine test suites pass, including `subtle_crypto_hmac_length`.
