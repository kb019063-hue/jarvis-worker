# Finding 220: `crypto.subtle.digest()` was not implemented

## What was found

`window.crypto` had `getRandomValues()` and `randomUUID()` (Phase 185), but `crypto.subtle` (`SubtleCrypto.digest()`) was missing.

## Why it matters

Scripts that hash data with `crypto.subtle.digest('SHA-256', data)` could not do so.

## What the fix does

Adds `SubtleCrypto` and `setup_subtle_crypto` with a self-contained FIPS 180-4 SHA-256 implementation in `src/js/DomCryptoBindings.cpp`, exposed via `crypto.subtle`.

## What is still open

- `SHA-1`, `SHA-384`, `SHA-512`, and key/sign/encrypt operations on `SubtleCrypto`.
