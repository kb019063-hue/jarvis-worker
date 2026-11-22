# Phase 162 — a stale Digest nonce is not a refused credential

Task: honor RFC 7616 `stale=true` challenges for the HTTP-authentication row in
`AGENTS.md`. Finding 167.

## What was wrong

`ResourceLoader::load_http` treated every second 401 as proof that the stored
username or password was refused. RFC 7616 gives Digest a different case:
`stale=true` says the nonce is expired, and the server supplies a replacement.
The loader discarded a valid credential instead of retrying the request with
that new nonce.

## What changed

A same-origin, same-realm Digest `stale=true` response now keeps the stored
credential and gets one additional retry with the challenge's replacement
nonce. The stale nonce takes precedence over an older remembered
`Authentication-Info: nextnonce`, and its nonce count starts at one. A repeated
stale response is bounded: the request stops after that retry, the latest nonce
is remembered for the next load, and the credential is retained. A non-stale
401 still removes only the credential actually attempted; a stale challenge
for a different realm cannot keep that credential alive.

The loopback-wire tests cover both the successful replacement-nonce retry and a
server that returns `stale=true` repeatedly. They also check that credentials
are not sent on the first request, `nc` restarts at one for the replacement
nonce, the stored credential survives a repeated stale response, and a
successful retry's `nextnonce` is retained.

## Verification

`make test` — **318 engine suites passed; 114 cross-origin checks passed, 0
failures.** The wire assertions run inside
`http_authentication_basic_and_digest_challenges` against loopback HTTP servers.

## What is still open

HTTP authentication still has no credential prompt or proxy support. Username
and password NFC normalization is not applied, and a Digest `charset` other
than UTF-8 is refused. A load permits one stale retry; if the server returns
another stale challenge, the client stops and leaves the credential available
for a later load.
