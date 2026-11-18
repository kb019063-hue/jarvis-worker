# Finding 91 — Per-request socket teardown, missing `SIGPIPE` guard, and Brotli block-length code table

**Severity:** medium–high — every HTTP/HTTPS fetch forced a fresh TCP/TLS
handshake (`Connection: close`) and ignored `Accept-Encoding: br`; during
Brotli development, differential fuzzing against `zlib` also uncovered a
non-uniform block-length extra-bit rule and a repeat-count overflow vector.

**Found by:** inspecting `src/net/ResourceLoader.cpp` while taking ROADMAP Tier 1
#7 / Wave 4 #21, and running differential corpus tests against Node.js 22
`zlib.brotliCompressSync`.

## 1. What was wrong

1. **One-shot sockets & EOF framing:** `ResourceLoader::load_http_single_hop`
   emitted `Connection: close`, buffered raw bytes until socket EOF before
   decoding chunked or `Content-Length` bodies, had no `Keep-Alive: timeout/max`
   parser, and never installed `SIG_IGN` on `SIGPIPE` — so writing or running
   `SSL_shutdown` on a peer-closed TLS socket could deliver `SIGPIPE`.
2. **No RFC 7932 `br` codec or stacked `Content-Encoding` order:** Only `gzip`
   and `deflate` were decoded (as a single string rather than comma-separated
   right-to-left codings per RFC 9110 §8.4), and servers offering Brotli fell
   back or failed.
3. **Brotli `kBlockLenExtraBits` asymmetry & repeat overflow:** RFC 7932 §6.2
   gives block-length codes `16..25` extra bit widths `[6, 6, 7, 8, 9, 10, 11,
   12, 13, 24]` — unlike insert/copy length codes (`16..19` having four 6-bit
   slots), block-length codes only have **two** 6-bit codes (`16, 17`) before
   advancing to 7 bits at code `18` (`305`) and 8 bits at `19` (`369`). Initial
   draft tables also allowed complex-code `16`/`17` repeat loops to overflow an
   alphabet if a hostile stream lacked space.

## 2. What changed

- Added pure C++20 RFC 7932 decoding (`include/litebird/net/Brotli.hpp`,
  `src/net/Brotli.cpp`, `src/net/BrotliTables.cpp` via
  `tools/gen_brotli_tables.py`), verified exact bit-width tables for all 26
  block-length, 24 insert-length, and 24 copy-length codes against `zlib`, and
  bounded repeat runs (`repeat > alphabet_size + 2`).
- Added thread-safe `HttpConnectionPool` (`4` per key / `32` total), incremental
  RFC 9112 header/chunked/`Content-Length` framing, `ensure_sigpipe_ignored()`,
  `poll` + `SSL_peek` stale-socket eviction (handling TLS 1.3 `NewSessionTicket`),
  single-attempt retry strictly for idempotent methods, and right-to-left
  `Content-Encoding` decoding (`br`, `gzip`, `deflate`, `identity`).

## 3. How it was verified

- `make verify-brotli` and `make fuzz-brotli` pass **53** valid (**42** Node
  22 differential streams), **49** bomb-cap, **6** invalid, and **2,000** seeded
  mutation fuzz inputs cleanly under both `-O2` Release and ASan+UBSan+LSan.
- `make test` passes **240 / 240** engine suites + **114 / 114** cross-origin
  checks under Release and ASan+UBSan, including `brotli_rfc7932_decoder_and_http_content_encoding`
  and `http11_keep_alive_socket_pooling_and_tls_reuse`.
