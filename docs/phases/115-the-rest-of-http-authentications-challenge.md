# Phase 115 — The rest of HTTP authentication's challenge, and a credential a server refuses

## What was wrong

Phase 113 landed RFC 7617 `Basic` and RFC 7616 `Digest` with MD5, SHA-256 and
`-sess`, and left six things open, recorded in one row of `AGENTS.md`:
`auth-int`, `SHA-512-256`, `userhash`, non-UTF-8 `charset`,
`Authentication-Info`/`nextnonce`, `407`/`Proxy-Authenticate`, and "a refused
credential is not deleted between loads".

Two of those turned out to be **bugs in what Phase 113 had already shipped**,
not missing features:

1. **`-sess` hashed A1 with MD5 whatever the algorithm was.** Both code paths
   had `if (sess) a1 = auth_hex(auth_md5(a1)) + …`. RFC 7616 §3.4.2 says A1 for
   `<algorithm>-sess` is `H(username:realm:passwd):nonce:cnonce` with *that*
   algorithm, so `SHA-256-sess` computed H(A1) from an MD5 fold. Measured: the
   old code answered `1dfbe580…` for one case where the standard's answer is
   `1eba72f3…`. It is now `auth_split_algorithm()` and one builder.
2. **`SHA-512-256` was refused outright** by an explicit test, and the loader's
   ranking (`algorithm.rfind("SHA-256", 0)`) matched it by prefix, so a server
   offering `SHA-512-256` was ranked as if it had offered `SHA-256`.

## What changed

`src/net/ResourceLoader.cpp` and `include/litebird/net/ResourceLoader.hpp`.

**SHA-512/256, FIPS 180-4 §6.7.** Not "SHA-512 cut to 32 bytes": §5.3.6.2
gives an IV of its own for every `t`, and §6.7 gives the truncation. The
constants are the standard's 80 words and the initial state is the published
`SHA-512/256` IV. A test asserts the digest of `""` and of `"abc"` *and* asserts
that `"abc"` is not SHA-512's leading 256 bits, because that is the mistake.

**`auth-int`.** §3.4.3: `A2 = Method ":" request-uri ":" H(entity-body)`. The
body is now threaded from the loader (`cur_body`) through
`auth_authorization_for` into the digest, so the header that goes on the wire
digests the bytes that go with it. `auth-int` is preferred over `auth` when the
server offers both, because it protects the message and not only the identity.

**`userhash`.** §3.4.4: the `username` field becomes `H(username ":" realm)` —
computed with **the algorithm in use**, which is what erratum 4897 corrected —
and `userhash=true` is sent with it. Previously any `userhash=true` challenge
was refused outright.

**`Authentication-Info`/`nextnonce`.** §3.5: after a 2xx, a named `nextnonce`
replaces the nonce for the next request in that protection space, kept against
(origin, realm). `nc` is now counted per nonce (§3.4.4) rather than always
`00000001`, so a new nonce restarts at 1 and a reused one increments.

**`407`/`Proxy-Authenticate`.** A 407 is the proxy's challenge and is answered
with `Proxy-Authorization`, which this client does not send. What it does now
is report it — `proxy_auth_schemes` and `proxy_auth_realm` on
`ResourceResponse` — so an embedder can tell a proxy wanting credentials from a
server wanting them. A 407 never resolves through the credential store, and
holding credentials for the very realm a proxy names does not make it send them
(a test asserts exactly that).

**A refused credential is deleted.** When the retry after a 401 is *itself* a
401, the pair is erased from the store for that (origin, realm). Before, it
stayed for the life of the process and every later load replayed it.

`charset` was already handled (anything but `UTF-8` is refused) and is now
documented where it is enforced; §4 allows only `UTF-8`.

## How it was verified

`./build/test_suite` — 271 suites, all pass. The HTTP authentication suite
grew these:

**Against the RFCs' own numbers, not this code's.**

```
$ ./build/test_suite 2>&1 | grep http_authentication
Running test: http_authentication_basic_and_digest_challenges... PASSED
```

- RFC 7616 §3.9.1 reproduced field for field: MD5 → `8ca523f5…`, SHA-256 →
  `753927fa…`, both the RFC's values.
- RFC 7616 §3.9.2 **with erratum 4897 applied**: username →
  `793263ca…`, response → `3798d413…`.
- `-sess`: MD5-sess → `788ad150…`, SHA-256-sess → `1eba72f3…`, and an explicit
  assertion that SHA-256-sess is *not* `1dfbe580…`, the value the MD5-fold bug
  produced.
- SHA-512/256 of `""` → `c672b8d1…`, of `"abc"` → `53048e26…`, and not
  `ddaf35a1…` (SHA-512's first 256 bits).

**The wire, over loopback servers this repository starts**, so every byte of
the exchange is this engine's: a refused credential is dropped (2 requests, the
second carrying `Authorization`, the store empty afterwards); a 407 is reported
and not answered; a `nextnonce` is used on the next load (4 requests, the last
carrying `nonce="second"` and `nc=00000001`).

**Erratum 4897 is the honest part of this phase.** The RFC as published prints
`username="48886947…"` and `response="ae66e67d…"` for §3.9.2. Computing that
example independently — Python's `hashlib.new("sha512_256")`, which is FIPS
180-4 §6.7 — gives `793263ca…` and `3798d413…`, which are exactly the values
erratum 4897 (Reported, 2016) proposes. The published pair is SHA-512/256
computed the way FIPS 180-3 §7 described, which is a different function.
Asserting the RFC's printed values would have asserted a bug; the phase asserts
the corrected ones and says so in the test.

## Scores, unchanged from `main`

js 1141/1158, DOM 678/678, URL 1188/1188, XML 1718/1718, WPT 9567/9824 (257
listed, none new), 271 suites.

## What is still open

- Non-UTF-8 `charset`: §4 allows only `UTF-8`, so "support" here means refusing
  everything else, which is what a conforming client does. The credential store
  holds `std::string`, i.e. UTF-8 octets, and §4 additionally asks for NFC
  normalisation, which is not applied.
- There is still no prompt: credentials come from
  `ResourceLoader::set_auth_credentials`.
- No proxy support: `Proxy-Authorization` is never sent, so a 407 is reported
  and the load fails.
- `stale=true` is not acted on specially: a second 401 now drops the
  credential, where a `stale` one should keep it and retry with the new nonce.
