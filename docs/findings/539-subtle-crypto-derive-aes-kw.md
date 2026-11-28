# Finding 539: `SubtleCrypto.deriveKey()` rejected `AES-KW` as `derivedKeyType`

## What was found

`SubtleCrypto.deriveKey()` for `ECDH` accepted `AES-GCM`, `AES-CBC`, `AES-CTR`, and `HMAC` as `derivedKeyType`, but rejected `AES-KW` with `NotSupportedError`.

## Why it matters

Scripts that derive an `AES-KW` key-wrapping key directly from `ECDH` key agreement could not do so.

## What the fix does

Adds `"aes-kw"` to the accepted `derivedKeyType` algorithms in `SubtleCrypto.deriveKey()` in `src/js/DomCryptoBindings.cpp`.

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` on `SubtleCrypto`.
