# Finding 153 — every certificate refusal said the same thing

## What was wrong

Six certificate fixtures failed for five different reasons and the engine
printed one identical sentence for all of them. `ResourceLoader` compared
`SSL_get_verify_result(...) != 0` and threw the code away, so an expired
certificate, a certificate that was not valid yet, an untrusted issuer, a
missing intermediate, a hostname mismatch and an IP address mismatch were
indistinguishable in the output:

```
wrong name   TLS handshake or certificate verification failed for localhost
expired      TLS handshake or certificate verification failed for localhost
not yet      TLS handshake or certificate verification failed for localhost
untrusted    TLS handshake or certificate verification failed for localhost
no inter     TLS handshake or certificate verification failed for localhost
```

The WebSocket path printed the same line for `wss://`.

This mattered twice over. A page author could not tell a server with an
expired certificate from one that was merely not trusted, and — more
importantly for the tests — a fixture that failed for the *wrong* reason was
indistinguishable from one that failed for the right one. Both security
harnesses were asking the same weak question: "was it refused?" A fixture that
was refused because the `keyUsage` extension was malformed would have passed
as proof that hostname checking works.

## What changed

The loader asks for the code and renders it, with OpenSSL's own name for it
when that symbol is available:

```
TLS certificate verification failed (code 62, hostname mismatch) for localhost
```

The code is printed either way; only the words depend on
`X509_verify_cert_error_string`, which is loaded as an *optional* symbol — the
same way `SSL_pending` and `RAND_bytes` are — so a build without it loses the
wording and not the TLS layer. A handshake that failed for a reason unrelated
to a certificate (no shared cipher, a truncated record) has no code, and says
so instead of blaming a certificate.

`wss://` reports the same message. One detail matters there: a refused
certificate usually makes `SSL_connect` itself fail rather than return 1 with a
non-zero verify result. The first attempt read the code only on the `rc == 1`
path and so reported nothing at all for the ordinary case. The code is now
read after any failed handshake — and *before* `cleanup_transport()`, which may
free the `SSL` object the query needs. An earlier draft read it after cleanup,
which is a use-after-free.

## Verification

Both harnesses were taught to check the *reason* a fixture was refused, and
both were then run against the unfixed engine to show the check can fail:

| harness | against the unfixed engine | with the fix |
| --- | --- | --- |
| `tools/security/verify_https.py` | 14 failures / 20 | 20 cases, 0 failures |
| `tools/security/verify_websocket.py` | 3 failures / 32 | 32 checks, 0 failures |

The HTTPS harness holds each of the twenty cases to the name it was built to
fail for, and the names come from `openssl verify` rather than from memory —
a first hand-written table had 64 as "hostname mismatch" and 79 as "invalid
CA", and the tool contradicted both. The engine's code is then compared
against the oracle's name through a table, so the two readers of the same
decision are checked against each other rather than one string being asserted
twice.

The four `wss://` refusals previously all asserted `err,close:1006`, which is
what every one of them does. Each is now held to its own code — 18 self-signed,
64 address mismatch, 10 expired — read from the error event the page receives.
A refusal on a socket never reaches the engine's stderr, which is where the
first version of the check looked; it found nothing and was blind in exactly
the way it was written to cure.

`make test`: **301/301**.

## What is still not covered

The oracle is still Python's `ssl`, which is the same OpenSSL library the
engine uses — not an independent TLS implementation. Revocation (OCSP/CRL),
pinning and TLS 1.3 resumption remain untested. What changed is that the two
readers are now compared on the reason, so a disagreement about *why* a
certificate is bad surfaces; it does not make them independent.
