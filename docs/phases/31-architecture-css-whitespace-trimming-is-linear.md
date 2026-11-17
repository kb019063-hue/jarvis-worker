# Phase 31 Architecture: CSS Whitespace Trimming Is Linear

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 31 Architecture: CSS Whitespace Trimming Is Linear` section.
> The text below is the section as it stood, unchanged.


### 1. What was wrong

Finding 28 measured 300,000 spaces inside `:nth-child( )` at about nine
seconds. The cause was not the HTML parser or layout: several CSS trim sites
did this:

```cpp
while (!text.empty() && std::isspace(text.front())) text.erase(text.begin());
```

Every erase shifted the whole remaining string. A run of n spaces before a
value therefore cost O(n²) before the value was read. The nth matcher also
took its argument by value and lowercased the padding before trimming it,
once for every child it tested.

### 2. What changed

`include/litebird/css/CssText.hpp` provides `css_trim_view` and `css_trim`.
The view scans the two outer runs without copying. The owned-string version
erases the leading run once and resizes the tail once. The attribute-selector
quote stripper uses the same bounded-scan shape with its existing quote set.
All front-erasure loops in `src/css/CssParser.cpp` and
`src/css/StyleResolver.cpp` were replaced; `grep -n 'erase(.*begin())'
src/css/*.cpp` now finds none.

The nth matcher takes a view, trims before allocating/lowercasing, and
shorthand/duration trimming stays in views until a token is stored. The
whitespace set is deliberately unchanged: `std::isspace` in the process
locale, with an unsigned-char cast. The engine does not call `setlocale`;
in its C locale this includes VT. Changing that to the CSS whitespace
production would be a tokenizer change, not this cost fix.

### 3. Measured through the page pipeline

`tools/bench/bench_css_whitespace.py` writes a compact page and the same page
with spaces in parser/resolver arguments, runs `--dump-layout --dump-paint`,
and compares the bytes. With `--baseline` it also compares the old binary's
dump and reports its time. Times below are whole-process milliseconds, best
of three for the new binary and one run for the baseline, on the same
two-core verification machine; they are not microbenchmarks of trim alone.

| 300,000 spaces per placeholder | Before | After |
| :--- | ---: | ---: |
| `:nth-child` formula | 14,627 | 111 |
| attribute name and quoted value | 4,687 | 90 |
| attribute presence | 2,331 | 58 |
| keyframe selector | 47 | 55 |
| grid `minmax(...)` | 9,786 | 199 |
| grid `repeat(...)` | 22,545 | 188 |
| filter name/argument/colour | 7,191 | 156 |
| shorthand tokens | 181 | 171 |
| durations and `grid-area` | 5,791 | 283 |
| transform functions/arguments | 11,694 | 304 |

The keyframe and shorthand cases were already cheap in this pipeline because
other whitespace consumption happens before the trim; they are kept as
behaviour guards, not claimed as speedups.

```bash
make bench-css
# or reproduce the table against a saved pre-change binary:
python3 tools/bench/bench_css_whitespace.py \
  --sizes 30000,300000 --reps 3 --max-ms 3000 --baseline build/litebird-before
```

The checked-in CI guard uses 30,000 and 300,000 spaces, three repetitions,
compact/padded output equality and a generous 3,000 ms per-run limit. It
fails on a crash, timeout, sanitizer report, different dump or slow case.

### 4. Verification and limits

Three appended unit suites cover the helper against the old character model
(including empty input, embedded NUL, high bytes and VT), parsed selector
fields and resolved values across every changed path, and 300,000-space
inputs. `make test` passes 117 suites. The conformance corpora are unchanged
(HTML 1781/1792, JS 840/845, DOM 572/572, URL 1150/1188). `make diff-trees`
has 0 differing pages, `make deep` 0 failures in 1,292 runs, `make fuzz`
0 findings in 800 runs, and `make asan` no sanitizer finding (ASan JS
839/845, ASan deep 842 runs, 0 failed). `make tsan`, `make bench` and
`make diff-strings` also pass.
This phase does not make the CSS parser spec-complete, does not change CSS
whitespace recognition, and does not address the separate layout, tokenizer
attribute-count or style-object tasks. The `:nth-child(2n + 1)` sign-space
parsing bug found while building the benchmark is fixed (AUDIT.md finding 39:
whitespace tokens are ignored inside the `<an+b>` micro-syntax and the offset
and coefficient are strictly signed integers, so an invalid formula matches
nothing); the benchmark kept `+1` glued while the bug was open, so its
timings never depended on the fix.

---
