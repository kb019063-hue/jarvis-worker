# Finding 188 — the same password had two digests

**Resolved by Phase 184** (`docs/phases/184-a-digest-credential-is-digested-in-nfc.md`).

## 1. What was found

`auth_digest_build` hashed the bytes it was handed, and RFC 7616 §3.4.1 says
the credential must be NFC before that. A username or password containing an
accent therefore had two digests depending on how it was typed:

```
é      -> 2248d1d270e12f8fbd985df11111d87e
e+0301 -> f2c9481685a393d1e7e27bdcd73f31c7
```

Both are correct for their own bytes, which is exactly why no test failed: the
missing test asks the same question twice with spellings a person cannot tell
apart. The row had listed this as an open item for three phases.

## 2. What was done

`auth_nfc()` — UTF-8 to UTF-16, `unicode::normalize_utf16` in NFC (the
engine's UAX #15, the same one `String.prototype.normalize` uses), back to
UTF-8 — applied once at the top of `auth_digest_build`, to the username and the
password, before A1, before `userhash`, and before the field is written out.
ASCII returns the input unchanged, so the common path is a scan.

## 3. Evidence

`make test` **335/335**. Six assertions in
`http_authentication_basic_and_digest_challenges`, with the expected digest
computed by hand in Node.js v22.22.3:

- both spellings of the user name now answer `2248d1d2…` (the combining one
  answered `f2c94816…`);
- both spellings of the password answer `e2035f3d…` (the combining one
  answered `63b92bfc…`);
- RFC 7616 §3.9.1's published example still answers `8ca523f5…`, so the
  normalization is a no-op for a credential already in NFC;
- `userhash=true` hashes the normalized name, so both spellings agree.

## 4. What is still open

No credential prompt, no proxy support (`Proxy-Authorization` is never sent),
and a Digest `charset` other than UTF-8 is refused rather than re-encoded.
`Basic` is left alone: RFC 7617 has no normalization requirement.
