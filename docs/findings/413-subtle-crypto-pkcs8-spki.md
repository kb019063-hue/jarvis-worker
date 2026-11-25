# Finding 413: `SubtleCrypto` had no `'pkcs8'` or `'spki'` key format support

## What was found

`SubtleCrypto.importKey()` and `exportKey()` supported `'raw'` and `'jwk'` formats, but `'pkcs8'` and `'spki'` were rejected with `NotSupportedError`.

## Why it matters

Scripts that import or export `ECDSA` / `ECDH` (`P-256`) keys in standard DER `'spki'` or `'pkcs8'` format could not do so.

## What the fix does

Adds `'spki'` and `'pkcs8'` DER encoding/decoding for `P-256` keys in `importKey()` and `exportKey()` in `src/js/DomCryptoBindings.cpp`.

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` on `SubtleCrypto`.
