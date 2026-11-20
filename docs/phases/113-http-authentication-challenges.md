# Phase 113 — HTTP authentication challenges

Task row: **HTTP authentication challenges are not read at all.** Added to
`AGENTS.md` → Open tasks by this phase, because the row did not exist and
`ROADMAP.md` Tier 1 row 7 named it as the one thing left of HTTP/1.1. Finding
117.

## 1. What was wrong

A `401` was an ordinary failed load. `WWW-Authenticate` was never parsed, no
credential was held for a realm, and no request was ever retried with
`Authorization`. A page behind Basic or Digest authentication was a blank body
with no way for the embedder to learn why.

Two greps said it plainly, and both returned zero:

```bash
grep -c "WWW-Authenticate" src/net/ResourceLoader.cpp
grep -c "Authorization"    src/net/ResourceLoader.cpp
```

`ROADMAP.md` had carried the gap in Tier 1 row 7 since the HTTP work of Phase
36: *"Still open: HTTP authentication challenges (`WWW-Authenticate`/`401`)."*

## 2. What changed

**The two schemes.** RFC 7617 `Basic` and RFC 7616 `Digest`, the latter with
`MD5` and `SHA-256`, the `-sess` variants, and `qop=auth`. `auth-int` is
refused rather than approximated: it hashes the entity body, and a wrong body
hash is worse than a refusal.

**The hashes are written out.** Neither MD5 (RFC 1321) nor SHA-256 (FIPS 180-4)
existed in the tree. Both are implemented directly, which is what makes §3's
oracle possible: the RFCs publish the answers, so the test can hold this code
to a value it did not compute.

**Nothing is sent pre-emptively**, per RFC 7617 §2.2. The first request for an
origin carries no `Authorization`; a `401` is read for its challenges; the same
hop is retried **once** with credentials matching both the origin *and* the
realm the server named. That pairing is the security-relevant part: a password
given for `realm="intranet"` is never offered to `realm="elsewhere"`, and a
credential for one origin is never offered to another.

**Scheme preference** is strongest-first: Digest with SHA-256, then Digest with
MD5, then Basic — which sends the password in the clear and is therefore the
last resort, as RFC 7616 §1 asks.

**What the embedder gets.** `ResourceResponse` now carries `auth_schemes` and
`auth_realm` on a `401`, and `ResourceLoader` exposes a credential store
(`set_auth_credentials`, `has_credentials`, `clear_auth_credentials`,
`auth_credential_count`). There is no prompt: a terminal browser with nowhere
to type a password reports the challenge and lets its embedder fill the store.

## 3. How it was verified

`make test` — **268 / 268 suites**, up from 266; the new suite is
`http_authentication_basic_and_digest_challenges`.

**The digests against published vectors** — RFC 1321 §A.5, FIPS 180-4 §B.1 and
§B.2, RFC 4648 §10:

```
md5("")        = d41d8cd98f00b204e9800998ecf8427e
md5("abc")     = 900150983cd24fb0d6963f7d28e17f72
sha256("abc")  = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
base64("Aladdin:open sesame") = QWxhZGRpbjpvcGVuIHNlc2FtZQ==
```

**The whole Digest exchange against RFC 7616's own example** (§3.9.1 and
§3.9.2), reproduced with the RFC's `cnonce` and `nc` so the expected hex is the
RFC's:

```
MD5     → 8ca523f5e9506fed4657c9700eebdbec
SHA-256 → 753927fa0e85d155564e2e272a28d1802ca10daf4496794697cf8db5856cb6c1
```

**The wire**, which no unit test can fake: a loopback HTTP/1.1 server answers
`401` and then `200`, and both request header blocks are kept and asserted on.
The first has no `Authorization` at all; the second has exactly

```
Authorization: Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==
```

A second loopback case answers `401` with **no** credentials held, and checks
that there is no retry, that the load fails, and that the challenge survives on
the response as `auth_schemes == ["Digest"]` and `auth_realm == "locked"`.

Nothing else moved:

| Command | Before | After |
| :--- | :--- | :--- |
| `make test` | 266 / 266 | **268 / 268** |
| `make js-conformance` | 1141 / 1158 | 1141 / 1158 |
| `make dom-conformance` | 678 / 678 | 678 / 678 |
| `make conformance` | 1973 / 1973 | 1973 / 1973 |
| `make wpt` | 9567 / 9824 | 9567 / 9824 |
| `make check-docs` | 0 failures | 0 failures |

## 4. What is still open

- **`Digest` `auth-int`** is refused, not implemented: it hashes the request
  body, so it needs the body available where the response is built.
- **`SHA-512-256`** (RFC 7616's third algorithm) is refused by name.
- **`userhash=true`** and a `charset` other than `UTF-8` are refused: both
  change what the octets mean, and neither is guessed at.
- **No prompt.** Credentials come from `set_auth_credentials`; there is no UI,
  which matches a terminal shell with no dialog.
- **`Authentication-Info` / `nextnonce`** (RFC 9110 §15.5.2) is not read, so a
  server that rotates its nonce costs one extra challenge round trip.
- **Proxy authentication** (`407`, `Proxy-Authenticate`) is not touched.
- **No revocation of a refused credential**: a server that answers 401 to
  correct-looking credentials gets exactly one retry and the load fails; the
  stored credential is not deleted, so the next load will try it again.
