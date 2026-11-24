# Phase 263 — a WSS trust anchor need not be a root

## What was wrong

Phase 258 allowed HTTPS chains to end at an explicitly trusted intermediate
or leaf, but the separate WSS TLS client did not. A valid WSS peer whose issuer
was the configured trust anchor failed before the HTTP Upgrade. It could also
report an issuer error instead of an expected hostname or expiration error.

## What changed

WSS now sets `X509_V_FLAG_PARTIAL_CHAIN` on each TLS session, for both direct
connections and connections through HTTP CONNECT. The flag lets a chain end
at a certificate already in the configured or default trust store; it does not
trust a certificate just because the peer supplied it. Peer verification,
identity and validity checks remain enabled. Failure to load or set the
required verification parameter fails closed.

## Evidence

```sh
make -j2 all
python3 tools/security/verify_websocket.py
python3 tools/security/verify_wss_partial_chain.py --node  # Node >=22.9
```

The WSS harness reports **61 checks, 0 failures**, including the original 50
and 11 partial-chain cases, and 60/60 stress runs. The initial nine-case
partial-chain comparison reported **8 failures before, 0 after**; two IP
identity cases were subsequently added, giving **11 cases, 0 failures**. A
valid connection must complete Upgrade, exchange a message and close cleanly.
Rejections require the specific certificate error, an unclean close and zero
upgrades. The new cases cover a trusted intermediate with or without the
server sending it, direct leaf trust, IP success and mismatch, DNS mismatch,
expired leaf and anchor, an untrusted issuer, and valid and invalid identity
through a recorded CONNECT tunnel.

Python `ssl` explicitly enables partial-chain verification; `--node` adds a
separate Node TLS handshake comparison with `allowPartialTrustChain: true`.
Both references use OpenSSL like Litebird: they are separate clients, **not**
an independent X.509 library or a browser oracle. Only Litebird's client is
checked for the WebSocket Upgrade and tunnel path. The cases run from the
existing verifier in normal and sanitizer CI.

## Still open

This does not implement revocation (OCSP/CRL), pinning, or TLS 1.3 resumption,
or verify all browser trust-store policies. The independent TLS-stack oracle
remains open. Proxy authentication and ordinary `ws://` proxying are separate.
