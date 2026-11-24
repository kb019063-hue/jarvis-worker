# Phase 242 — an IP SAN is not a DNS name

Task row: **HTTPS certificate test coverage remains partial.** (`AGENTS.md` →
Open tasks).
Finding 246.

## 1. What was wrong

`tools/security/verify_https.py` exercised twenty fixtures (Phase 100 + Phase
148 + Phase 89): DNS SAN, IP SAN, wrong SAN, DNS-as-IP, DNS-only on IP URL,
untrusted, chain (present/missing/CA:FALSE), dates (expired leaf/future
leaf/expired intermediate), IPv6 literals (compressed/expanded) and the wrong
family both ways, and SNI (name vs no-name). Every fixture was held to the
reason it was built to fail for, not just “rejected” — the `CODE_FOR_REASON`
table that Phase 148 introduced.

One direction was missing: **an IP SAN presented to a DNS name**. The
existing `DNS-only certificate on IP URL` checked that a DNS SAN does not
satisfy an IP literal (`DNS:localhost` on `127.0.0.1` → `IP address
mismatch`, code 64). The symmetric case — `IP:127.0.0.1` on `localhost` →
`hostname mismatch`, code 62 — was not exercised. A verifier that accepted
an IP SAN for a DNS name would have passed 20/20, because the two checks
are different code paths in OpenSSL (`X509_check_host` vs
`X509_check_ip`) and in `SSL_set1_host` the IP-literal branch is never
taken for a DNS name.

The gap was not a bug in the engine — the loader's `SSL_set1_host`
parameter is already right — but a gap in the oracle that is supposed to
catch the next bug. The row's **Still open** lists revocation, pinning and
TLS 1.3 resumption; the identity half is done except for this one
asymmetry.

## 2. What changed

One file, one fixture:

`tools/security/verify_https.py`:

```python
('IP-only certificate on DNS URL', identities['ip'][0], identities['ip'][1],
 'localhost', root_ca[0], 'hostname mismatch', None, None),
```

added after `DNS-only certificate on IP URL`. `identities['ip']` is the
leaf with `SAN=IP:127.0.0.1` (created by `issued(..., san='IP:127.0.0.1')`
in `root_ca`), presented to `host='localhost'` with trust `root_ca[0]`.
The reference client (`reason()` via Python `ssl.SSLCertVerificationError`)
must answer `hostname mismatch` (code 62), and the engine must refuse with
`certificate verification failed (code 62, hostname mismatch)` and no HTTP
request. No new certificate is created — the `identities['ip']` leaf and
`root_ca` trust are reused, so the fixture is one line of table.

No C++ change: `src/net/ResourceLoader.cpp` already sets
`SSL_set1_host`/`SSL_set1_host` correctly, and the twenty existing cases
still pass.

## 3. How it was verified

```
python3 tools/security/verify_https.py --binary build/litebird
```

```
PASS trusted DNS: Python=OK, Litebird loaded it, SNI=['localhost']
PASS trusted IP: Python=OK, Litebird loaded it, SNI=[]
PASS wrong DNS (CN matches, SAN does not): Python=hostname mismatch, Litebird refused it: code 62 hostname mismatch
PASS wrong IP: Python=IP address mismatch, Litebird refused it: code 64 IP address mismatch, SNI=[]
PASS DNS SAN is not an IP SAN: Python=IP address mismatch, Litebird refused it: code 64 IP address mismatch
PASS DNS-only certificate on IP URL: Python=IP address mismatch, Litebird refused it: code 64 IP address mismatch
PASS IP-only certificate on DNS URL: Python=hostname mismatch, Litebird refused it: code 62 hostname mismatch
PASS untrusted DNS: Python=unable to get local issuer certificate, Litebird refused it: code 20 unable to get local issuer certificate
PASS untrusted IP: Python=unable to get local issuer certificate, Litebird refused it: code 20 unable to get local issuer certificate
PASS trusted chain: Python=OK, Litebird loaded it
PASS chain with the intermediate missing: Python=unable to get local issuer certificate, Litebird refused it: code 20 unable to get local issuer certificate
PASS intermediate marked not a CA: Python=invalid CA certificate, Litebird refused it: code 79 invalid CA certificate
PASS expired certificate: Python=certificate has expired, Litebird refused it: code 10 certificate has expired
PASS certificate not valid yet: Python=certificate is not yet valid, Litebird refused it: code 9 certificate is not yet valid
PASS expired intermediate: Python=certificate has expired, Litebird refused it: code 10 certificate has expired
PASS trusted IPv6 literal: Python=OK, Litebird loaded it, SNI=[]
PASS expanded IPv6 literal: Python=OK, Litebird loaded it, SNI=[]
PASS IPv4 SAN on an IPv6 URL: Python=IP address mismatch, Litebird refused it: code 64 IP address mismatch
PASS IPv6 SAN on an IPv4 URL: Python=IP address mismatch, Litebird refused it: code 64 IP address mismatch
PASS virtual host chosen by SNI: Python=OK, Litebird loaded it, SNI=['localhost']
PASS literal URL gets no name and the default certificate: Python=IP address mismatch, Litebird refused it: code 64 IP address mismatch, SNI=[]
21 HTTPS cases; 0 failure(s).
```

Was 20 cases; now 21, with the new `IP-only certificate on DNS URL` as the
negative control for the other direction. Runs from a pre-fix checkout of
the script (without the line) print `20 HTTPS cases`; a verifier that
accepted IP SANs for DNS names would print `FAIL IP-only certificate on
DNS URL: Python=hostname mismatch, Litebird loaded it` and exit 1.

```
make test
All 375 engine test suites passed successfully!
```

No engine suite touches TLS; the change is confined to the Python oracle.

## 4. What is still open

The row's three deliberate gaps remain:

- Revocation (OCSP/CRL): the PKI has no CRL distribution point, no OCSP
  responder, and `ResourceLoader` never loads a CRL. A revoked leaf would
  still load.

- Pinning (HPKP/`Expect-CT`/static pins): no pin set is stored, no
  `X509_check_host` pin is checked.

- TLS 1.3 resumption and 0-RTT: the loopback server negotiates TLS 1.3 but
  the harness never resumes a session and never checks early data.

- The oracle is still the same OpenSSL library, not an independent TLS
  stack — a bug in both would be invisible. A non-self-signed leaf in the
  trust store is still rejected as Python rejects it (`X509_V_FLAG_PARTIAL_CHAIN`
  browsers accept).

Phase 242, finding 246 closes only the IP↔DNS asymmetry; the four items
above are the next steps.
