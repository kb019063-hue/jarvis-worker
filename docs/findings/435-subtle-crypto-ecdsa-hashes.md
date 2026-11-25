# Finding 435: `SubtleCrypto` `ECDSA` ignored `algorithm.hash` and hardcoded `SHA-256`

## What was found

`SubtleCrypto.sign()` and `verify()` for `ECDSA` (Phase 398) always hashed with `SHA-256` regardless of `algorithm.hash`.

## Why it matters

Scripts that sign or verify `ECDSA` (`P-256`) with `SHA-384` or `SHA-512` produced mismatched signatures.

## What the fix does

Reads `algorithm.hash` in `ECDSA` `sign()` and `verify()` in `src/js/DomCryptoBindings.cpp` and dispatches to `SHA-256`, `SHA-384`, or `SHA-512`.

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` on `SubtleCrypto`.
