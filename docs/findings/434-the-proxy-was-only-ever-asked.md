# Finding 434 — the proxy was asked and never answered

**Resolved by Phase 434** (`docs/phases/434-proxy-authorization.md`).

## 1. What was found

Two comments and no code. `auth_note_proxy_challenge` read a `407`'s
`Proxy-Authenticate` header into `proxy_auth_schemes` and `proxy_auth_realm`,
and the load returned the `407`. Above it, in this repository's own words:

> This client does not send to proxies; what it does is report what the proxy
> asked for, so an embedder can tell a proxy that wants credentials from a
> server that does.

So the engine could *recognise* a proxy's challenge and had no way to answer
one. There was no store for a proxy credential and no request path that wrote
`Proxy-Authorization`, which means the row's claim — "a `407` is reported and
the load fails" — was exact, and the feature was not half-built but absent.

## 2. What was done

A proxy credential space of its own, and one bounded retry. The new store is
keyed by the authority the request went to and the realm the challenge named;
the authorizer reads only that store; `load_http` re-sends the hop once with
`Proxy-Authorization`, and forgets the pair on a non-stale refusal exactly as a
`401` does.

## 3. The part worth keeping

The obvious implementation — reuse `set_auth_credentials`, since it is already
keyed by realm — is wrong, and the repository had already written the test that
says why. `tests/test_suite.cpp` holds a credential for the realm the proxy
names and asserts that it is **not** sent, because RFC 9110 §11.7 makes the
proxy's protection space and the origin's two different spaces.

Reusing the store would have satisfied a naive "can a 407 be answered" test
while quietly handing an origin's password to a proxy — a credential leak
introduced by a fix, which is the worst kind, and one the suite would have
caught only because somebody had already thought about the direction that does
not look like a bug. The new suite asserts both directions in one test so that
the pair cannot be separated later: one half without the other is either the
missing feature or the leak.

## 4. Evidence

`make test` **479/479**; js-, dom- and xml-conformance unchanged at 1153/1158,
678/678 and 1718/1718. On the unfixed build the same request answers `407`
with the challenge reported and the credential unsent — measured, see the
phase write-up — and the new suite's first case fails against it. The suite
cannot compile against the old build at all, because the four functions it
needs do not exist there; that is why the phase also records the behavioural
measurement.
