# Finding 251: `SubtleCrypto` had no PBKDF2 key derivation

## What was found

`SubtleCrypto` supported `digest()`, `importKey('raw', ..., 'HMAC')`, `exportKey('raw', ...)`, `generateKey('HMAC', ...)`, `sign()`, and `verify()`, but had no `deriveBits()` or `deriveKey()` for PBKDF2.

## Why it matters

Scripts that derive keys or bit strings from passwords via `crypto.subtle.deriveBits` / `deriveKey` with PBKDF2 could not do so.

## What the fix does

Adds `pbkdf2_bytes()` (RFC 2898 / RFC 8018), `'PBKDF2'` support in `importKey()`, and `deriveBits()` / `deriveKey()` in `src/js/DomCryptoBindings.cpp`.

## What is still open

- `HKDF` key derivation and `AES` / asymmetric algorithms on `SubtleCrypto`.
