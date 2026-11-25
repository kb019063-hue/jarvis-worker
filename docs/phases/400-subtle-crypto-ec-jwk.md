# Phase 400: `SubtleCrypto` EC JWK (`importKey('jwk', ...)` and `exportKey('jwk', ...)`)

## What was built

`SubtleCrypto.importKey('jwk', ...)` and `exportKey('jwk', ...)` for EC `P-256` (`ECDSA` / `ECDH`) keys are implemented in `src/js/DomCryptoBindings.cpp`:

- `exportKey('jwk', key)` exports `{ kty: 'EC', crv: 'P-256', x, y, ext }` for public keys and additionally `d` (computing `(x, y) = d * G`) for private keys.
- `importKey('jwk', jwk, ...)` validates `kty === 'EC'`, `crv === 'P-256'`, decodes 32-byte `x`, `y`, and optional `d`, and creates the corresponding `private` or `public` `CryptoKey`.

## Verification

- `make test`: all 461 engine test suites pass, including `subtle_crypto_ec_jwk_import_and_export`.
