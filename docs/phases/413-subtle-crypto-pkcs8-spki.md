# Phase 413: `SubtleCrypto` `PKCS8` and `SPKI` key formats for EC `P-256`

## What was built

`SubtleCrypto.importKey()` and `exportKey()` in `src/js/DomCryptoBindings.cpp` now support `'spki'` and `'pkcs8'` DER formats for EC `P-256` (`ECDSA` / `ECDH`) keys:

- `'spki'` (91-byte SubjectPublicKeyInfo DER): exports/imports 65-byte uncompressed (`0x04 || X || Y`) `P-256` public keys.
- `'pkcs8'` (67-byte PrivateKeyInfo DER): exports/imports 32-byte `P-256` private keys.

## Verification

- `make test`: all 468 engine test suites pass, including `subtle_crypto_ec_spki_and_pkcs8`.
