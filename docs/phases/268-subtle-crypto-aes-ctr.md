# Phase 268: `SubtleCrypto` AES-CTR (`generateKey`, `importKey`, `exportKey`, `encrypt`, `decrypt`)

## What was built

Self-contained FIPS 197 AES-128/192/256 and NIST SP 800-38A CTR mode are implemented in `src/js/DomCryptoBindings.cpp`:

- `importKey('raw', keyBytes, 'AES-CTR', extractable, keyUsages)` and `exportKey('raw', key)`.
- `generateKey({ name: 'AES-CTR', length: 128 | 192 | 256 }, extractable, keyUsages)`.
- `encrypt({ name: 'AES-CTR', counter, length }, key, data)` and `decrypt(...)` with 16-byte counter block and `1..128` counter bit length.

## Verification

- `make test`: all 405 engine test suites pass, including `subtle_crypto_aes_ctr_encrypt_and_decrypt` (verified against NIST SP 800-38A F.5.1 CTR-AES128).
