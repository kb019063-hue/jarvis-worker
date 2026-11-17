# Phase 39 Architecture: The Tokenizer's Repeated Attribute Names Are Linear

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 39 Architecture: The Tokenizer's Repeated Attribute Names Are Linear` section.
> The text below is the section as it stood, unchanged.


### 1. What was wrong

`HtmlTokenizer::consume_tag` drops a repeated attribute name — the standard's
*duplicate-attribute* parse error, where the first attribute with that name
wins — by comparing the name with every attribute the token has already
accepted (`std::any_of` over `token.attributes` in the `flush_attribute`
lambda). Every attribute of a tag therefore costs as much as the attributes
before it: the square of the count. Phase 28 indexed the element's own
attribute list, which took one element with 50,000 attributes from 10.4 s to
2.9 s in the parser; the tokenizer's own search was what was left. An end tag
paid it too, and its attributes are then thrown away by the tree builder. The
XML parser had already solved the same problem with a set (`XmlParser.cpp`,
the duplicate-name checks).

### 2. What changed

Only `src/html/HtmlTokenizer.cpp` changed. `consume_tag` keeps a
`std::unordered_set<std::string_view>` of the accepted names, built the first
time a tag reaches 32 accepted attributes. Below that the scan stays: for the
handful of attributes an ordinary element has, comparing a few short strings
in a contiguous vector costs less than hashing into a table, and no table is
allocated at all.

The set holds views of the names **inside** `token.attributes`, so no
attribute name is stored twice. That is only safe while those strings stay
put: appending to the vector can reallocate it, and a short name lives inside
its `std::string` object, so it moves with it. The index therefore records the
vector's capacity and is rebuilt whenever it changes; between rebuilds no
accepted name moves. Capacity grows geometrically, so the rebuilds together
cost O(n) over the tag, and the whole check is linear. Nothing else about the
state machine changed: names are ASCII-lower-cased before they reach the
check (which is what makes it the standard's case-insensitive comparison),
the first value wins, order is the order of first occurrence, valueless and
quoted and unquoted forms behave as before, `=` may still start a name, an
end tag's attributes are still tokenized, and EOF inside a tag still drops
the token.

### 3. Measured

Whole-process times (`--dump-dom-webplatform`: parse and tree dump, no layout,
no scripts), best of three, idle machine, same hour. The baseline is this
merged tree built with `main`'s tokenizer, kept as
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

Every dump equals the tree the markup must produce, computed by the bench
from the markup rather than recorded from a binary, and equals the baseline
binary's dump byte for byte. The square is gone: the distinct-names case at
25,000/50,000/100,000/200,000 attributes now takes tens to hundreds of
milliseconds (45/93/183/344 ms measured across runs), against 628 ms at
25,000 and 3,474 ms at 50,000 before: twice the input cost 5.5× before and
about 2× now. Tokenizing alone, 50,000 attributes cost 28 ms in the release
build and 102 ms under ASan/UBSan. The checked-in guard is
`make bench-tokenizer-attributes`: seven cases, three repetitions, expected
trees, 1,000 ms whole-process limit — far
below the 3.5 s the scan costs, and far above what a shared runner needs.

### 4. Verification

`make test` passes 159 suites. The two new suites are
`tokenizer_repeated_attribute_names_keep_the_first` (the fixed shapes:
first-wins, ASCII-only folding, `Ä`/`ä` distinct, `=`-starting names and the
after-attribute-name `=`, entities in values, `/` in a tag, end tags, EOF
dropping the token; every size around the index threshold and around each
vector growth from 1 to 257; 60 random tags against a set-based model; the
same shapes through the parser and the DOM) and
`tokenizer_duplicate_attribute_check_is_linear` (50,000 distinct attributes,
50,000 occurrences of 25,000 long names in both cases, an end tag with 50,000
attributes, and 5,000 ordinary four-attribute tags, each with a 2 s wall-clock
guard). Both were checked against mutants: with the old scan restored the
correctness suite passes and the timing suite fails at `distinct_seconds <
2.0`; with the capacity rebuild removed the correctness suite fails on the
first size past a vector growth, which is the dangling-view bug the rebuild
exists to prevent.

Out-of-corpus pages run through `tools/conformance/compare_with_jsdom.py`
before and after printed the same counts: `16 lines, 16 the same` (duplicates
through `innerHTML`, `DOMParser`, `insertAdjacentHTML`, SVG foreign
attributes, table cells, a 300-attribute element), `20 lines, 20 the same`
(the sizes around the threshold and every growth, whitespace forms, form
controls) and `6 lines, 6 the same` (`=`-starting names). No corpus
expectation was re-recorded.

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

### 5. What is still open

This is a cost fix in one function, not a parser conformance change: the
tokenizer's other states, the tree builder (including the eleven remaining
html5lib `<select>` cases), and the XML parser were not touched. Finding 28's
other costs — a block inside an inline box, 10,000 sized floats in one box —
and finding 40's middle erases are still open, as are the
`Attr`-is-not-a-`Node`, one-qualified-name-in-two-namespaces and collection
named-property rows. (Since then: the finding-28 layout costs were both
fixed, the block inside an inline box in Phase 36 and the sized floats by the
band-edge sweep; finding 40's middle erases remain.)

---
