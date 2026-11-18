# Phase 85 — HTTP/1.1 keep-alive socket pooling and RFC 7932 Brotli (`br`) decoding

## 1. What was wrong

Before Phase 85, every HTTP/HTTPS request in `ResourceLoader::load_http_single_hop` had two structural limitations:

1. **Per-request TCP + TLS teardown:** `ResourceLoader` sent `Connection: close` on every outbound HTTP/1.1 request, buffered the entire wire response until EOF (`recv == 0` / `SSL_read == 0`), and tore down both the POSIX socket and the OpenSSL `SSL*`/`SSL_CTX*` on every single resource load. Redirect chains, subresources, and repeated `fetch()` calls to the same origin paid a fresh TCP handshake and full TLS handshake every hop, and `ResourceLoader` never installed `SIGPIPE` suppression (`SIG_IGN`) before calling OpenSSL `SSL_write`/`SSL_shutdown` on a peer-closed socket.
2. **No Brotli (`br`) `Content-Encoding` support:** `ResourceLoader` advertised `Accept-Encoding: gzip, deflate` and rejected `Content-Encoding: br` (or stacked codings such as `br, gzip` / `gzip, br`) even though modern HTTP servers overwhelmingly prefer Brotli for HTML, CSS, JavaScript, and SVG payloads.

## 2. What changed

### 1. Pure C++20 RFC 7932 Brotli (`br`) decoder (`Brotli.hpp`, `Brotli.cpp`, `BrotliTables.cpp`)

- **`tools/gen_brotli_tables.py` & `src/net/BrotliTables.cpp`:** Generates the verbatim RFC 7932 Appendix A static dictionary (`122,784` bytes across lengths `4..24`), all `121` RFC 7932 Appendix B word transforms (`50` prefix/suffix strings), and the `2,048`-byte §7.1 context lookup table from host `libbrotlicommon` without any runtime or link-time dependency on `libbrotli`.
- **`include/litebird/net/Brotli.hpp` & `src/net/Brotli.cpp`:** Implements `Brotli::decompress(data, size, out, max_output_bytes, bytes_consumed)` supporting:
  - `WBITS` window-size decoding (`10..24`), uncompressed meta-blocks, metadata blocks (`MSKIPBYTES`), empty-last blocks, and RFC 7932 §9.2 trailing-padding zero checks;
  - Simple (`1..4` symbols) and complex (run-length code `16`/`17` repeat guards + Huffman canonical decoding with table-overflow and unused-code rejection) prefix codes;
  - Literal, insert-and-copy command (`IAC`), and distance Huffman trees, `NBLTYPES`/`BLEN` block-type switches, context maps (`UTF8`/`Signed`/`MSB6`/`LSB6`), distance ring buffer (`dist_rb[4]`), `NPOSTFIX`/`NDIRECT` distance extra-bit formulas, backward window copies, and static-dictionary + transform word copies;
  - Strict `max_output_bytes` bomb limits checked *before* pushing output bytes, failing cleanly on malformed streams or output limits without UB or leaks.

### 2. Thread-safe HTTP/1.1 & TLS `keep-alive` socket pool + incremental RFC 9112 framing (`ResourceLoader.hpp`, `ResourceLoader.cpp`)

- **Public pool controls (`ResourceLoader.hpp`):** Declared `ResourceLoader::clear_http_connection_pool()` and `ResourceLoader::http_connection_pool_size()`, and updated `set_ca_bundle_for_testing()` to clear idle sockets whenever the test CA bundle changes.
- **`HttpConnectionPool` (`src/net/ResourceLoader.cpp`):** Single-list MRU/LRU pool keyed by `(scheme|lowercase_host:port|custom_ca)` bounded to **4 idle connections per origin key** and **32 idle connections total**, with default 30 s TTL (capped at 60 s).
- **Stale-socket detection & safe retry:** Before returning an idle socket, `is_http_connection_healthy()` checks expiry, `max_remaining`, `::poll` (`POLLIN`/`POLLERR`/`POLLHUP`), and for TLS connections `SSL_pending` + non-blocking `SSL_peek` (safely absorbing post-handshake TLS 1.3 `NewSessionTicket` records while discarding closed or desynced sockets). If a pooled socket suffers a write failure or zero-byte EOF race before response headers arrive, **idempotent methods (`GET`, `HEAD`, `OPTIONS`, `PUT`, `DELETE`, `TRACE`) retry once** on a freshly dialed connection, while non-idempotent methods (`POST`, `PATCH`) fail closed without double submission.
- **Exact RFC 9112 framing & `Keep-Alive` negotiation:**
  - Sends `Accept-Encoding: gzip, deflate, br` and `Connection: keep-alive` by default, and installs `ensure_sigpipe_ignored()` (`SIGPIPE` → `SIG_IGN`) so closed TLS writes return errors cleanly.
  - Frames headers incrementally, treats `1xx`/`204`/`304`/`HEAD` as bodyless (`HEAD` ignores large `Content-Length` values for response-size rejection while still validating syntax), rejects malformed or conflicting `Content-Length` values (`5, 10`), and reads chunked bodies via `IncrementalChunkDecoder` (handling split TCP/TLS writes, chunk extensions, bounded trailer lines, and compaction while preserving the `MaxHttpResponseBytes` 16 MiB wire cap).
  - Honors `Connection: close`, `HTTP/1.0` opt-in `Connection: keep-alive`, and `Keep-Alive: timeout=S, max=M` directives, disabling reuse on ambiguous framing (`Transfer-Encoding` + `Content-Length`, close-delimited bodies, or trailing bytes past body end) and skipping `SSL_shutdown` on dead sockets.
  - Decodes `Content-Encoding` token lists right-to-left (`br`, `gzip`/`x-gzip`, `deflate`, `identity`).

## 3. How it was verified

- **`make verify-brotli` & `make fuzz-brotli` (`tools/conformance/verify_brotli.py`):** **53** valid streams (**42** live Node 22 `zlib.brotliCompressSync` differential streams across qualities `0..11`, window bits `10..24`, all modes, multi-meta-block streams, `>1 MiB` 5/6-nibble `MLEN` payloads, and UTF-8 dictionary transforms), **49** exact/`exact - 1` bomb-cap checks, **6** crafted invalid headers, and **2,000** seeded truncation/bit-flip/random-byte mutation fuzz inputs — **0 discrepancies** in both `-O2` Release and `-fsanitize=address,undefined` (`ASAN_OPTIONS=detect_leaks=1`).
- **`make test`:** **237 / 237** engine test suites + **114 / 114** cross-origin checks pass in Release and ASan+UBSan, including `brotli_rfc7932_decoder_and_http_content_encoding` and `http11_keep_alive_socket_pooling_and_tls_reuse` (persistent HTTP/1.1 reuse across `Content-Length`/split-chunked/`204`/`HEAD`/`Connection: close`, `Keep-Alive: timeout/max`, `HTTP/1.0` rules, stale-socket `GET` retry vs `POST` refusal, concurrent per-key cap `<= 4`, conflicting `Content-Length`, and native loopback OpenSSL `SSL*` reuse with `Content-Encoding: br`).
- **Existing scoreboards (`make conformance`, `make js-conformance`, `make dom-conformance`, `make url-conformance`, `make wpt`, `make security-fetch`, `make security-worker`):** All pass at `1967/1973`, `1128/1152`, `678/678`, `1188/1188`, and `9567/9824` (`0` unlisted WPT failures).

## 4. What is still open

- HTTP authentication challenges (`WWW-Authenticate` / `401` / `407` proxies) and HTTP/2 (`ALPN: h2`) multiplexing.
- TLS OCSP/CRL revocation checking and certificate pinning (tracked in `ROADMAP.md` Wave 4 #22).
- `WOFF2` font decompression in `FontRenderer` (which can now reuse `Brotli::decompress` in a future font phase).
