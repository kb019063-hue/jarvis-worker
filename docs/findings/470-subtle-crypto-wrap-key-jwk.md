# Finding 470: `SubtleCrypto.wrapKey()` and `unwrapKey()` rejected `'jwk'` format

## What was found

`SubtleCrypto.wrapKey()` and `unwrapKey()` (Phase 362) only supported `'raw'` format and rejected `'jwk'` with `NotSupportedError`.

## Why it matters

Scripts that wrap or unwrap EC `P-256` private keys or symmetric keys in JWK format could not do so.

## What the fix does

Adds `'jwk'` serialization/parsing to `wrapKey()` and `unwrapKey()` in `src/js/DomCryptoBindings.cpp`, supporting `kty: 'oct'` and `kty: 'EC'` (`P-256`).

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` on `SubtleCrypto`.
