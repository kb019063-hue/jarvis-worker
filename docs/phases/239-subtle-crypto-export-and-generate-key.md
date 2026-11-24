# Phase 239: `SubtleCrypto.exportKey()` and `SubtleCrypto.generateKey()` (HMAC)

## What was built

`SubtleCrypto.exportKey('raw', key)` and `SubtleCrypto.generateKey({ name: 'HMAC', hash, length? }, extractable, keyUsages)` are implemented in `src/js/DomCryptoBindings.cpp`:

- `exportKey('raw', key)`: returns a `Promise<ArrayBuffer>` with the key's raw bytes, or rejects with `InvalidAccessError` if `key.extractable` is `false`.
- `generateKey({ name: 'HMAC', hash, length? }, extractable, keyUsages)`: generates a random `CryptoKey` of `length` bits (or the hash block size: 64 bytes for SHA-1/SHA-256, 128 bytes for SHA-384/SHA-512) using `/dev/urandom` (with `std::random_device` fallback).

## Verification

- `make test`: all 376 engine test suites pass, including `subtle_crypto_export_and_generate_key`.
