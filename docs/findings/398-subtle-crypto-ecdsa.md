# Finding 398: `SubtleCrypto` had no ECDSA signing or verification

## What was found

`SubtleCrypto` supported `ECDH` (`P-256`, Phase 378) and `HMAC` signing, but `ECDSA` was not implemented.

## Why it matters

Scripts that generate `ECDSA` (`P-256`) key pairs and sign or verify messages via `crypto.subtle.sign()` / `verify()` could not do so.

## What the fix does

Adds scalar arithmetic modulo $n$ and `ecdsa_sign()` / `ecdsa_verify()` to `namespace p256` in `src/js/DomCryptoBindings.cpp`, and wires `'ECDSA'` (`namedCurve: 'P-256'`) into `generateKey()`, `importKey('raw', ...)`, `sign()`, and `verify()`.

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` and PKCS8/SPKI key formats on `SubtleCrypto`.
