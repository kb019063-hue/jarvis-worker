# Finding 422: `SubtleCrypto` had no `AES-KW` key wrapping

## What was found

`SubtleCrypto.wrapKey()` and `unwrapKey()` supported `AES-GCM`, `AES-CBC`, and `AES-CTR`, but `AES-KW` (RFC 3394) was not implemented.

## Why it matters

Scripts that wrap or unwrap symmetric keys using `AES-KW` via `crypto.subtle.wrapKey()` / `unwrapKey()` could not do so.

## What the fix does

Adds `aes_kw_wrap()` and `aes_kw_unwrap()` in `src/js/DomCryptoBindings.cpp`, and wires `'AES-KW'` into `importKey()`, `exportKey()`, `generateKey()`, `wrapKey()`, and `unwrapKey()`.

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` on `SubtleCrypto`.
