# Finding 373: `SubtleCrypto` had no `'jwk'` key format support

## What was found

`SubtleCrypto.importKey()` and `exportKey()` only supported `'raw'` format; `'jwk'` was rejected with `NotSupportedError`.

## Why it matters

Scripts that import or export symmetric keys in JSON Web Key (`jwk`) format could not do so.

## What the fix does

Adds `base64url_encode()` / `base64url_decode()` and `'jwk'` format support for `kty: 'oct'` keys in `importKey()` and `exportKey()` in `src/js/DomCryptoBindings.cpp`.

## What is still open

- Asymmetric (`ECDSA`, `RSA-PSS`, `ECDH`) algorithms and PKCS8/SPKI key formats on `SubtleCrypto`.
