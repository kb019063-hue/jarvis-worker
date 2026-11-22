# Phase 185: `Crypto.getRandomValues()` and `Crypto.randomUUID()`

## What was built

`Crypto` (`window.crypto`) is implemented in `src/js/DomCryptoBindings.cpp` and wired into `src/js/DomBindings.cpp`:

- `crypto.getRandomValues(typedArray)`: fills integer `TypedArray`s (`Int8Array`, `Uint8Array`, `Uint8ClampedArray`, `Int16Array`, `Uint16Array`, `Int32Array`, `Uint32Array`) up to 65,536 bytes using `/dev/urandom` (with `std::random_device` fallback) and returns the same array.
- `crypto.randomUUID()`: generates a version-4 UUID string (`xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx`).

## Verification

- `make test`: all 336 engine test suites pass, including `crypto_get_random_values_and_random_uuid`.
