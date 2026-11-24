# Finding 291: `SubtleCrypto` had no AES-CBC encryption/decryption

## What was found

`SubtleCrypto` (Phases 268 & 277) supported `AES-CTR` and `AES-GCM`, but `AES-CBC` was not implemented.

## Why it matters

Scripts that encrypt or decrypt data with `AES-CBC` via `crypto.subtle.encrypt()` / `decrypt()` could not do so.

## What the fix does

Adds `aes_decrypt_block()`, `aes_cbc_encrypt()`, and `aes_cbc_decrypt()` in `src/js/DomCryptoBindings.cpp`, and wires `'AES-CBC'` into `importKey()`, `generateKey()`, `encrypt()`, and `decrypt()`.

## What is still open

- Asymmetric (`ECDSA`, `RSA-PSS`, `ECDH`) algorithms and JWK key format on `SubtleCrypto`.
