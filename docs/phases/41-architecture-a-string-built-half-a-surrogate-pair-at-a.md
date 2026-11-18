# Phase 41 Architecture: A String Built Half a Surrogate Pair at a Time Is Linear

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 41 Architecture: A String Built Half a Surrogate Pair at a Time Is Linear` section.
> The text below is the section as it stood, unchanged.


### 1. What was wrong

`JsString` is a handle on a range of a block of UTF-8 bytes that never change
once written (Phase 23, AUDIT.md finding 25). `a + b` writes `b` into the room
after `a` when `a` ends where the block's text ends, and blocks that keep being
extended are given room to grow by half, so `s += x` in a loop costs what it
writes. One join could not do that. When `a` ends in a high surrogate half and
`b` starts with the low half, the two halves are one character: four bytes that
start three bytes *before* the end of `a`. Writing them would change bytes that
another handle — `var kept = s` before the pair was completed — is still
reading, so `merge_halves` copied the whole of both texts into a new block.
The new block had no spare room and no history of moves, so the append after it
copied as well. A string built one half at a time copied itself at every pair:
20,000 pairs took 5.6 s and 40,000 took 21 s, where a pair at a time took 9 and
15 ms. The corpus case that does this 60,000 times took 5.68 s of the 10 s a
case is allowed, so it timed out on a busy machine, and the JavaScript floor in
CI had to sit one below the score for that case alone.

### 2. What changed

Only `src/js/JsString.cpp` and `include/litebird/js/JsString.hpp`. A join does
not write a high half that ends its result; the handle **owes** it.

- The handle carries a fifth 32-bit field, `m_pending`: 0, or the high half
  owed at the end. `m_units` counts the half, `m_bytes` does not, and `size()`
  reports the bytes the text has, half included. `is_ascii()` is false for a
  handle that owes one, and `is_well_formed()` is false without scanning.
- `concat_settled` appends everything but a trailing high half and owes that
  half. `concat_owed` decides what becomes of a half the left side owes: if the
  text arriving starts with a low half, the character they spell is appended
  where the block's text ends (four bytes, two code units, nothing copied);
  otherwise the half is written first, which is an ordinary append too. A join
  of two handles passes the right side's bytes without its owed half and owes
  that half itself, so nothing is written to be joined.
- Anything that reads the text settles the handle first: `view()`, `wide()`,
  `str()`, `substring()`. `unit_at()` does not need to: the half is the last
  unit, and everything before it is in the block. Settling writes the three
  bytes into the room after the block's text when that room is still free,
  claimed by the same compare-and-swap an append uses, and otherwise copies the
  text once into a block of its own. That copy hangs off a list on the handle's
  block, which holds one claim on it, so it lives as long as any handle that
  can still ask for it — the same arrangement as the cached UTF-16 form. The
  list only grows and each node is whole before it is linked, so readers walk
  it without a lock. A settled handle is an ordinary range of a block.
- `merge_halves` stays for a high half that is already in a block (a literal,
  `String.fromCharCode`, a piece): those bytes may be read by another handle,
  so that join still copies.

The handle is 32 bytes where it was 24 (a pointer and five 32-bit fields, one
word of padding), and `JsValue` is 96 where it was 88. The unit test that pins
the size was updated with the reason.

### 3. Measured

Whole-process times, release build, idle machine, `--run-js`. The baseline is
the branch's base commit (`ce37948`), which is `main`'s `JsString`, kept as
`build/litebird-surrogate-baseline`; these scripts run no layout, so the only
difference on their path is this change.

| Pairs appended | Half at a time, before | Half at a time, after | Speedup | A pair at a time, both |
| ---: | ---: | ---: | ---: | ---: |
| 5,000 | 325 ms | 7 ms | 43.5× | — |
| 10,000 | 1,390 ms | 11 ms | 127.1× | — |
| 20,000 | 5,583 ms | 18 ms | 308.0× | 11 ms |
| 40,000 | 23,252 ms | 30 ms | 782.1× | 15 ms |

`make bench-surrogates` is the checked-in guard (three cases, best of three,
results compared with Node.js, one-second limit): 17 ms, 28 ms and 14 ms,
against 5,546 ms, over 8 s and 3,856 ms for `main`'s binary, which fails all
three. Counting work instead of timing it, 20,000 pairs built half at a time
copy 399,370 bytes into 152 blocks for 100,000 bytes of text; a pair at a time
copies 399,220 bytes into 22 blocks. The corpus case went from 5.545 s to
0.035 s, printing the same line.

The wider handle is not free, and the cost is measured: across the 39 string
benchmarks that were already there, 2,521 ms before and 2,640 ms after (1.05×),
median case 1.04×, worst 1.52× (`read: for-of, 200k non-ASCII`, 42 → 64 ms),
best 0.60×. An earlier run of the same two binaries came out at 2,661 → 2,452 ms
(median 0.92×), so the difference is close to this machine's run-to-run spread.
Repro: `python3 tools/bench/bench_strings.py --binary … --reference '' --reps 3`.

### 4. Verification

`make test` passes 162 suites. The two new ones are
`js_string_owes_a_trailing_high_half_until_it_is_read` (what script can see of
the owed half: nothing is written when it is owed, the text reads as it always
did, a handle kept before the completion still ends in a lone half, settling
copies three bytes once and never again, a settle whose room was taken copies
once and outlives the handle that took it, and every way round gives the same
bytes) and `js_string_built_half_a_pair_at_a_time_costs_what_it_writes` (the
work counted, and 6,000 kept prefixes each ending in the half they were given).
With the deferral turned off — `ends_in_high_half` always false, which is the
old behaviour — the first fails at once and the second fails at
`bytes_copied < 2000000`. The random UTF-16 model test in
`tests/js_string_stress.hpp`, which `make tsan` also runs, generates lone
halves in its alphabet and concatenates, slices and appends with reads in
between; it passes unchanged.

The JavaScript corpus scores 907/921 with 0 timeouts, `long-strings.dat`
34/34. The floor in CI rose to that score, 907, at that phase (before Phase 45), as the comment above it said to
do when this row was fixed. Out-of-corpus, 40 surrogate-heavy programs run in
Node.js and in both binaries print the same: 0 mismatches; and two pages' worth
of DOM text built half a pair at a time through
`tools/conformance/compare_with_jsdom.py` print `11 lines, 11 the same` and
`12 lines, 12 the same` on both binaries.

The settle path is shared state, so it was run on its own under the sanitizers
too: 200 rounds of seven threads settling copies of one handle that owes a
half, reading one handle from several threads at once and taking pieces, while
another thread appends to the same block so the room is sometimes free and
sometimes taken. No race, no leak, every answer right, under ThreadSanitizer
and under ASan/UBSan with leak checking on.

The full chain on the merged tree (`main` at `e007fa2`, PR #51's float
placement included): release — 162 suites, html5lib 1781/1792, JS
907/921, DOM 640/640, URL 1188/1188, XML 1702/1702, WPT 4658/5192 (534 listed,
0 unlisted), diff-trees and diff-xml 0 differing, diff-strings at its ceiling,
deep 1,332 runs over 465 pages at depth 200,000 (0 failed, slowest 23.4 s),
bench 42 / bench-surrogates 3 / bench-css 20 / bench-attributes 6 /
bench-tokenizer-attributes 7 / security-fetch 3 / security-worker 17 with 0
failures, verify-media 35 checks. Sanitizers — 162 suites, JS 906/921 (one
fewer: deep recursion), XML 1702/1702, WPT 4658/5192, 1,000 generated XML
documents and 1,000 round trips, fuzz 400 / pipeline 400 / XML 600 mutations
with 0 findings, 1,800 media fuzz cases clean, deep 882 runs over 315 pages at
depth 10,000 (0 failed, slowest 3.6 s), and `make tsan` under both sanitizers.

### 5. What is still open

- A high half that is already in a block still copies when the low half
  arrives: a piece taken with `substring` that ends in one, or a literal, is
  settled text, and the four bytes of the pair start inside it. A join owes the
  half instead, which is why `s += String.fromCharCode(0xD83D)` in a loop is
  linear all the same: the half is the last thing in the text joined, so the
  join owes it rather than writing it.
- Reading a handle that owes a half writes the half into its block, so
  completing the pair afterwards copies: `s += '\uD83D'; n += s.indexOf('zz');
  s += '\uDE00'` in a loop is still quadratic (58/242/893 ms at
  5,000/10,000/20,000). Making that linear needs text that can be read without
  being flattened — a rope, or UTF-16 storage — which is a change of
  representation, not of this path. It is a row of the task table.
- The handle is a word wider. Packing the owed half into the spare high bits of
  the four length and offset fields would keep it at 24 bytes, at the cost of
  masking every read of them; that was not done, and the measured cost of not
  doing it is in section 3.

---
