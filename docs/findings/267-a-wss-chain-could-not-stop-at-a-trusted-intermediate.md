# 267. A WSS chain could not stop at a trusted intermediate

## What was wrong

Unlike HTTPS after Phase 258, WSS rejected non-self-signed certificates in its
trust store as anchors. That also masked later identity and expiration failures.

## What changed

Phase 263 enables partial-chain verification for WSS, retaining verification of
trust, identity and dates. Direct and HTTP CONNECT paths share this TLS setup.

## Evidence

`python3 tools/security/verify_websocket.py` prints **61 checks, 0 failures**.
The original nine new cases failed **8/9 before, 0/9 after**;
`python3 tools/security/verify_wss_partial_chain.py --node` now prints **11
cases, 0 failures**. Python and Node are separate clients but both use OpenSSL.
[Phase 263](../phases/263-a-wss-trust-anchor-need-not-be-a-root.md) describes
the fixtures and limitations.

## Still open

Revocation, pinning, TLS 1.3 resumption and an independent TLS-stack comparison
remain open, as do proxy authentication and ordinary `ws://` proxying.
