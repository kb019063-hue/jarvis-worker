# 59. ✅ Completing a surrogate pair after reading or slicing copied the string (Phase 53)

> Moved out of `AUDIT.md` — written there as a `## 59. ✅ Completing a surrogate pair after reading or slicing copied the string (Phase 53)` section.
> The text below is the section as it stood, unchanged.


**The hole.** Phase 41 eliminated the quadratic copy when building surrogate
pairs without intermediate reads by holding an owed trailing high surrogate in
`JsString::m_pending` on top of append-only UTF-8 blocks (finding 47). However,
whenever script read, searched, or sliced the intermediate string before
appending the low half:

```js
var s = '';
for (var i = 0; i < n; i++) {
    s += '\uD83D';
    n_sink += s.indexOf('zz'); // or s.charCodeAt(s.length - 1)
    s += '\uDE00';
}
```

or built on a `substring()` slice that ended at an already-written high half,
the high half had been committed as the 3-byte UTF-8 sequence `\xED\xA0\xBD`.
Appending `'\uDE00'` required turning those 3 bytes + 3 bytes into the 4-byte
UTF-8 sequence `\xF0\x9F\x98\x80` starting three bytes inside the immutable
prefix, copying the whole string on every pair (`58 / 242 / 893 ms` at
5,000 / 10,000 / 20,000 iterations).

**The fix.** Non-ASCII `JsString` buffers (`include/litebird/js/JsString.hpp`,
`src/js/JsString.cpp`) now store 2-byte UTF-16 (`char16_t`) code units, while
pure ASCII buffers remain 1 byte per unit and `sizeof(JsString)` shrinks from
32 back to 24 bytes:

- **Direct code-unit reads:** `length`, `operator[]`, `unit_at`, `substring`,
  `compare`, `equals_utf8`, `is_well_formed`, and `Utf16View::{find,rfind}`
  (with a 64-unit stack fast path) inspect 1-byte or 2-byte units directly
  without flattening or populating UTF-8. Reading a trailing high half never
  mutates the buffer; appending `'\uDE00'` writes 2 bytes at `used`.
- **Lazy UTF-8 and `Wide` caches:** `view()` / `c_str()` fill an aligned,
  checkpointed `Utf8Cache` (`kMarkEvery = 256`) on demand, using bounded
  `Utf8Slice` entries instead of extending `Utf8Cache` across a live unpaired
  high surrogate at `buffer->used`.
- **Safe tail rewind:** `concat()` rewinds `buffer->used` via atomic CAS only
  when `sole_thread == current_thread_tag()`, `refs == 1`, and
  `!cache_covers_past(buffer, a_end)` (checking `Utf8Cache`, `Utf8Slice`s, and
  `Wide`), preventing stale views or cross-thread data races.

**Verification.**

- `JsString::stats()` probe on 20,000 pairs with reads (`s += '\uD83D';
  s.indexOf('zz'); s += '\uDE00'`): `bytes_copied = 279,364`, `blocks = 23`,
  `units_widened = 2`; `substring(0, 1)` slice completed after `charCodeAt`:
  `bytes_copied = 279,366`, `blocks = 23`, `units_widened = 3`; single-owner
  C++ dropped-tail `big.substring(0, big.length() - 1).concat(low)`:
  `bytes_copied = 4`, `shares_text_with = true`.
- `make bench-surrogates` (6 cases, `--max-ms 1000`, Litebird vs Node.js):
  - `build: half a surrogate pair at a time, 20k`: `14 ms` vs `28 ms`
  - `build: half a surrogate pair at a time, 40k`: `22 ms` vs `32 ms`
  - `build: half a surrogate pair, keeping each prefix, 20k`: `11 ms` vs `28 ms`
  - `build: half a surrogate pair, read between halves, 20k`: `17 ms` vs `81 ms`
  - `build: half a surrogate pair, indexOf between halves, 20k`: `154 ms` vs `94 ms`
  - `build: surrogate piece from substring, read then complete, 20k`: `17 ms` vs `85 ms`
- `make bench` (45/45), `make diff-strings` (`50,485/50,580`), `make test`
  (`193/193`, adding `js_string_completes_a_surrogate_pair_after_reading_without_copying`),
  and `make tsan` (`js_string_tsan` and `js_string_asan`, including `.view()` /
  `.wide()` and multi-thread tail-rewind coverage) all pass.
- Merged-tree Release and ASan+UBSan verification: Release `js-conformance`
  `974/989` (Node ref `989/989`), `dom-conformance` `669/669`, `url-conformance`
  `1188/1188`, `conformance` `1792/1792`, `wpt` `7998/8009` (`0` unlisted),
  `diff-trees` `6000/6000`, `verify-media` `35/35`, `fuzz-media` `1800/1800`,
  `verify-color` + `fuzz-color` `5008`, `security-fetch` `3/3`, `security-worker`
  `17/17`, `verify-promise-iterators` `34/34`, `deep_pages` `900/900` random +
  targeted, `xml_roundtrip` `3000/3000`, `diff_xml --expat-only` `4000`;
  ASan+UBSan `test_suite` `193/193`, `js-conformance` `973/989`,
  `dom-conformance` `669/669`, `wpt` `7998/8009`, `asan-deep` `882/882`,
  `fuzz_{parser,pipeline,xml,media,color}` `0` findings.

**Still open.**
- In JS, truncating a completed pair and appending a low half (`s += '\uD83D\uDE00'; s = s.substring(0, s.length - 1); s += '\uDE00'`) still holds `refs == 2` across the JS variable binding and the `+=` operand temporary (`JsEval.cpp` / VM), so JS still copies even though a single-owner C++ handle rewinds in 23 blocks.
- Repeatedly calling host `.view()` on a `JsString` while its buffer ends in an unpaired high surrogate allocates a bounded `Utf8Slice` per call so `Utf8Cache` never pins `\xED\xA0\xBD`; standard JS string operations do not call `.view()`.
