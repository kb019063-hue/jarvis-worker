# Finding 243: `SubtleCrypto` had no `exportKey()` or `generateKey()`

## What was found

`SubtleCrypto` (Phase 230) supported `importKey('raw', ...)`, `sign('HMAC', ...)`, and `verify('HMAC', ...)`, but `exportKey()` and `generateKey()` were not implemented.

## Why it matters

Scripts that generate fresh HMAC keys or export raw key material via `crypto.subtle` could not do so.

## What the fix does

Adds `exportKey('raw', key)` and `generateKey({ name: 'HMAC', hash, length? }, extractable, keyUsages)` in `src/js/DomCryptoBindings.cpp`.

## What is still open

- AES (`AES-GCM`, `AES-CBC`, `AES-CTR`) and asymmetric (`ECDSA`, `RSA-PSS`) algorithms on `SubtleCrypto`.
