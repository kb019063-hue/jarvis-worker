# Finding 378: `SubtleCrypto` had no ECDH key agreement

## What was found

`SubtleCrypto` supported symmetric ciphers and KDFs (`PBKDF2`, `HKDF`), but elliptic-curve Diffie-Hellman (`ECDH`) was not implemented.

## Why it matters

Scripts that perform `ECDH` (`P-256`) key generation, raw public-key import/export, and `deriveBits()` could not do so.

## What the fix does

Adds self-contained NIST P-256 field/curve arithmetic (`namespace p256`) in `src/js/DomCryptoBindings.cpp` and wires `'ECDH'` (`namedCurve: 'P-256'`) into `generateKey()`, `importKey('raw', ...)`, `exportKey('raw', ...)`, and `deriveBits()`.

## What is still open

- `ECDSA` signing/verification, `RSA-PSS`, and PKCS8/SPKI key formats on `SubtleCrypto`.
