# Phase 47 Architecture: The WHATWG Encoding Standard, the `<meta charset>` Prescan, and XML/WPT Encodings

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 47 Architecture: The WHATWG Encoding Standard, the `<meta charset>` Prescan, and XML/WPT Encodings` section.
> The text below is the section as it stood, unchanged.


### 1. The hole

There was one text decoder, `utf8_decode` in `src/net/DataUrl.cpp`, and an XML
declaration that could also say UTF-16 or Latin-1. Everything else that
arrived as bytes was assumed to be UTF-8: an HTML page named its encoding
only in `<meta charset>`, which nothing read, `document.characterSet` did not
exist, the `CharacterSet` labels of an HTTP `Content-Type` were dropped by the
loader, and CSS `@charset` was a comment. The cost showed up in two
scoreboards: 16 documents of the W3C XML suite were skipped as "an encoding
this build cannot read", and web-platform-tests' `encoding/` directory — the
place where the tables are checked byte by byte — could not run at all.

### 2. The tables, generated and pinned

`include/litebird/net/TextEncoding.hpp` is the standard's model: an
`EncodingId` for each of the **40 encodings**, a label map of **228 labels**
(normalised per §2, so `latin1`, `ISO-8859-1` and `L1` are one entry), the
`Decoder`/`Encoder` flag of each so `replacement` is refused where the
standard refuses it, and two decode entry points:

- `decode_with_encoding(bytes, EncodingId, out, fatal)` — the standard's
  algorithm. Non-fatal mode emits one `U+FFFD` per *error* (not per byte:
  `EUC-JP`'s three-byte errors, `ISO-2022-JP`'s escape state and the shared
  UTF-16 decoder's single end-of-stream error are all reproduced), fatal mode
  stops at the first one and returns false.
- `decode_text(bytes, label)` — BOM sniffing, then the label, then the
  fallback window-1252 that HTML requires for an unknown label.

`src/net/TextEncodingTables.cpp` (881 KB) holds the indexes and ranges,
generated from `whatwg/encoding` at
`be17ac5c49881336538acfc7b40dba3c8009144b` and recorded in the header, so the
bytes on disk are auditable against a pinned commit rather than retyped. All
of them except `big5` (`max` pointer `0x2f9d4`) fit in `uint16_t`, with `0` as
the `null` marker the standard uses; `ISO-8859-8-I` shares
`index-iso-8859-8`. Three details are not a table lookup and are written out
in code: the four `Big5` pointers (1133, 1135, 1164, 1166) that stand for a
letter plus a combining mark, `gb18030`'s ranges (pointer 7457 is `U+E7C7`
even though it lies inside a null gap, and two ranges above 39419 are null),
and `gb18030`'s rule that a failed four-byte sequence hands its second byte
back to the stream (`0x81 0x30 0x81 0x40` is `U+FFFD`, `'0'`, then the two
bytes `0x81 0x40` = `U+4E02`).

### 3. How a document chooses its encoding

- **HTML**: `HtmlParser::decode_bytes` applies the standard's order (§13.2.3.1)
  — BOM, transport `Content-Type`, `<meta>`, fallback — and
  `HtmlParser::prescan_meta_charset` is the §13.2.3.2 prescan: the first 1,024
  bytes, `<meta charset>` and `<meta http-equiv="content-type">`, comments,
  `<script>`/`<style>` and the attribute quoting rules, and the "UTF-16 that
  is not the BOM becomes UTF-8" corrections. `Document` stores the answer
  and `document.characterSet`/`charset`/`inputEncoding` report it.
- **XML**: `XmlParser.cpp` validates the declaration's `[81] EncName` (so
  `UTF8!` is rejected as a grammar error, not silently accepted as a label),
  refuses `replacement`, and decodes with the fatal mode, because XML has no
  error-recovery text. A navigated `.xml` page sets `characterSet` before
  parsing and, on a fatal error, gets the `<parsererror>` element browsers
  show instead of an empty document.
- **Everything else**: `data:` URLs (`;charset=`), `XMLHttpRequest`
  (`overrideMimeType`), `<script src>` and `<iframe>` subdocuments, and CSS
  `@charset` all funnel through the same two functions, so a page and its
  subresources cannot disagree about what a byte means. `--content-type=<mime>`
  on the command line, and an optional `<file>.headers` sidecar next to a
  `--map-origin` file, let the conformance runner give a page the type a
  server would.

### 4. What it moved

| Scoreboard | Before | After |
| :--- | ---: | ---: |
| W3C XML suite (documents that apply) | 1702/1702, 16 skipped for encoding | **1718/1718**, 0 skipped |
| Fuji Xerox XML: EUC-JP, Shift_JIS, ISO-2022-JP, UTF-16, UTF-16LE | 5 refused | **5 canonical forms equal to the UTF-8 translation's 2,822 bytes** |
| web-platform-tests | 5421/5463 across 87 files | **7968/8009 across 103 files**, 0 unlisted |
| — of which `encoding/` | not run | **2543/2543 in 16 files** (`big5-decode-extra` 2472) |
| Unit suites | 174 | **177** |

`tests/wpt/` gained 37 vendored files at `wpt`
`f2b7b7add021b5f40e30c65dd348135058996249`, fetched as raw blobs because the
UTF-32 ones are not UTF-8 and the contents API would have transcoded them.
`tools/conformance/run_wpt.py` now stages each page as `bytes` and forwards
its `<page>.headers` `Content-Type` to `--content-type`, which is how
`bom-handling.html` and `utf-32-from-win1252.html` can assert what they
assert. `tests/wpt/expected-failures.txt` went from 42 to 41 entries: the one
that went away was `DOMParser-parseFromString-encoding.html`, which passes 4/4
now that `DOMParser` honours a `Content-Type` charset.

### 5. What is still open

Encoders are not implemented — this is a decoder, and nothing here needs
to write legacy bytes — so the standard's encoder-side special cases (Big5's
`U+00CA`/`U+00EA`, `gb18030`'s ranges in the other direction) are untested.
`TextDecoder` and `TextEncoder` are not exposed to script at all, and the
`encoding/` tests that need those globals are not vendored; the ones vendored
are the byte-level pages that iframe a document and read the text it decodes
to. `document.characterSet` is not consulted by `XMLSerializer` when it
writes a declaration, an `encoding` change on an already-parsed document
(the standard's "change the encoding") is not modelled, and the prescan
stops at 1,024 bytes with none of the streaming a browser needs when a page
arrives in pieces.

---
