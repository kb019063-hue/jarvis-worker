# Finding 400: `SubtleCrypto` had no EC JWK (`kty: 'EC'`) import/export

## What was found

`SubtleCrypto.importKey('jwk', ...)` and `exportKey('jwk', ...)` only supported symmetric `kty: 'oct'` keys; `kty: 'EC'` (`P-256`) was rejected.

## Why it matters

Scripts that import or export `ECDSA` or `ECDH` (`P-256`) keys in JWK format could not do so.

## What the fix does

Extends `importKey('jwk', ...)` and `exportKey('jwk', ...)` in `src/js/DomCryptoBindings.cpp` to handle `kty: 'EC'`, `crv: 'P-256'` (`x`, `y`, and optional `d` base64url coordinates).

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` and PKCS8/SPKI key formats on `SubtleCrypto`.
