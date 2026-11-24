# 262. A trusted intermediate was not a trust anchor

## What was wrong

HTTPS rejected a certificate chain ending at a non-self-signed certificate in
its trust store. The failure also masked hostname and expiration diagnostics.

## What changed

Phase 258 enables partial-chain verification on HTTPS sessions, still requiring
an explicitly trusted certificate and retaining identity and date checks.
The separate WSS implementation is unchanged.

## Evidence

`python3 tools/security/verify_https.py` prints **27 cases, 0 failures** after;
**6/27 failed before**. `python3 tools/security/verify_https_node.py` compares
six cases with Node.js 22.9 or newer and prints **6 comparisons, 0 failures**.
Both client references use OpenSSL, not an independent X.509 implementation.
See [Phase 258](../phases/258-an-https-trust-anchor-need-not-be-a-root.md).

## Still open

WSS partial-chain trust, revocation, pinning, TLS 1.3 resumption and comparison
with a different TLS implementation remain open.
