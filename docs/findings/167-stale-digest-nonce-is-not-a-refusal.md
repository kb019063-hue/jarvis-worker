# Finding 167 — a stale Digest nonce is not a refused credential

## What was wrong

The HTTP loader deleted credentials after the first authenticated request got a
401. That is correct when the server refuses the password, but not when a Digest
challenge carries `stale=true`: RFC 7616 says the nonce is stale, not the
credential, and the challenge supplies the nonce to use next.

## What changed

The loader now retries once with the same realm's stored credential and the
replacement nonce. The replacement overrides an old remembered `nextnonce` and
starts a fresh nonce count. If the server says stale again, the retry budget
stops the exchange, preserves the credential, and remembers the latest nonce;
a non-stale 401 still removes the credential that was actually tried.

## Verification

`make test` passed **318 engine suites** and **114 cross-origin checks**. The
loopback-wire cases in `http_authentication_basic_and_digest_challenges` verify
the new nonce on the retry, `nc=00000001`, preservation after repeated stale
responses, and the existing credential-removal behavior for a non-stale 401.

## What is still open

The loader still has no credential prompt or proxy authentication, does not
NFC-normalize credentials, and refuses Digest charsets other than UTF-8. Stale
retries are deliberately bounded to one per load.
