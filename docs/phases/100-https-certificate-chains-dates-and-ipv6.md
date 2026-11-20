# Phase 100 — HTTPS: chains, dates and IPv6, and the name a client sends

Task row: **HTTPS certificate test coverage remains partial** (finding 106).

## 1. What was wrong

`make security-https` had eight fixtures and every one of them asked the same
question: *is this certificate about this identity?* Trusted and untrusted DNS
and IPv4 names, an IP SAN used as a DNS name, a DNS SAN used at an IP URL. Each
one was a leaf certificate signed directly by the trust anchor.

Nothing asked the three questions a browser's TLS stack is actually made of:

- **a chain** — a leaf signed by an intermediate, with the intermediate
  handed over by the server, and the four ways that can go wrong (the
  intermediate missing, the intermediate marked `CA:FALSE`, the intermediate
  expired, the leaf not yet valid);
- **a date** — a certificate that expired in 2020, and one that is not valid
  until 2099;
- **an IPv6 literal** — `https://[::1]/`, whose identity is an `iPAddress`
  SAN of sixteen bytes and whose URL is bracketed where the address is not;
- **SNI** — the name a client sends, which decides *which* certificate the
  server answers with, and which must be sent for a DNS host and never for a
  literal address.

A regression in any of them would have passed CI. Phase 89's engine change
(`X509_VERIFY_PARAM_set1_ip_asc` for literals) had one positive IPv6-free test
and no chain test at all.

## 2. What changed

`tools/security/verify_https.py` is rebuilt around a small two-level PKI in the
temporary directory, and the case table grew from eight to **twenty**.

- **A root and an intermediate.** The root is `openssl req -x509`; everything
  below it is signed by `openssl ca`, which is the only one of the two OpenSSL
  verbs that takes explicit dates — `req -x509 -days 0` is "Non-positive number
  for option -days", so a certificate that expired in 2020 cannot be made any
  other way. Each issuer gets its own CA database, with `unique_subject = no`
  because two fixtures are both `CN=localhost`.
- **A chain is what a server hands over.** `chain_file()` concatenates the leaf
  and its issuer, and the fixture server loads that as its certificate chain, so
  "the intermediate is missing" is a server that hands over the leaf alone.
- **An IPv6 fixture binds `::1`.** `server()` picks `AF_INET6` from the address,
  and `url_for()` brackets a literal so the engine is asked for
  `https://[::1]:port/` — the compressed form and the expanded
  `https://[0:0:0:0:0:0:0:1]:port/`, which is the same address and has to
  verify the same way.
- **SNI is measured, not assumed.** The fixture server's `sni_callback` records
  every name a client sends and can swap in a per-name certificate, as a
  virtual host does. Two cases use it: a virtual host that answers `localhost`
  with the trusted chain and anything else with `DNS:other.invalid`, and a
  literal URL against the same host, which must get the default certificate and
  must send no name. The cases assert the engine *sent* `['localhost']` and
  `[]` respectively, not merely that it ended up with the right certificate.
- Every case is still decided twice — Python's verifying `ssl` client first, as
  the independent client, then Litebird's CLI — and a rejection still has to be
  a certificate failure with exit status 1 and no HTTP byte received, so an
  unrelated CLI error cannot pass.

**No engine change was needed.** That is the expected result and it is the
point of the phase: chain building, path length, `basicConstraints`, validity
windows and hostname/IP matching are OpenSSL's, and the only engine-side part —
which verification parameter the URL's host is put into (Phase 89) — was
already right. What was missing was the proof that it stays right.

## 3. How it is verified

- `make security-https` → **20 / 20** cases, from 8 / 8. The twelve new ones:
  - *chains*: `trusted chain` (accept), `chain with the intermediate missing`
    (reject), `intermediate marked not a CA` (reject);
  - *dates*: `expired certificate`, `certificate not valid yet`, `expired
    intermediate` — all rejected, each with Python agreeing;
  - *IPv6*: `trusted IPv6 literal` and `expanded IPv6 literal` (accept, no SNI),
    `IPv4 SAN on an IPv6 URL` and `IPv6 SAN on an IPv4 URL` (reject);
  - *SNI*: `virtual host chosen by SNI` (accept, `SNI=['localhost']`) and
    `literal URL gets no name and the default certificate` (reject, `SNI=[]`).
- Every case's Python verdict equals the expected verdict, and every engine
  verdict equals Python's — 20 of 20 agree.
- The eight Phase 89 cases still pass unchanged in meaning: trusted DNS/IP,
  wrong DNS (matching CN does not override the SAN), wrong IP, DNS SAN as an IP
  identity, DNS-only certificate on an IP URL, and the two untrusted-trust-store
  cases.
- `make asan-core` runs the same target against the sanitized binary, so the
  fixtures are exercised there too; the suite takes about seven seconds.

## 4. What is still open

- **Revocation** (OCSP and CRL), **certificate pinning** and **TLS 1.3
  resumption** are still untested, and now they are the whole of what the row
  leaves open.
- **The oracle is not an independent TLS implementation.** Python's `ssl` is
  the same OpenSSL library this engine loads at run time, so a bug in OpenSSL
  itself would agree with itself. What the oracle does prove is that this
  engine's *use* of the library — the parameters, the host versus IP decision,
  the SNI decision, the check of `SSL_get_verify_result` — matches another
  careful user of it. Phase 89 recorded the same limit.
- **`wss://` is not covered here.** `WebSocket.cpp` builds its own `SSL_CTX`
  and its own identity parameters; `make security-websocket` covers it, and this
  harness says nothing about it.
- **No partial-chain trust.** A trust store holding an intermediate that is not
  self-signed is rejected, as it is by Python: OpenSSL needs
  `X509_V_FLAG_PARTIAL_CHAIN` for that, and browsers accept what it would
  accept. That is a deliberate divergence not taken here — one flag, and a
  decision about which client to match.
