# Phase 378: `SubtleCrypto` ECDH (`P-256`)

## What was built

Self-contained NIST P-256 (`secp256r1`) ECDH is implemented in `src/js/DomCryptoBindings.cpp`:

- `namespace p256`: 256-bit prime field arithmetic (`add`, `sub`, `mul`, `sqr`, `inv`) and Jacobian point operations (`point_double`, `point_add`, `scalar_mul`).
- `generateKey({ name: 'ECDH', namedCurve: 'P-256' }, extractable, usages)` returning `{ publicKey, privateKey }`.
- `importKey('raw', ...)` and `exportKey('raw', publicKey)` for 65-byte uncompressed (`0x04 || X || Y`) P-256 public keys.
- `deriveBits({ name: 'ECDH', public: pubKey }, privKey, length)` deriving up to 256 bits of the shared secret X coordinate.

## Verification

- `make test`: all 442 engine test suites pass, including `subtle_crypto_ecdh_p256_derive_bits` (verifying Alice and Bob derive identical 256-bit shared secrets).
