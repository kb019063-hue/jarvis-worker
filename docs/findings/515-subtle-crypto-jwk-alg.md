# Finding 515: `SubtleCrypto.importKey('jwk', ...)` ignored `jwk.alg`

## What was found

`SubtleCrypto.importKey('jwk', ...)` did not check `jwk.alg` against the requested algorithm and key length.

## Why it matters

Scripts that rely on WebCrypto rejecting mismatched JWK `alg` fields (e.g. importing an `A128CBC` JWK as `AES-GCM`) did not receive `DataError`.

## What the fix does

Validates `jwk.alg` in `SubtleCrypto.importKey('jwk', ...)` for AES algorithms in `src/js/DomCryptoBindings.cpp`, rejecting mismatches with `DataError`.

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` on `SubtleCrypto`.
