# Finding 189: `window.crypto` (`getRandomValues`, `randomUUID`) was not implemented

## What was found

`window.crypto` (`Crypto.getRandomValues()` and `Crypto.randomUUID()`) was missing from the DOM/Window bindings.

## Why it matters

Scripts that generate random bytes or UUIDs via the Web Crypto API could not use `crypto.getRandomValues()` or `crypto.randomUUID()`.

## What the fix does

Adds `src/js/DomCryptoBindings.cpp` with `setup_crypto`, `register_crypto_interfaces()`, and `window.crypto` installation in `src/js/DomBindings.cpp`.

## What is still open

- `crypto.subtle` (`SubtleCrypto`).
- `BigInt64Array` / `BigUint64Array` support in `getRandomValues()`.
