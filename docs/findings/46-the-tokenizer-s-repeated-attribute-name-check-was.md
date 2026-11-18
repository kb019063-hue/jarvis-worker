# 46. ✅ The tokenizer's repeated-attribute-name check was quadratic

> Moved out of `AUDIT.md` — written there as a `## 46. ✅ The tokenizer's repeated-attribute-name check was quadratic` section.
> The text below is the section as it stood, unchanged.


**The hole.** `HtmlTokenizer::consume_tag` drops a repeated attribute name —
the standard's *duplicate-attribute* parse error, where the first attribute
with that name wins — by comparing the name with every attribute the token
has already accepted (`std::any_of` over `token.attributes` in the
`flush_attribute` lambda). Each attribute costs as much as the ones before
it, so a tag with n attributes costs n² comparisons. One element with 50,000
attributes took about 2.9 s in the HTML parser (10.4 s before Phase 28 indexed
the element's own attribute list); 25,000 took about 0.5 s. An end tag with
that many attributes paid the same and its attributes were then thrown away
by the tree builder. This was the tokenizer half of finding 28's cost row;
the XML parser already did the check with a set.

**The fix.** Only `src/html/HtmlTokenizer.cpp` changed. A tag that reaches 32
accepted attributes gets a `std::unordered_set<std::string_view>` of their
names; below that the scan stays, because comparing a handful of short
strings in a contiguous vector is cheaper than hashing into a table, and an
ordinary tag allocates nothing extra. The set holds views of the names inside
`token.attributes`, so no name is stored twice — which is only safe while
those strings stay put. Appending can reallocate the vector, and a short name
lives inside its `std::string` object and moves with it, so the index records
the vector's capacity and is rebuilt whenever it changes; between rebuilds no
accepted name moves. Capacity grows geometrically, so all the rebuilds
together are O(n) and the check is linear. Nothing else changed: names are
ASCII-lower-cased before the check, the first value wins, order is the order
of first occurrence, valueless/quoted/unquoted forms behave as before, `=`
may start a name, end-tag attributes are still tokenized, and EOF inside a
tag still drops the token.

**Measured.** Whole-process times (`--dump-dom-webplatform`: parse and tree
dump, no layout, no scripts), best of three, idle machine, same hour. The
baseline is this merged tree built with `main`'s tokenizer, kept as
`build/litebird-final-baseline`, so the only difference is this change:

```bash
python3 tools/bench/bench_tokenizer_attributes.py \
  --baseline build/litebird-final-baseline --reps 3 --max-ms 1000
```

| Case (50,000 attribute occurrences) | Baseline | Current | Speedup |
| :--- | ---: | ---: | ---: |
| distinct names | 3,474 ms | 93 ms | 37.2× |
| each name twice | 1,150 ms | 39 ms | 29.6× |
| uppercase repeats | 1,320 ms | 35 ms | 37.6× |
| long names, each twice | 3,810 ms | 71 ms | 53.5× |
| quoted, bare, unquoted | 772 ms | 27 ms | 28.2× |
| many small tags | 39 ms | 40 ms | 1.0× |
| end tag with attributes | 3,170 ms | 25 ms | 125.5× |

Every dump equals the tree the markup must produce — computed by the bench
from the markup, not recorded from a binary — and equals the baseline
binary's dump byte for byte. The square is gone: distinct names at
25,000/50,000/100,000/200,000 attributes take 45/93/183/344 ms across runs
(before: 628 ms at 25,000, 3,474 ms at 50,000). Tokenizing alone, 50,000 attributes cost 28 ms
in the release build and 102 ms under ASan/UBSan. `make
bench-tokenizer-attributes` is the checked-in guard: seven cases, three
repetitions, expected trees, 1,000 ms limit, run by CI (the scan cost 3.5 s).

**How it was verified.** `make test` passes 159 suites; the two new ones are
`tokenizer_repeated_attribute_names_keep_the_first` and
`tokenizer_duplicate_attribute_check_is_linear`. Both were checked against
mutants: with the old scan restored the correctness suite still passes and
the timing suite fails at `distinct_seconds < 2.0`; with the capacity rebuild
removed the correctness suite fails at the first size past a vector growth,
which is exactly the dangling-view bug the rebuild prevents. Out-of-corpus
pages through `tools/conformance/compare_with_jsdom.py` printed `16 lines, 16
the same`, `20 lines, 20 the same` and `6 lines, 6 the same` before and after
(duplicates through `innerHTML`/`DOMParser`/`insertAdjacentHTML`/SVG/table
cells, the sizes around the index threshold and every vector growth,
whitespace forms, form controls, and `=`-starting names). No expectation was
re-recorded and no floor was lowered.

The release run on the merged tree kept every scoreboard: html5lib
1781/1792, JS 907/921, DOM 640/640, URL 1188/1188, XML 1702/1702, WPT
4658/5192 (534 listed, 0 unlisted); diff-trees 6,000 pages and diff-xml 4,000
documents with 0 differing, XML round trip 3000/3000; deep 1,332 runs over
465 pages at depth 200,000 on an 8 MB stack, 0 failed (slowest 19.3 s);
bench-css 20 padded runs, bench-attributes 6 cases,
bench-tokenizer-attributes 7 cases, security-fetch 3 checks and
security-worker 17 checks, all with 0 failures; verify-media 35 checks and
fuzz-media 1,800 cases clean;
diff-strings at its 95-difference ceiling. Peak RSS for the 50,000-attribute
page was 16.1 MB in both binaries: the index is freed when the tag ends and
the dump reuses its memory.

The sanitizer run on the same tree reported nothing: ASan/UBSan 159/159
suites, JS 906/921 (one fewer than release: a deep-recursion case needs more
stack than a sanitizer build's frames leave, as before), DOM
640/640, URL 1188/1188, XML 1702/1702, WPT 4658/5192; ASan diff-xml 1,000
documents with 0 differing and 1,000/1,000 round trips; fuzz 400 parser
mutations, 400 pipeline runs and 600 XML mutations with 0 findings; ASan deep
882 runs over 315 pages at depth 10,000 on a 2 MB stack, 0 failed (slowest
2.7 s). `make tsan` passed (the string type under ThreadSanitizer, then under
ASan with leak checking).

**What is still open.** A cost fix in one function, not a parser conformance
change: the tokenizer's other states, the tree builder (the eleven html5lib
`<select>` cases) and the XML parser were not touched. Finding 28's layout
costs (a block inside an inline box, 10,000 sized floats) and finding 40's
middle erases remain, as do the `Attr`-is-not-a-`Node`,
one-qualified-name-in-two-namespaces and collection named-property rows.

---
