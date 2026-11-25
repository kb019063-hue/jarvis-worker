# Finding 437: `CryptoKey` lacked `.algorithm` and `.usages` getters

## What was found

`CryptoKey` exposed `.type` and `.extractable`, but `.algorithm` and `.usages` were `undefined`.

## Why it matters

Scripts that inspect `key.algorithm` or `key.usages` on keys returned by `SubtleCrypto.generateKey()`, `importKey()`, or `deriveKey()` could not do so.

## What the fix does

Adds `algorithm` and `usages` getters to `setup_crypto_key()` in `src/js/DomCryptoBindings.cpp` and populates `usages` on created `CryptoKey`s.

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` on `SubtleCrypto`.
