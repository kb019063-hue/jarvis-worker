# Finding 281: `SubtleCrypto` had no AES-GCM authenticated encryption

## What was found

`SubtleCrypto` (Phase 268) supported `AES-CTR`, but `AES-GCM` was not implemented.

## Why it matters

Scripts that use authenticated encryption (`AES-GCM`) via `crypto.subtle.encrypt()` / `decrypt()` could not do so.

## What the fix does

Adds `gcm_gf_mul()`, `gcm_ghash()`, `aes_gcm_encrypt()`, and `aes_gcm_decrypt()` in `src/js/DomCryptoBindings.cpp`, and wires `'AES-GCM'` into `importKey()`, `generateKey()`, `encrypt()`, and `decrypt()`.

## What is still open

- `AES-CBC` and asymmetric (`ECDSA`, `RSA-PSS`) algorithms on `SubtleCrypto`.
