# Phase 286: `SubtleCrypto` AES-CBC (`generateKey`, `importKey`, `exportKey`, `encrypt`, `decrypt`)

## What was built

Self-contained NIST SP 800-38A AES-CBC (128/192/256-bit) with PKCS#7 padding is implemented in `src/js/DomCryptoBindings.cpp`:

- `aes_decrypt_block()` (FIPS 197 inverse cipher: InvShiftRows, InvSubBytes, InvMixColumns, AddRoundKey).
- `aes_cbc_encrypt()` and `aes_cbc_decrypt()` with 16-byte IV validation and PKCS#7 padding/unpadding (rejecting invalid padding with `OperationError`).
- Wired `'AES-CBC'` into `importKey('raw', ...)`, `generateKey()`, `encrypt()`, and `decrypt()`.

## Verification

- `make test`: all 419 engine test suites pass, including `subtle_crypto_aes_cbc_encrypt_and_decrypt` (verified against NIST SP 800-38A F.2.1 CBC-AES128).
