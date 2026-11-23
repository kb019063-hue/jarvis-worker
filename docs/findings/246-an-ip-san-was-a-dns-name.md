# Finding 246 — an IP SAN was a DNS name

Phase 242.

## What was wrong

The HTTPS oracle in `tools/security/verify_https.py` checked that a DNS SAN
does not satisfy an IP literal:

```python
('DNS-only certificate on IP URL', dns_cert, '127.0.0.1',
 'IP address mismatch')
```

but not the other way round. A certificate with `SAN=IP:127.0.0.1`
presented to `https://localhost/` would have been accepted by a verifier
that treated an IP SAN as a DNS name — `X509_check_host("localhost")` must
not match `iPAddress 127.0.0.1`, but the only test that could have caught
it was `DNS:127.0.0.1` on `127.0.0.1`, which checks the opposite type error
(`X509_check_ip` on a DNS SAN, code 64). The two code paths are separate
in OpenSSL and in the loader's `SSL_set1_host`/`SSL_set_tlsext_host_name`
setup; the twenty-case table had one, not both, and 20/20 hid the
asymmetry.

The engine was correct — `SSL_set1_host("localhost")` with an IP-only leaf
fails with `X509_V_ERR_HOSTNAME_MISMATCH` (62) — but the table could not
say so. The defect was not in the ink, but in the paper that promised to
detect the next defect.

## Why it stayed hidden

Identity checks are two functions, not one, and the table was built from
the first half only. Phase 89 fixed IP-literal identity and Phase 100 grew
the fixtures to twenty; Phase 148 made each fixture name the reason it must
fail for, so a fixture that failed for the wrong code would finally be
red. The new harness caught five wrong-code cases before the fix went in,
but the missing case was not a wrong code — it was a missing row. No
fixture asked for it, so no code could be wrong about it. The row's
**Still open** listed revocation, pinning and TLS 1.3 resumption, and the
asymmetry looked like a detail of the twenty that were already there.

The oracle's two readers — Python's `ssl` and Litebird's `ResourceLoader`
— share the same OpenSSL library, so a bug that made both accept an IP
SAN for a DNS name would be invisible even with the row. The one-line
addition does not fix that, only the gap inside the library's own API.

## What changed

One line of table in `verify_https.py`:

```python
('IP-only certificate on DNS URL', ip_cert, 'localhost',
 'hostname mismatch')
```

reusing the `IP:127.0.0.1` leaf and the `root` trust. Python's reference
must be `hostname mismatch` (62), the engine must refuse with `code 62`
and no request, and SNI must still be `['localhost']`. The twenty old
cases are unchanged.

## How to avoid it next time

Keep the table symmetric: for every SAN type, test every host type. DNS
SAN on DNS URL (pass), DNS SAN on IP URL (fail, code 64), IP SAN on IP
URL (pass), IP SAN on DNS URL (fail, code 62) is the 2×2, and the chain,
date and SNI rows are the other axes. When a new SAN type is added
(`otherName`, `directoryName`), add its row before the engine is taught to
handle it, so the table fails first and the fix turns it green. The next
time a fixture is added for revocation, add the CRL to the trust store in
`confirm_verify_names` the same way the CA database is built — that is the
place the table earns its right to claim the code.
