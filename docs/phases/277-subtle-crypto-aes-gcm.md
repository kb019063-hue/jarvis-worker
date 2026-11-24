# Phase 277: `SubtleCrypto` AES-GCM (`generateKey`, `importKey`, `exportKey`, `encrypt`, `decrypt`)

## What was built

Self-contained NIST SP 800-38D AES-GCM (128/192/256-bit) is implemented in `src/js/DomCryptoBindings.cpp`:

- `importKey('raw', keyBytes, 'AES-GCM', extractable, keyUsages)` and `exportKey('raw', key)`.
- `generateKey({ name: 'AES-GCM', length: 128 | 192 | 256 }, extractable, keyUsages)`.
- `encrypt({ name: 'AES-GCM', iv, additionalData?, tagLength? }, key, data)` and `decrypt(...)` with GF(2^128) GHASH, 96-bit or arbitrary-length IVs, and constant-time tag verification (rejecting with `OperationError` on tag mismatch).

## Verification

- `make test`: all 410 engine test suites pass, including `subtle_crypto_aes_gcm_encrypt_and_decrypt` (verified against NIST GCM Test Case 3 and tamper rejection).
