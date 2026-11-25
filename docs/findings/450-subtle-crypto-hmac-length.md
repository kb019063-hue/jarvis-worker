# Finding 450: `CryptoKey.algorithm` omitted `length` on `HMAC` keys

## What was found

`CryptoKey.algorithm` (Phase 437) returned `{ name: 'HMAC', hash: { name } }` without `length`.

## Why it matters

Scripts that inspect `hmacKey.algorithm.length` to determine the key size in bits received `undefined`.

## What the fix does

Adds `alg.set_property("length", num(static_cast<double>(st.key_bytes.size() * 8u)));` to `CryptoKey.algorithm` for `HMAC` keys in `src/js/DomCryptoBindings.cpp`.

## What is still open

- `RSA-PSS` / `RSASSA-PKCS1-v1_5` on `SubtleCrypto`.
