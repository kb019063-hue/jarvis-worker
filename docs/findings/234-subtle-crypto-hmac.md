# Finding 234: `SubtleCrypto` had no HMAC `importKey` / `sign` / `verify`

## What was found

`crypto.subtle.digest()` (Phases 216 & 219) supported SHA-1/256/384/512, but `CryptoKey`, `importKey()`, `sign()`, and `verify()` were not implemented.

## Why it matters

Scripts that authenticate or verify payloads with HMAC via `crypto.subtle` could not do so.

## What the fix does

Adds `CryptoKey` (`CryptoKeyState`, `setup_crypto_key`), `hmac_bytes()`, and `SubtleCrypto.importKey` / `sign` / `verify` for `'HMAC'` in `src/js/DomCryptoBindings.cpp`.

## What is still open

- `exportKey()`, `generateKey()`, AES, and asymmetric algorithms (ECDSA/RSA) on `SubtleCrypto`.
