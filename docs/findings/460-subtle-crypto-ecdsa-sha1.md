# Finding 460: `SubtleCrypto` `ECDSA` rejected `SHA-1`

## What was found

`SubtleCrypto.sign()` and `verify()` for `ECDSA` (Phases 398 & 435) supported `SHA-256`, `SHA-384`, and `SHA-512`, but rejected `SHA-1` with `NotSupportedError`.

## Why it matters

Scripts that sign or verify `ECDSA` (`P-256`) with `SHA-1` could not do so.

## What the fix does

Adds `"sha-1"` handling to `ECDSA` `sign()` and `verify()` in `src/js/DomCryptoBindings.cpp`.

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` on `SubtleCrypto`.
