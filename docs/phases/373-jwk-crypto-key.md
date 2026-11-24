# Phase 373: `SubtleCrypto` JWK (`importKey('jwk', ...)` and `exportKey('jwk', ...)`)

## What was built

`SubtleCrypto.importKey('jwk', ...)` and `exportKey('jwk', ...)` for symmetric keys (`kty: "oct"`) are implemented in `src/js/DomCryptoBindings.cpp`:

- Added `base64url_encode()` and `base64url_decode()`.
- `importKey('jwk', jwk, ...)` validates `kty === 'oct'`, decodes the base64url `k` field, and imports the key.
- `exportKey('jwk', key)` returns `{ kty: 'oct', k, ext, alg }` with standard JWK algorithm names (`HS256`/`HS384`/`HS512`, `A128GCM`/`A192GCM`/`A256GCM`, `A128CBC`/`A192CBC`/`A256CBC`, `A128CTR`/`A192CTR`/`A256CTR`).

## Verification

- `make test`: all 433 engine test suites pass, including `subtle_crypto_jwk_import_and_export`.
