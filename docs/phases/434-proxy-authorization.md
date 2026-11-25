# Phase 434 — a proxy's `407` is now answered

Task row: **HTTP authentication is whole except for the parts that need a
prompt or a proxy** — the proxy half (`AGENTS.md` row 4). Finding 434.

## 1. What was wrong

The loader read a proxy's challenge and then gave up. `auth_note_proxy_challenge`
parsed `Proxy-Authenticate` into `proxy_auth_schemes` and `proxy_auth_realm` for
the embedder to inspect, and the `407` was returned as the load's result. The
comment above that function said so in as many words:

```cpp
/// A `407` is the proxy's challenge, not the document's (RFC 9110 §15.5.8),
/// and it is answered with `Proxy-Authorization` rather than `Authorization`.
/// This client does not send to proxies; what it does is report what the proxy
/// asked for, so an embedder can tell a proxy that wants credentials from a
/// server that does.
```

So a proxy that wanted credentials could never be satisfied: there was no API
that would even hold a proxy credential, and no request ever carried
`Proxy-Authorization`. The row recorded it as "`Proxy-Authorization` is never
sent, so a 407 is reported and the load fails".

The half of that which is easy to get wrong is the *other* direction, and the
existing suite already guarded it: `tests/test_suite.cpp` sets a credential for
the very realm the proxy names and asserts it is **not** sent, because they are
the document's and this is the proxy asking. RFC 9110 §11.7 keeps the two
protection spaces apart — a proxy must not be able to collect a password meant
for an origin, nor the reverse. A fix that simply reused
`set_auth_credentials(origin, realm)` would have passed a naive test and failed
that one, turning a missing feature into a credential leak.

## 2. What changed

- **A second store.** `g_proxy_auth_store`, keyed `(authority, realm)`, beside
  the document's `g_auth_store`, with `set_proxy_credentials`,
  `has_proxy_credentials`, `clear_proxy_credentials` and
  `proxy_credential_count`. The two keys never meet: a document credential is
  looked up by origin, a proxy credential by the authority the request went to.
- **A second authorizer.** `auth_proxy_authorization_for` ranks challenges the
  same way `auth_authorization_for` does (Digest by `SHA-512-256`, `SHA-256`,
  MD5, then Basic) and reads *only* the proxy store. Its nonce bookkeeping is
  keyed by the proxy authority rather than an origin, so the two spaces cannot
  share a nonce either.
- **A bounded retry in `load_http`.** Nothing is sent pre-emptively. On a `407`
  the challenges are parsed, matched against the proxy store, and the hop is
  re-sent once with `Proxy-Authorization`. A non-stale `407` after that attempt
  forgets the pair that was refused — the same rule a `401` follows, so a wrong
  proxy password is not replayed for the life of the process. A stale Digest
  nonce is not a refusal: the credential stays and the `407` is reported with
  its replacement nonce. The attempt is counted per redirect hop, like the
  origin's, so a chain of hops cannot loop.
- `clear_auth_credentials()` still clears only the document space. That is
  deliberate and is now asserted: a caller that wants the proxy space gone
  calls `clear_proxy_credentials()`.

The header's `set_auth_credentials` documentation did not change; the proxy
functions document their own space and say why it is separate.

## 3. Evidence

`make test` **479/479**. `make js-conformance` **1153/1158**, `make
dom-conformance` **678/678** and `make xml-conformance` **1718/1718**, all
unchanged from `main`.

`tests/suites/434-proxy-authorization.cpp` runs a loopback stand-in for a proxy
that answers `407` with a Basic challenge until the right
`Proxy-Authorization` arrives, and holds four things at once:

1. **A proxy credential answers the challenge.** `200`, body intact, the
   credential sent, and — matched on the line start, so the proxy header's own
   name cannot satisfy it — no `Authorization` on the wire.
2. **The document's credential is not the proxy's.** The same origin, the same
   realm name, a credential the server would take: the `407` is still the
   answer, nothing is sent, the challenge is still reported, and both stores are
   as they were.
3. **A refused proxy credential is forgotten.** A wrong password is tried once,
   the `407` comes back, and the pair is gone.
4. **The two stores clear apart.**

On the unfixed build the behaviour is measured the only way it can be — the new
functions do not exist there, so the suite cannot compile against it. Using the
one API the old build has, with the proxy's own realm, the load answers:

```
status=407  proxy_auth_schemes=1  realm=gateway
VERDICT: the 407 is reported and the load FAILS - a proxy wanting credentials
can never be satisfied
```

and with the fix the same credential path answers `200` with
`Proxy-Authorization` sent. That measurement is what the phase is about; the
suite pins it so it cannot come back.

## 4. What is still open

Still nothing that needs a prompt: no `407` triggers any interactive
credential request, and `set_proxy_credentials` is the only way in. There is
still no general proxy support — this engine does not route requests through a
proxy for `http://` (only `wss://` has an HTTP CONNECT tunnel, Phase 242) — so
a `407` in the wild can only arrive from something already standing in the
request's path. `Proxy-Authenticate`'s `Digest` challenge is answered through
the same code path as Basic but is not separately measured by this suite.
