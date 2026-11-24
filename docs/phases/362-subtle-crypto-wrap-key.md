# Phase 362: `SubtleCrypto.wrapKey()` and `unwrapKey()`

## What was built

`SubtleCrypto.wrapKey()` and `SubtleCrypto.unwrapKey()` for `'raw'` key format are implemented in `src/js/DomCryptoBindings.cpp`:

- `wrapKey('raw', key, wrappingKey, wrapAlgo)` exports `key`'s raw bytes (rejecting non-extractable keys with `InvalidAccessError`) and encrypts them with `AES-GCM`, `AES-CBC`, or `AES-CTR`.
- `unwrapKey('raw', wrappedKey, unwrappingKey, unwrapAlgo, unwrappedKeyAlgo, extractable, keyUsages)` decrypts `wrappedKey` with `AES-GCM`, `AES-CBC`, or `AES-CTR` and imports the resulting raw bytes as a `CryptoKey`.

## Verification

- `make test`: all 427 engine test suites pass, including `subtle_crypto_wrap_and_unwrap_key`.
