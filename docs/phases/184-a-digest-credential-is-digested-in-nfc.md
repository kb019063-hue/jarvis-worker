# Phase 184 — a Digest credential is digested in NFC

Task row: **HTTP authentication is whole except for the parts that need a
prompt or a proxy** (`AGENTS.md`). Of what the row listed as still open — no
prompt, no proxy support, NFC normalization of the username and password, and
a `charset` other than UTF-8 — one was a defect in something already shipped
rather than a feature this engine does not have: the credential was digested
exactly as it was typed. `src/net/ResourceLoader.cpp` was named by no open
pull request. Finding 188.

## 1. What was wrong

RFC 7616 §3.4.1 is one sentence about octets:

> The username and password fields ... **MUST** be encoded in UTF-8, and
> **SHOULD** be in Normalization Form C.

A password with an accent has two spellings in Unicode — precomposed `é`
(U+00E9) and `e` followed by U+0301 — and they are the same password to a
person and different bytes to a digest. This client hashed whichever bytes it
was given, so a credential typed with a combining accent computed a response
its own server could never reproduce:

```js
// The same credential, two spellings, one request:
ResourceLoader::auth_Digest_response("MD5", "é", …)      // 2248d1d270e12f8fbd985df11111d87e
ResourceLoader::auth_Digest_response("MD5", "e\u0301", …) // f2c9481685a393d1e7e27bdcd73f31c7
```

The two answers are both "right" for the bytes handed in, which is why nothing
failed: the test that would have caught it is one that asks the same question
twice with spellings a person cannot tell apart.

## 2. What changed

`src/net/ResourceLoader.cpp`, in the one builder every Digest computation goes
through (`auth_digest_build`, which `auth_Digest_response` and
`auth_Digest_build` both call, so the loader and the test entry point cannot
drift):

- **`auth_nfc()`** returns the NFC of a UTF-8 string: a UTF-8 → UTF-16 decode,
  `unicode::normalize_utf16(..., NormForm::NFC, ...)` — the engine's own UAX #15
  implementation, the one `String.prototype.normalize` uses — and back to
  UTF-8. ASCII returns the input untouched (the normalizer says so, and so does
  the ASCII check before it), so the common case costs a scan.
- **The username and password are normalized once, at the top**, and the two
  places that mix them into the digest use the normalized pair: A1
  (`username:realm:password`), and the `userhash` field
  (`H(username ":" realm)`, §3.4.4). The `username` sent on the wire is the
  normalized one too, since §3.4.1 requires the field to be NFC and a server
  that precomposed its user list must match a client that did not.
- **Nothing else moved.** `-sess`, `auth-int`, `SHA-512-256`, `userhash`,
  `nextnonce` and the `stale` retry all read the same pair they did before.

## 3. How it was verified

The expectation is not this code's: each digest below was computed by hand in
**Node.js v22.22.3** (`crypto.md5` over the A1/A2 strings), and then the engine
was asked for the same credential in both spellings.

```bash
make test      # All 337 engine test suites passed successfully!
```

Six assertions were added to the existing `http_authentication_basic_and_digest_challenges`
suite, beside RFC 7616 §3.9.1's example:

| Ask | Expected | Before |
| :--- | :--- | :--- |
| `é` (precomposed) as the user | `2248d1d270e12f8fbd985df11111d87e` | the same |
| `e` + U+0301 as the user | `2248d1d270e12f8fbd985df11111d87e` | `f2c9481685a393d1e7e27bdcd73f31c7` |
| `é` as the password | `e2035f3d116dbeadfccde7d61c193e81` | the same |
| `e` + U+0301 as the password | `e2035f3d116dbeadfccde7d61c193e81` | `63b92bfc491951678210ea4aa9b19096` |
| RFC 7616 §3.9.1's own example, unchanged | `8ca523f5e9506fed4657c9700eebdbec` | the same |
| `userhash` of both spellings | equal | different |

The RFC's example surviving is the check that the normalization is a no-op for
a credential already in NFC; without it a "normalizer" that changed ASCII would
look like a pass.

## 4. What is still open

- **No prompt.** Credentials still come from
  `ResourceLoader::set_auth_credentials`; a page that needs one the embedder
  did not supply fails the load.
- **No proxy.** `Proxy-Authorization` is never sent, so a `407` is reported as
  `proxy_auth_schemes`/`proxy_auth_realm` and the load fails.
- **A Digest `charset` other than UTF-8 is refused**, not re-encoded: RFC 7616
  §3.4.1 allows only `UTF-8`, and the refusal is what a client that cannot
  accept an unknown charset should do.
- **`Basic` is not normalized.** RFC 7617 has no normalization requirement,
  and its `charset` parameter is not part of the profile this engine
  implemented; the two schemes now differ in a way the standards do.
