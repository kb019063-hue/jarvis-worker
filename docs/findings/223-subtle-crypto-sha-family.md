# Finding 223: `SubtleCrypto.digest()` only supported `SHA-256`

## What was found

`crypto.subtle.digest()` (Phase 216) supported `SHA-256` and rejected `SHA-1`, `SHA-384`, and `SHA-512` with `NotSupportedError`.

## Why it matters

Scripts that compute `SHA-1`, `SHA-384`, or `SHA-512` digests via `crypto.subtle.digest()` could not do so.

## What the fix does

Adds `sha1_bytes()` and `sha512_family_bytes()` in `src/js/DomCryptoBindings.cpp` and dispatches `'sha-1'`, `'sha-384'`, and `'sha-512'` in `SubtleCrypto.digest()`.

## What is still open

- Key import/export, HMAC, AES, and ECDSA/RSA operations on `SubtleCrypto`.
