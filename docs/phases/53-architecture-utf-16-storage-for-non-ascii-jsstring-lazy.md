# Phase 53 Architecture: UTF-16 Storage for Non-ASCII `JsString` (Lazy UTF-8 & Tail Rewind)

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 53 Architecture: UTF-16 Storage for Non-ASCII `JsString` (Lazy UTF-8 & Tail Rewind)` section.
> The text below is the section as it stood, unchanged.


### 1. Why the old representation still copied

Phase 41 deferred writing a trailing high surrogate (`0xD800..0xDBFF`) in
`m_pending` on a UTF-8 `Buffer`, which made tight `s += '\uD83D'; s +=
'\uDE00'` loops linear when nothing read the intermediate string. Once
anything read `s` (`charCodeAt`, `indexOf`, `substring`) between the halves,
`settle()` had to commit the 3-byte UTF-8 sequence `\xED\xA0\xBD` into the
block; when `'\uDE00'` arrived next, combining them required erasing those 3
bytes and writing the 4-byte UTF-8 scalar `\xF0\x9F\x98\x80`, which violated
append-only UTF-8 sharing and forced a full copy (`58 / 242 / 893 ms` at 5k /
10k / 20k iterations). A slice created with `substring` whose end landed on an
already-written high half suffered the same copy when completed.

### 2. Dual-width `Buffer` and the 24-byte handle

`include/litebird/js/JsString.hpp` and `src/js/JsString.cpp` now store raw
code units in two widths instead of UTF-8:

```
JsString (24 bytes): { const Buffer* buffer, uint32_t offset, uint32_t units, std::atomic<uint32_t> utf8_bytes }
Buffer:              { is_utf16, capacity, std::atomic<uint32_t> used, data[], sole_thread,
                       utf8_mutex, utf8_cache (+ marks[] every 256 units), utf8_slices,
                       wide_mutex, wide_cache (lazy Wide for Utf16View/RegExp) }
```

- **Pure ASCII stays 1 byte per code unit** (`is_utf16 == false`), keeping
  ASCII storage density and byte-for-byte indexing unchanged.
- **Any non-ASCII string uses 2-byte `char16_t` units** (`is_utf16 == true`),
  replacing `m_pending`. In UTF-16, an unpaired high surrogate (`0xD800..0xDBFF`)
  is a normal 16-bit code unit at `used - 1`, and a matching low half appended
  later is simply written at `used` without altering any preceding bytes.
- `sizeof(JsString)` returns to 24 bytes (down from Phase 41's 32 bytes by
  eliminating `m_pending` and folding `m_utf8_bytes` into an atomic cache slot).

### 3. Direct UTF-16 reads and lazy UTF-8 / `Wide` materialization

- All JS string operations (`length`, `operator[]`, `unit_at`, `substring`,
  `compare`, `equals_utf8`, `is_well_formed`, `find`, `rfind`) operate directly
  on the 1-byte or 2-byte buffer without materializing UTF-8.
- `Utf16View::{find,rfind}` encode needles of up to 64 code units into a
  stack buffer to match against UTF-16 haystacks without heap allocation in
  inner `indexOf` loops (`20,000` `indexOf('zz')` calls between halves drop to
  ~150 ms end-to-end).
- `equals_utf8` validates equality against a host UTF-8 `string_view` in place
  without populating `Utf8Cache`.
- Host callers needing UTF-8 (`view()`, `c_str()`) populate `Buffer::Utf8Cache`
  lazily under `utf8_mutex` using periodic offset checkpoints (`kMarkEvery =
  256`) so substring views seek from the nearest mark.
- **Unpaired-surrogate cache guard:** if the end of a non-slice query lands
  at `buffer->used` on an unpaired high surrogate (`0xD800..0xDBFF`), encoding
  `\xED\xA0\xBD` into the shared `Utf8Cache` prefix would freeze a 3-byte stub
  at the byte offset where a later appended low half needs a 4-byte scalar
  `\xF0...`. Instead `view()` materializes a bounded `Utf8Slice` in
  `buffer->utf8_slices` so `Utf8Cache` stops before the live high half.

### 4. In-place extension and sole-thread tail rewind

`concat()` appends at `buffer->used` whenever `a` ends at `used` and capacity
permits (or widens ASCII to UTF-16 once when capacity allows or on fresh
allocation). When a temporary tail after `a` was dropped so `a_end < used`,
`concat()` may atomically CAS `buffer->used` back from `tail_end` to `a_end`
only when **all three** invariants hold:

1. `buffer->sole_thread == current_thread_tag()` (cleared permanently once any
   `JsString` handle crosses threads via share/copy across thread boundaries).
2. `buffer->refs.load(acquire) == 1` — `a` is the only live handle, though a
   `const JsString&` passed across threads could still observe `refs == 1`, which
   is why `sole_thread` is checked in addition to `refs`.
3. `!cache_covers_past(buffer, a_end)` — under `utf8_mutex` and `wide_mutex`,
   no cached UTF-8 prefix (`utf8_len_units`), no `Utf8Slice` (`start + units`),
   and no `Wide` cache (`wide_len_units`) extends past `a_end`.

### 5. Verification & open nuances

- `make test`: **193/193** unit suites (including updated
  `js_strings_are_shared_and_read_in_constant_time` checking `sizeof(JsString) == 24`,
  `js_string_owes_a_trailing_high_half_until_it_is_read`, and new
  `js_string_completes_a_surrogate_pair_after_reading_without_copying`), in both
  Release and ASan+UBSan (`build-asan/test_suite`).
- `make tsan`: `js_string_tsan` and `js_string_asan` pass, exercising
  randomized UTF-16 model checks, `.view()`/`.wide()` cache coverage, and
  sole-thread vs multi-thread tail rewind under TSan and ASan/LSan.
- `make bench-surrogates`: all **6** cases pass under 1 s (`14 / 22 / 11 / 17 /
  154 / 17 ms`); `make bench`: all **45** cases pass; `make diff-strings`:
  `50,485/50,580` (`95` differ).
- Full merged-tree Release + ASan+UBSan runs (`js-conformance` 974/989 Release /
  973/989 ASan, Node ref 989/989; `dom-conformance` 669/669; `url-conformance`
  1188/1188; `conformance` 1792/1792; `wpt` 7998/8009; `diff-trees` 6000/6000;
  `asan-deep` 882/882; fuzzers 0 findings).
- **Still open / boundaries:** (1) In JS, `s += '\uD83D\uDE00'; s =
  s.substring(0, s.length - 1); s += '\uDE00'` still keeps `refs == 2` across
  the JS variable slot and the operand temporary during `+=`, so JS still
  copies even though a single-owner C++ handle rewinds in 23 blocks. (2)
  Repeated `.view()` calls while a string still ends in an unpaired high half
  allocate a bounded `Utf8Slice` per call rather than pinning `\xED\xA0\xBD` in
  `Utf8Cache`; standard JS string operations never call `.view()`.

---
