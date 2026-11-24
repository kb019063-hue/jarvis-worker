# Finding 362: `SubtleCrypto` had no `wrapKey()` or `unwrapKey()`

## What was found

`SubtleCrypto` supported `encrypt`, `decrypt`, `importKey`, `exportKey`, and `generateKey`, but `wrapKey()` and `unwrapKey()` were not implemented.

## Why it matters

Scripts that wrap or unwrap symmetric keys using a key-encryption key (KEK) via `crypto.subtle.wrapKey()` / `unwrapKey()` could not do so.

## What the fix does

Adds `wrapKey()` and `unwrapKey()` to `SubtleCrypto` in `src/js/DomCryptoBindings.cpp` for `'raw'` format with `AES-GCM`, `AES-CBC`, and `AES-CTR` wrapping keys.

## What is still open

- JWK/PKCS8/SPKI key formats and asymmetric (`ECDSA`, `RSA-PSS`, `ECDH`) algorithms on `SubtleCrypto`.
