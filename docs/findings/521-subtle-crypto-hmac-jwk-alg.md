# Finding 521: `SubtleCrypto` `HMAC` JWK import ignored `jwk.alg` and export omitted `HS1`

## What was found

`SubtleCrypto.importKey('jwk', ...)` for `HMAC` did not validate `jwk.alg` against `algorithm.hash`, and `exportKey('jwk', ...)` did not set `alg: 'HS1'` for `HMAC` `SHA-1` keys.

## Why it matters

Scripts that rely on `HS1` export or `DataError` rejection on mismatched `HMAC` JWK `alg` fields saw missing `alg` or silent acceptance.

## What the fix does

Validates `jwk.alg` (`HS1`, `HS256`, `HS384`, `HS512`) in `importKey('jwk', ...)` and exports `alg: 'HS1'` for `SHA-1` in `exportKey('jwk', ...)` in `src/js/DomCryptoBindings.cpp`.

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` on `SubtleCrypto`.
