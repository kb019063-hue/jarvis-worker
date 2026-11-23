# Phase 219: `SubtleCrypto.digest()` (`SHA-1`, `SHA-384`, `SHA-512`)

## What was built

Extended `crypto.subtle.digest(algorithm, data)` in `src/js/DomCryptoBindings.cpp` with self-contained FIPS 180-4 implementations of `SHA-1` (20 bytes), `SHA-384` (48 bytes), and `SHA-512` (64 bytes), alongside `SHA-256`.

## Verification

- `make test`: all 363 engine test suites pass, including `subtle_crypto_digest_sha1_sha384_sha512` (verified against NIST FIPS 180-4 `"abc"` vectors).
