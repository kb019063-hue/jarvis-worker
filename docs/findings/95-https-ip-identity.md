# Finding 95 — HTTPS IP URLs checked trust but not certificate identity

## What was wrong

ResourceLoader skipped `SSL_set1_host` for IP literals without installing an
IP verification parameter. A trusted certificate for a different IP or a
DNS-only identity could therefore authenticate an HTTPS IP URL.

## What changed

Set the connection's `X509_VERIFY_PARAM` IP identity before TLS negotiation,
and fail closed if either DNS or IP identity configuration fails. Existing
chain verification remains enabled. No insecure fallback was added.

## Verification

`make security-https` checks eight local TLS cases against Python ssl. Before
the fix three incorrect IP identities were accepted; after it all eight
agree, normally and under ASan/UBSan. Rejected peers receive no HTTP request.
CI requires the suite. See [Phase 89](../phases/89-https-certificate-identity.md)
for reproduction, corpus results and the oracle's shared-library limitation.

## Still open

IPv6 fixtures, expiry/intermediate/revocation coverage, pinning and TLS
resumption remain open; this does not implement secure WebSockets.
