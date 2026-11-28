# Finding 549: `SubtleCrypto.deriveKey()` for `PBKDF2`/`HKDF` rejected `AES-KW`

## What was found

`SubtleCrypto.deriveKey()` for `PBKDF2` and `HKDF` only accepted `HMAC` as `derivedKeyType`, rejecting `AES-KW` with `NotSupportedError`.

## Why it matters

Scripts that derive an `AES-KW` key-wrapping key from a password (`PBKDF2`) or `HKDF` secret could not do so.

## What the fix does

Adds `"aes-kw"` (128/192/256-bit) support to `PBKDF2` and `HKDF` `deriveKey()` in `src/js/DomCryptoBindings.cpp`.

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` on `SubtleCrypto`.
