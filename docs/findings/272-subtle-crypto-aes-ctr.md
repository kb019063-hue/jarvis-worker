# Finding 272: `SubtleCrypto` had no AES-CTR encryption/decryption

## What was found

`SubtleCrypto` supported hashing (`digest`), `HMAC` (`importKey`/`exportKey`/`generateKey`/`sign`/`verify`), `PBKDF2`, and `HKDF`, but had no `encrypt()` or `decrypt()` for `AES-CTR`.

## Why it matters

Scripts that encrypt or decrypt data with `AES-CTR` via `crypto.subtle` could not do so.

## What the fix does

Adds `aes_encrypt_block()` (FIPS 197), `aes_ctr_crypt()` (NIST SP 800-38A), and `'AES-CTR'` support in `importKey()`, `generateKey()`, `encrypt()`, and `decrypt()` in `src/js/DomCryptoBindings.cpp`.

## What is still open

- `AES-CBC`, `AES-GCM`, and asymmetric (`ECDSA`, `RSA-PSS`) algorithms on `SubtleCrypto`.
