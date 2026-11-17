# 53. ✅ A page was read as UTF-8 whatever it said it was (Phase 47)

> Moved out of `AUDIT.md` — written there as a `## 53. ✅ A page was read as UTF-8 whatever it said it was (Phase 47)` section.
> The text below is the section as it stood, unchanged.


**The hole.** One decoder existed — `utf8_decode` — plus an XML declaration
that could also name UTF-16 or Latin-1. An HTML page's `<meta charset>` was
text nobody parsed, an HTTP `Content-Type`'s charset was dropped by the
loader, `document.characterSet` did not exist, `CSS @charset` was a comment,
and `data:` URLs only knew UTF-8. Two scoreboards measured it: the W3C XML
suite skipped 16 documents as "an encoding this build cannot read", and
web-platform-tests' `encoding/` directory, where a browser's tables are
checked byte by byte, could not be run at all.

**The fix.** `src/net/TextEncoding.cpp` implements the WHATWG Encoding
Standard's decoder algorithms over tables generated from `whatwg/encoding`
`@ be17ac5c49881336538acfc7b40dba3c8009144b` (`src/net/TextEncodingTables.cpp`,
881 KB; 40 encodings, 228 labels, every index and every `gb18030` range, with
`Big5`'s four two-code-point pointers and `ISO-8859-8-I`'s shared index
written out). `HtmlParser::decode_bytes` follows HTML §13.2.3.1 and
`prescan_meta_charset` is §13.2.3.2 over the first 1,024 bytes. XML validates
`[81] EncName` before looking a label up and decodes fatally; a navigated
`.xml` that fails gets the `<parsererror>` element browsers show. The same
functions serve `data:` URLs, `XMLHttpRequest`, `<script src>`, `<iframe>`
subdocuments and CSS `@charset`. `document.characterSet`/`charset`/
`inputEncoding` report the result. `--content-type=<mime>` and a
`<file>.headers` sidecar let the conformance runner hand a page the type a
server would.

**Verification.** W3C XML suite **1718/1718** (100.0%), 0 encoding skips, 261
canonical forms compared, 2,585 documents run, 0 crashes and 0 timeouts — the
16 documents that were skipped are read and each is still answered as the
suite says; the Fuji Xerox `weekly-euc-jp`, `weekly-iso-2022-jp`,
`weekly-shift_jis`, `weekly-utf-16` and `weekly-little-endian` documents now
serialize to the exact 2,822 bytes of `weekly-utf-8.xml`. web-platform-tests
**7968/8009 across 103 files, 41 listed failures and 0 unlisted** (5421/5463
across 87 files before): 16 new `encoding/` files pass 2543/2543, including
`big5-decode-extra.html` 2472/2472, the four `*-decode-errors.html` families,
`utf-32.html` and `utf-32-from-win1252.html` 8/8, and
`remove-only-one-bom.html` 3/3; `DOMParser-parseFromString-encoding.html`
4/4 stops being an expected failure, so the list falls from 42 to 41. The
37 vendored WPT files were fetched as raw blobs at
`f2b7b7add021b5f40e30c65dd348135058996249` (the contents API transcodes the
UTF-32 ones, which would have made the tests they belong to meaningless).
`make test` passes **177** suites (174 before; three new ones cover the label
table, the single-byte and multi-byte decoders' edge cases — `ISO-8859-6`'s
unmapped `0xA1`, `gb18030`'s four-byte pushback, `Big5`'s two-code-point
pointers — and the prescan, `characterSet`, XHR and subdocument paths).
`make conformance` 1781/1792, JS 926/940, DOM 644/644, URL 1188/1188,
`make diff-xml` 4000 documents with 0 DIFFER and 3000 round trips, `make wpt`
as above, `make deep` 1,332 runs 0 failed. Floors raised, none lowered:
`XML_FLOOR` 1702 -> **1718**, `WPT_FLOOR` 5421 -> **7968**.

**Still open.** No encoders (nothing here needs to write legacy bytes, so the
standard's encoder-side special cases are untested); `TextDecoder` and
`TextEncoder` are not exposed to script, and the `encoding/` tests that need
those globals are not vendored — the ones vendored iframe a document and
read the text it decodes to. `XMLSerializer` does not consult
`document.characterSet` for an output declaration, an encoding change on an
already-parsed document is not modelled, and the prescan is one pass over the
first 1,024 bytes with none of a browser's streaming. See `AGENTS.md`.
