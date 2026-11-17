# 35. ✅ CSS whitespace trimming was quadratic

> Moved out of `AUDIT.md` — written there as a `## 35. ✅ CSS whitespace trimming was quadratic` section.
> The text below is the section as it stood, unchanged.


**The hole.** The CSS parser and resolver trimmed leading whitespace with
`erase(begin())` in a loop. Each space shifted the entire remaining value.
Finding 28 measured 300,000 spaces inside `:nth-child( )` at 8.8 s; the
reproducible page-pipeline baseline for this phase measured 14.6 s on its
machine. The same shape appeared in attribute selectors, grid track
arguments, filters, shorthands, durations, grid areas and transforms.

**The fix.** `CssText.hpp` scans the outer runs once and edits an owned
string at most once at the front. Callers that only need a token keep a
string view. The nth matcher trims before copying/lowercasing and no longer
copies the padding for every child. The existing whitespace and quote sets
are preserved; this is not a CSS tokenizer conformance change.

**Measured.** `python3 tools/bench/bench_css_whitespace.py --sizes
30000,300000 --reps 3 --max-ms 3000 --baseline BUILD` compares every padded
page with its compact form and with the baseline binary. At 300,000 spaces:
nth-child 14,627 → 111 ms; attribute value 4,687 → 90 ms; grid minmax
9,786 → 199 ms; grid repeat 22,545 → 188 ms; filters 7,191 → 156 ms;
durations/grid-area 5,791 → 283 ms; transforms 11,694 → 304 ms. The full
table and protocol are in ARCHITECTURE.md, Phase 31. Keyframe and shorthand
cases were already cheap in this pipeline and stayed cheap.

**What is still approximate.** The whitespace set is still the C locale's
`std::isspace`, not the CSS whitespace production; VT is still trimmed.
The separate `:nth-child(2n + 1)` sign-space parsing bug that this phase
surfaced **is** fixed now — see finding 39. The finding-28 float,
computed-style memory and 50,000-attribute tokenizer costs are also not
fixed here and remain open tasks.

**How verified.** `make test` passes 117 suites, including the helper model,
selector/value equivalence and long-run suites. `make bench-css` passes 20
padded runs with identical compact/paged layout and paint. The scoreboards are
unchanged: HTML 1781/1792, JS 840/845, DOM 572/572, URL 1150/1188.
`make diff-trees` has 0 differing pages; `make deep` has 0 failures in 1,292
runs; `make fuzz` has 0 findings in 800 runs; `make asan` reports no sanitizer
finding (ASan JS 839/845, ASan deep 842 runs, 0 failed); `make tsan`,
`make bench` and `make diff-strings` pass.

---
