# Phase 110 — The other way through the encoding tables

## What was wrong

Phase 47 landed the WHATWG Encoding Standard's decoders: 40 encodings, 228
labels, BOM sniffing, the `<meta charset>` prescan, and 2,543 `encoding/` WPT
subtests pass. There was **nothing in the other direction**. Any code that had a
code point and wanted bytes had to write its own mapping, and the audit row
that says so ("`TextDecoder` and `TextEncoder` are not exposed to script, and
there is no encoder") is the row this phase takes up, in the half that does not
need anything that does not exist yet.

## What changed

`src/net/TextEncoding.cpp` grew the encoder half of §5, declared in
`include/litebird/net/TextEncoding.hpp`:

- `encode_code_point(EncodingId, uint32_t, std::string&)` — one code point to
  its bytes, or `false` when the encoding cannot carry it.
- `encode_with_encoding(EncodingId, std::string_view utf8, std::string&)` — a
  whole UTF-8 string to bytes, stopping at the first code point the encoding
  cannot carry, with `out` holding what was written before it. That is exactly
  the contract `TextEncoder.encode()` (throw) and `encodeInto()` (a `read`
  count) need.

Twenty-six encoders. The shape of the whole thing is that **the index tables
are already the encoder**: every generated table is code-point-valued and
indexed by pointer, so "index pointer" in the standard is a forward scan of the
same array the decoder indexes. Nothing new was generated and
`TextEncodingTables.cpp` was not touched.

The cases that are not a scan:

| Encoding | What is not a scan |
| --- | --- |
| `gb18030` | index gb18030, then the 18-entry GB18030-2005 side table (fixed byte pairs for U+E78D…U+E864), then index gb18030 **ranges** for the four-byte form; U+E5E5 is refused |
| `gbk` | the same, minus the four-byte form; the euro sign is 0x80 |
| `Big5` | index Big5 with the HKSCS pointers below `(0xA1-0x81)×157` excluded, and six code points that take their **last** pointer |
| `Shift_JIS` | index jis0208 **without pointers 8272..8835**, so duplicate code points use a later pointer; EUDC is encoded from the pointer range the decoder reads back |
| `EUC-JP` | index jis0208; no jis0212 half, because the standard has no EUC-JP encoder for it |
| `ISO-2022-JP` | **stateful**: ASCII / Roman / jis0208, three escape sequences, one written at the end; index ISO-2022-JP katakana (63 entries, NFKC except U+FF9E/U+FF9F) |
| `replacement` | has a decoder and no encoder, as the standard says |
| `UTF-16BE`/`UTF-16LE` | §6 "get an encoder" asserts nobody asks |
| `x-user-defined` | ASCII as itself, U+F780..U+F7FF as 0x80..0xFF |
| the other 27 | the single-byte table the decoder reads, scanned backwards |

U+2212 is encoded as U+FF0D in all three Japanese encodings and U+00A5/U+203E
go to 0x5C/0x7E in Shift_JIS and EUC-JP, because the standard says so.

## How it was verified

**1. The whole of Unicode, both ways.** A throwaway harness (`/tmp`, not
committed) encoded every code point 0..U+10FFFF in every encoding and decoded
it back with this engine's own decoder:

```
$ ./rt
... 2,310,001 encodings, 42,172,559 refusals
```

Every round-trip miss was one of the standard's own asymmetries and nothing
else — 18 for gb18030 and gbk (the side table), 3 each for EUC-JP and Shift_JIS
(U+00A5, U+203E, U+2212), 64 for ISO-2022-JP (the 63 halfwidth katakana and
U+2212). A miss outside those lists would have been a bug; there were none.

**2. An independent implementation.** Python's codecs were the differential
oracle (`python3 /tmp/enc/cmp.py`):

```
windows-1252 agree=251 differ=0     windows-1251 agree=255 differ=0
KOI8-R agree=256 differ=0           macintosh  agree=256 differ=0
ISO-8859-2 agree=256 differ=0       IBM866     agree=256 differ=0
gbk agree=21919 differ=0            Shift_JIS  agree=7067 differ=0
ISO-2022-JP agree=7001 differ=0
TOTAL agree=1,181,769 differ=9,378
```

The 9,378 differences are all explained and all on the Python side of a
deliberate choice: `gb18030` (20 — Python's 2005-era codec shares fixed bytes for
four-byte forms that WHATWG writes through the ranges index), `Big5` (256 —
Python emits HKSCS pointers WHATWG's encoder excludes), `EUC-JP` (280 —
jis0212, which WHATWG has no encoder for), and `EUC-KR` (8,822 — Python's
`euc_kr` is the KS X 1001 codec, while WHATWG indexes **UHC**; the engine's
index[0] is U+AC02, which Python's codec cannot represent directly).

**3. The suite.** `make test` — 267 suites, all passing. The new assertions are
in `test_xml_http_request_answers_data_urls_and_nothing_else`:

- the standard's own byte sequences for every encoding above, taken from §10–§14
  rather than from this implementation, so a bug cannot hide behind a round trip;
- ISO-2022-JP's documented example, `U+00A5` → `1B 28 4A 5C 1B 28 42`;
- a full round trip over U+0000..U+FFFF for windows-1252, EUC-KR, Big5,
  Shift_JIS and EUC-JP;
- and, in the other direction, **every byte of all 27 single-byte tables** read
  back by the decoder and written again by the encoder. This is what found that
  the standard's windows-1252 maps 0x81 and 0x9D to U+0081/U+009D where Python's
  cp1252 leaves holes — the first assertion written for that was wrong, and the
  test caught the assertion rather than the code.

## What is still open

The **script** half of the audit row: `TextDecoder` and `TextEncoder` are still
not globals. That is not an oversight. `TextEncoder.prototype.encode` returns a
`Uint8Array` and `encodeInto` writes into one; `TextDecoder.prototype.decode`
takes an `ArrayBufferView`. Neither can be written honestly until typed arrays
exist, and typed arrays are PR #132 — a separate row, in `src/js/`, in flight.

So this phase is the half that stands on its own: the algorithm beneath the
interface, checked against two independent oracles. The interface is a thin
binding layer away once `Uint8Array` is there, and the two functions declared
here are exactly what it will call.

Also still open, and unchanged from Phase 47: the prescan is one pass over the
first 1,024 bytes with no streaming.
