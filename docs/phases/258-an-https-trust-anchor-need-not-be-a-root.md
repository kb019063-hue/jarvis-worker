# Phase 258 — an HTTPS trust anchor need not be a root

## What was wrong

The HTTPS loader required a chain to end at a self-signed certificate, even
when the local CA bundle explicitly trusted an intermediate or the leaf. Six
new loopback cases failed against the existing binary: three refused trusted
certificates, and three refused before reaching the expected identity or
expiration error.

## What changed

The HTTPS loader sets OpenSSL's `X509_V_FLAG_PARTIAL_CHAIN` on each TLS
session, allowing the chain to end at an explicitly trusted certificate.
Peer verification, hostname/IP identity, dates and the rest of chain
verification remain enabled. It fails closed if the required OpenSSL function
or parameter cannot be configured. The same behavior applies to custom and
default trust stores; a certificate merely supplied by the server does not
become trusted.

## Evidence

```sh
make -j2 all
python3 tools/security/verify_https.py
python3 tools/security/verify_https_node.py  # Node.js >=22.9
```

The HTTPS harness checks **27 cases, 0 failures** after; the old binary failed
**6 of 27**. The six new cases include intermediate trust with and without a
server-sent intermediate, direct leaf trust, a wrong hostname, an expired leaf,
and an expired intermediate anchor. Refusals require the expected verification
code and no HTTP request. The other 21 cases continue to cover untrusted and
missing chains, invalid CA constraints, DNS/IP identity, dates, IPv6 and SNI.
Python's `ssl` reference explicitly enables partial-chain verification.

The separate Node.js comparison reports **6 comparisons, 0 failures** with
`allowPartialTrustChain: true` and the same ephemeral fixture family. Python,
Node and Litebird are independent clients but all use OpenSSL: this is *not*
an independent certificate-verification implementation or a browser oracle.

## Still open

The `wss://` client has its own TLS setup and has **not** been changed; another
pull request owns that source. OCSP/CRL revocation, pinning, TLS 1.3 resumption,
and cross-checking with a TLS stack other than OpenSSL remain open. This does
not claim every browser trust-store policy is reproduced.
