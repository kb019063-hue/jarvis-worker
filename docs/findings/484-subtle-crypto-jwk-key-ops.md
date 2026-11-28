# Finding 484: `SubtleCrypto` JWK export omitted `key_ops` and import ignored `ext: false`

## What was found

`SubtleCrypto.exportKey('jwk', ...)` did not include `key_ops`, and `importKey('jwk', ...)` allowed `extractable: true` even when `jwk.ext === false`.

## Why it matters

Scripts that inspect `jwk.key_ops` or rely on `jwk.ext === false` enforcement during JWK import saw missing `key_ops` or missing `DataError` rejections.

## What the fix does

Populates `jwk.key_ops` in `exportKey('jwk', ...)` and rejects `jwk.ext === false` with `extractable === true` in `importKey('jwk', ...)` in `src/js/DomCryptoBindings.cpp`.

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` on `SubtleCrypto`.
