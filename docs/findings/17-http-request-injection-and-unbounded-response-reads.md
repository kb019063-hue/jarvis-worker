# 17. ✅ HTTP request injection and unbounded response reads

> Moved out of `AUDIT.md` — written there as a `## 17. ✅ HTTP request injection and unbounded response reads` section.
> The text below is the section as it stood, unchanged.


The POSIX HTTP client previously concatenated caller-supplied method/header
strings and URL-derived request targets directly into a raw HTTP/1.1 request.
CR/LF in those values could inject request lines or headers. It also accumulated
the peer's response until disconnect without a size bound.

`ResourceLoader::load_http()` now accepts only HTTP token methods/header names,
rejects control bytes (including CR/LF) in header values and request targets,
validates URL authority/port parsing, and manages framing headers (accepting a
caller-supplied `Content-Length` only when it exactly matches the body). Complete
request headers are capped at 64 KiB before DNS/network work.
Incoming headers and the full response stream are capped at 64 KiB and 16 MiB
respectively; malformed/incomplete response headers and invalid or oversized
`Content-Length` values are rejected. Socket sends handle partial writes and
interrupted system calls.

Regression coverage exercises method, header-value, and request-target
injection attempts, a successful local loopback response, and a response that
exceeds the 16 MiB cap. Tests use a loopback socket only and do not require
external networking.
