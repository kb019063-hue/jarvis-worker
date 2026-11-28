# Finding 532: `SubtleCrypto` `ECDSA` JWK export omitted `alg` and import ignored `jwk.alg`

## What was found

`SubtleCrypto.exportKey('jwk', ...)` for `ECDSA` (`P-256`) did not set `alg: 'ES256'`, and `importKey('jwk', ...)` did not validate `jwk.alg` against `ES256`.

## Why it matters

Scripts that inspect `jwk.alg` on exported `ECDSA` `P-256` keys or rely on `DataError` rejection for mismatched `jwk.alg` saw missing `alg` or silent acceptance.

## What the fix does

Sets `alg: 'ES256'` in `exportKey('jwk', ...)` and validates `jwk.alg === 'ES256'` in `importKey('jwk', ...)` for `ECDSA` `P-256` in `src/js/DomCryptoBindings.cpp`.

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` on `SubtleCrypto`.
