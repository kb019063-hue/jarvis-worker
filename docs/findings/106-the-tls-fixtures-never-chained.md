# Finding 106 — the TLS fixtures proved identity and never a chain

Phase 100. The architecture and the measurements are in
[`docs/phases/100-https-certificate-chains-dates-and-ipv6.md`](../phases/100-https-certificate-chains-dates-and-ipv6.md).

## 1. What was wrong

`make security-https` had eight fixtures, and all eight were one shape: a leaf
certificate, signed directly by the trust anchor handed to the engine through
`LITEBIRD_SSL_CA_FILE`, offered on a loopback server, at a URL whose name either
matched the certificate or did not.

That is identity. It is not the rest of what a client does with a certificate:

| question | fixtures before | fixtures now |
| --- | --- | --- |
| does a name match the certificate? | 8 | 8 (unchanged) |
| is the chain built through an intermediate? | 0 | 3 |
| is a validity window checked? | 0 | 3 |
| is an IPv6 literal an address, not a name? | 0 | 4 |
| is SNI sent — and not sent — where it should be? | 0 | 2 |

Phase 89 changed the engine's TLS setup so that an IP-literal URL configures
`X509_VERIFY_PARAM_set1_ip_asc` rather than `SSL_set1_host`. Its fixtures then
covered IPv4; the IPv6 half of the same code path had never been run, and a
regression that dropped `SSL_CTX_set_verify(SSL_VERIFY_PEER)`, or that passed a
bracketed literal to `set1_ip_asc`, would have been invisible.

## 2. What changed

`tools/security/verify_https.py` now builds a two-level PKI — a self-signed root
and an intermediate CA — with `openssl ca` for everything the root signs, which
is the only OpenSSL verb that takes explicit `-startdate`/`-enddate`:

- chains: trusted, intermediate missing, intermediate `CA:FALSE`;
- dates: expired leaf, not-yet-valid leaf, expired intermediate;
- IPv6: `[::1]` accepted, the same address expanded, an IPv4 SAN at an IPv6 URL
  and an IPv6 SAN at an IPv4 URL both rejected;
- SNI: a virtual host that answers `localhost` with the trusted chain and any
  other name with `DNS:other.invalid` — accepted only if the name is sent — and
  a literal URL against it, which must send no name and must get the default
  certificate. Both cases assert the names the engine sent.

The suite is **20 / 20**, and Python's `ssl` client agrees with the engine on
every one.

## 3. Why no engine change

Chain building, `basicConstraints`, path length, validity windows and name/IP
matching are OpenSSL's. The engine's own part is small and was made right in
Phase 89: the host of a URL becomes either `SSL_set1_host` (a DNS name, also
sent as SNI) or `X509_VERIFY_PARAM_set1_ip_asc` (a literal, never sent as SNI),
and `SSL_get_verify_result` is checked after the handshake. The fixtures found
nothing wrong with it. Their value is that the four properties above are now
measured instead of assumed, and a future change to that code has something to
fail against.

## 4. What is still open

- Revocation (OCSP/CRL), pinning, TLS 1.3 resumption — the remaining half of
  the row.
- The oracle is the same OpenSSL library, so it cannot see a bug inside OpenSSL.
- `wss://` builds its own `SSL_CTX` in `src/net/WebSocket.cpp`; this harness
  says nothing about it.
- A non-self-signed certificate in the trust store is rejected (as Python
  rejects it), where browsers accept it with `X509_V_FLAG_PARTIAL_CHAIN`. Not
  taken: it is a choice about which client to match, not a defect.
