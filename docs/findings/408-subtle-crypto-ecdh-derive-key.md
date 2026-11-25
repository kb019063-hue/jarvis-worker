# Finding 408: `SubtleCrypto.deriveKey()` did not support `ECDH`

## What was found

`SubtleCrypto.deriveBits()` supported `ECDH` (`P-256`, Phase 378), but `deriveKey()` only accepted `PBKDF2` and `HKDF`.

## Why it matters

Scripts that derive an `AES-GCM` / `AES-CBC` / `AES-CTR` / `HMAC` `CryptoKey` directly from `ECDH` via `crypto.subtle.deriveKey()` could not do so.

## What the fix does

Adds `'ecdh'` handling to `SubtleCrypto.deriveKey()` in `src/js/DomCryptoBindings.cpp`.

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` and PKCS8/SPKI key formats on `SubtleCrypto`.
