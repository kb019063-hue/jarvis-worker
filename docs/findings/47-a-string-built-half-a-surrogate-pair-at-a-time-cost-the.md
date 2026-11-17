# 47. ✅ A string built half a surrogate pair at a time cost the square of its length

> Moved out of `AUDIT.md` — written there as a `## 47. ✅ A string built half a surrogate pair at a time cost the square of its length` section.
> The text below is the section as it stood, unchanged.


**The hole.** `JsString` stores text as UTF-8 in blocks that never change once
written, and `a + b` writes `b` into the room after `a` when `a` ends where the
block's text ends. That is what makes `s += x` in a loop cost what it writes
(finding 25). One join could not use it: when `a` ends in a high surrogate half
and `b` starts with the low half, the two are one character, four bytes, and
those four bytes start *three bytes before the end of `a`*. Writing them would
change bytes another handle is reading, so `merge_halves` copied the whole of
`a` and `b` into a new block — and the new block had no room to grow and no
history of moves, so the next append copied too. Building a string one half at
a time (`s += '\uD83D'; s += '\uDE00'`) therefore copied the whole text at every
pair. Whole-process times on the verification machine, release build:

| Pairs appended | Half at a time, before | Half at a time, after | Speedup | A pair at a time, before and after |
| ---: | ---: | ---: | ---: | ---: |
| 5,000 | 325 ms | 7 ms | 43.5× | — |
| 10,000 | 1,390 ms | 11 ms | 127.1× | — |
| 20,000 | 5,583 ms | 18 ms | 308.0× | 11 ms |
| 40,000 | 23,252 ms | 30 ms | 782.1× | 15 ms |

Best of three, idle machine, `--run-js`, whole process. The baseline is the
branch's base commit (`ce37948`), which is `main`'s `JsString`, kept as
`build/litebird-surrogate-baseline`; these scripts run no layout, so the only
difference on their path is this change. Four times the time for twice the input
before; in step with the input after, and the same as a pair at a time. One case
of the corpus, `pairs made a half at a time in a long string`
(`tests/js-conformance/long-strings.dat`), does this 60,000 times: it took
5.545 s of the 10 s a case is allowed, so it timed out on a busy machine. That
is why the JavaScript floor in CI sat at 906 while the corpus scored 907. It
now takes 0.035 s and prints the same line.

**The fix.** Only `src/js/JsString.cpp` and `include/litebird/js/JsString.hpp`
changed. A join no longer writes a high half that ends its result. The handle
owes it: a fifth 32-bit field, `m_pending`, holds the half, `m_units` counts it
and `m_bytes` does not. When the low half arrives, the character is written
where the block's text ends — an ordinary append, nothing copied — and when
anything else arrives the half is written first, which is also an append. A
handle that owes a half is settled the first time its text is read (`view()`,
`wide()`, `str()`, a piece): the three bytes go into the room after the block's
text if it is still free, and otherwise the text is copied once into a block
that the handle's own block keeps alive, the same arrangement as the cached
UTF-16 form. The claim on that room is the same compare-and-swap every append
makes, so two threads settling at once cannot both win; the loser copies. A
settled handle is an ordinary range of a block, and nothing about it is new.

The handle grew from 24 bytes to 32 (one word of padding for the fifth field)
and `JsValue` from 88 to 96. That has a small cost, and it is measured rather
than assumed: across the 39 string benchmarks that were already there, best of
three on an idle machine, 2,521 ms before and 2,640 ms after (1.05×), median
case 1.04×, mean 1.03×, nine of 39 more than 15% slower. The worst is `read:
for-of, 200k non-ASCII` at 42 → 64 ms and `read: charCodeAt, 200k ASCII` at
97 → 133 ms; the best are `build: obj.s += x` and `build: append, then read the
end` at 0.60×. An earlier run of the same two binaries came out the other way
(2,661 → 2,452 ms, median 0.92×), so on this two-core machine the difference is
close to the run-to-run spread; the slowest case is still a fifth of the three
seconds `make bench` allows.

**Measured.** `make bench-surrogates` is the checked-in guard: the two sizes a
doubling apart plus the keep-each-prefix shape, best of three, results compared
with Node.js, limit one second.

```bash
make bench-surrogates
```

| Case | Before (`main`) | After | Node.js |
| :--- | ---: | ---: | ---: |
| half a pair at a time, 20,000 | 5,546 ms | 17 ms | 36 ms |
| half a pair at a time, 40,000 | over 8 s | 28 ms | 44 ms |
| half a pair, keeping each prefix, 20,000 | 3,856 ms | 14 ms | 40 ms |

The guard fails on all three cases against `main`'s binary and passes on this
one. Counting work rather than timing it (`JsString::stats`), 20,000 pairs built
half at a time copy 399,370 bytes into 152 blocks for 100,000 bytes of text;
20,000 pairs built a pair at a time copy 399,220 bytes into 22. The old path
copied the text so far at each of the 20,000 pairs, about a thousand million
bytes. The corpus case itself went from 5.545 s to 0.035 s, printing the same
line.

**How it was verified.** `make test` passes 162 suites; the two new ones are
`js_string_owes_a_trailing_high_half_until_it_is_read` and
`js_string_built_half_a_pair_at_a_time_costs_what_it_writes`. Both were checked
against a mutant: with the deferral turned off (`ends_in_high_half` always
false, which is the old behaviour) the first fails at "no block and no byte is
written when a half is owed" and the second at `bytes_copied < 2000000`. The
first pins what script can see: nothing is written when a half is owed, the
text reads as it always did, a handle kept before the pair was completed still
ends in a lone half, settling copies three bytes once and never again, a settle
whose room was taken copies once and outlives the handle that took it, and
every way round (two highs, a low then a high, raw text on either side, a join
of several units) gives the same bytes. The random UTF-16 model test
(`tests/js_string_stress.hpp`, also run by `make tsan`) already generates lone
halves in its alphabet and concatenates, slices and appends with reads in
between; it passes unchanged.

Out-of-corpus: 40 surrogate-heavy programs (kept prefixes, slicing a pending
string, splitting a pair, `for...of`, `indexOf` across the boundary, JSON,
property keys, `repeat`, `replace`, template literals, regular expressions,
numbers on either side) run in Node.js and in both binaries: 0 mismatches
against Node, and the two binaries print the same for all 40. Two pages' worth
of DOM text built the same way — `textContent`, attributes, comments,
`splitText`, `normalize`, `cloneNode`, `insertAdjacentHTML`, `innerHTML` round
trips — through `tools/conformance/compare_with_jsdom.py` printed `11 lines, 11
the same` and `12 lines, 12 the same`, on both binaries.

The new settle path is shared state, so it was also run on its own under the
sanitizers: 200 rounds of seven threads settling copies of one handle that owes
a half, reading one handle from several threads at once, taking pieces and
appending, while another thread appends to the same block so the room is
sometimes free and sometimes taken. `0 failure(s)` under ThreadSanitizer and
under ASan/UBSan with leak checking on — no race, no leak, and every answer the
text it was given.

The release run on the merged tree (`main` at `e007fa2`, which brought PR #51's
float placement) kept every scoreboard: 162 suites; html5lib
1781/1792 (57 files); JS 907/921 with 0 crashes and 0 timeouts; DOM 640/640;
URL 1188/1188; XML 1702/1702; WPT 4658/5192 (534 listed, 0 unlisted);
diff-trees 6,000 pages and diff-xml 4,000 documents with 0 differing, XML round trip 3000/3000;
diff-strings at its 95-difference ceiling; deep 1,332 runs over 465 pages at
depth 200,000 on an 8 MB stack, 0 failed (slowest 23.4 s); bench 42 cases,
bench-surrogates 3, bench-css 20, bench-attributes 6,
bench-tokenizer-attributes 7, security-fetch 3 and security-worker 17, all
with 0 failures; verify-media 35 checks. The sanitizer run: 162 suites,
JS 906/921 (one fewer, a deep-recursion case that needs more stack than a
sanitizer build's frames leave), XML 1702/1702, WPT 4658/5192, 1,000 generated
XML documents with 0 differing and 1,000 round trips, fuzz 400 / pipeline 400 /
XML 600 mutations with 0 findings, 1,800 media fuzz cases clean, and 882 deep
runs over 315 pages at depth 10,000 on a 2 MB stack, 0 failed (slowest 3.6 s).
`make tsan` passed both the model and the thread test, under ThreadSanitizer
and under ASan with leak checking.

**What this does not change.** A high half that is *already in a block* — from
a literal, from `String.fromCharCode`, from a piece — still copies when the low
half arrives, because the four bytes start inside text another handle may be
reading. And a handle that owes a half and is *read* before the low half
arrives has its half written into the block, so completing the pair then
copies: `s += '\uD83D'; n += s.indexOf('zz'); s += '\uDE00'` in a loop is still
quadratic (58/242/893 ms at 5,000/10,000/20,000). Neither shape is in the
corpus; both are rows of the task table now. No expectation was re-recorded, no
`everyday-*.dat` file was touched, and no floor was lowered: the JavaScript
floor in CI went **up**, from 906 to the score of 907, which is what the comment
above it said to do when this row was fixed.

---
