# Finding 264: `SubtleCrypto` had no HKDF key derivation

## What was found

`SubtleCrypto` (Phase 247) supported `PBKDF2` in `deriveBits()` / `deriveKey()`, but `HKDF` was not implemented.

## Why it matters

Scripts that derive keys or bit strings using HKDF (RFC 5869) via `crypto.subtle.deriveBits` / `deriveKey` could not do so.

## What the fix does

Adds `hkdf_bytes()` (RFC 5869 Extract + Expand) and `'HKDF'` support in `importKey()`, `deriveBits()`, and `deriveKey()` in `src/js/DomCryptoBindings.cpp`.

## What is still open

- `AES` (`AES-GCM`, `AES-CBC`, `AES-CTR`) and asymmetric (`ECDSA`, `RSA-PSS`) algorithms on `SubtleCrypto`.
