# Phase 89 — HTTPS certificate identity for IP URLs

## What was wrong

ResourceLoader verified the chain for every HTTPS connection but configured
`SSL_set1_host` only for non-IP hosts. A trusted certificate for an unrelated
identity was therefore accepted on an IP-literal URL. Existing Phase 85 TLS
keep-alive tests covered success, not negative identity cases.

## What changed

The runtime OpenSSL bindings now require `SSL_get0_param` and
`X509_VERIFY_PARAM_set1_ip_asc`. IPv4/IPv6 literals use the latter before the
handshake, requiring an iPAddress SAN rather than a DNS SAN or common name.
DNS hosts retain `SSL_set1_host`; both configuration return values are checked
and failure closes the connection. IP addresses are still not sent as SNI.
No OpenSSL headers or link-time dependency were added.

`make security-https` creates ephemeral EC certificates and loopback TLS
servers. Python's verifying `ssl` client independently checks the expected
accept/reject decision before the real Litebird CLI navigates to that fixture.
A rejection must have the certificate-failure diagnostic, exit status 1,
and no HTTP bytes received by the server; an unrelated CLI failure cannot pass.
Trusted positive cases must render the marker served over TLS. Keys and trust
files live only in a temporary directory; the system trust store is unchanged.
The suite requires Python ssl and the openssl CLI and does not silently skip.
CI runs it normally and inside `asan-core`.

## Verification

- Before: **5/8** cases passed; wrong IP, DNS-SAN-as-IP, and DNS-only-on-IP
  cases were accepted where Python rejected them.
- After: `make security-https` **8/8**, including trusted DNS/IP success,
  wrong DNS (matching CN does not override SAN), three IP identity failures,
  and untrusted DNS/IP certificates. Sanitized binary: **8/8** too.
- `make test`: **241 suites + 114 cross-origin/channel checks** on the task's
  base; normal JS **1128/1152**, DOM **678/678**, URL **1188/1188**, XML
  **1718/1718**, WPT **9567/9824**, 257 listed failures, zero unlisted.
- `make diff-trees`: four 1500-page batches, zero differences against the
  corrected oracle; `make diff-xml` with lxml: zero differences and 3000
  successful round trips. `make deep`: **1332 runs**, zero failures.
- Sanitized suites and corpora completed, including fuzzers and **882** deep
  runs, zero sanitizer findings. Sanitized JS: **1127/1152** (above the
  existing sanitizer floor); the Expat-only XML differential reports its
  known parser disagreement, not a sanitizer error. No floors changed.

The independent client is Python ssl, not an independent cryptographic
library (both clients use OpenSSL). Protocol requirement:
[RFC 9525 section 6.4](https://www.rfc-editor.org/rfc/rfc9525.html#section-6.4),
and OpenSSL's `X509_VERIFY_PARAM_set1_ip_asc` API contract.

## Still open

IPv6 takes the same implementation path but these fixtures exercise IPv4.
Expired certificates, chain intermediates, revocation, pinning, TLS resumption,
and connection-pool trust changes need additional tests. This does not add
WebSocket TLS: `wss://` still fails closed. DNS common-name fallback behavior
is unchanged. No claim of complete RFC 9525 conformance is made.
