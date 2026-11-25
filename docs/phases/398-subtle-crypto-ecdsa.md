# Phase 398: `SubtleCrypto` ECDSA (`P-256`, `SHA-256`)

## What was built

Self-contained NIST P-256 (`secp256r1`) ECDSA signing and verification are implemented in `src/js/DomCryptoBindings.cpp`:

- `p256::add_n()`, `mul_n()`, `inv_n()` for scalar arithmetic modulo the P-256 curve order $n$.
- `p256::ecdsa_sign()` and `p256::ecdsa_verify()` using 64-byte IEEE P1363 `(r || s)` signatures.
- Wired `'ECDSA'` (`namedCurve: 'P-256'`) into `generateKey()`, `importKey('raw', ...)`, `sign()`, and `verify()`.

## Verification

- `make test`: all 452 engine test suites pass, including `subtle_crypto_ecdsa_p256_sign_and_verify`.
