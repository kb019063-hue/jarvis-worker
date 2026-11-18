# 40. ✅ Attribute-list front removal was quadratic

> Moved out of `AUDIT.md` — written there as a `## 40. ✅ Attribute-list front removal was quadratic` section.
> The text below is the section as it stood, unchanged.


**The hole.** `AttributeList` kept the attributes in a `std::vector` and,
once a list was long enough to index, kept a parallel vector of the numbers
each name was given. The number index avoided rebuilding positions on every
erase, but `erase(iterator)` still erased from both vectors at the same
offset. Removing the first attribute moved every remaining string pair and
every remaining number: draining 50,000 attributes from the front was
quadratic. The recorded script loop took about 6 s (9 s in the older
measurement); the same removals from the back were tens of milliseconds.

**The fix.** Only `include/litebird/dom/AttributeList.hpp` changed. The
items and `Index::numbers` remain vectors, but each has a logical start
offset. A back erase pops the back. A front erase advances the start and
leaves a dead prefix; `compact()` drops that prefix only when it is at least
as large as the live tail, so repeated front erases are amortized O(1), a
full drain is linear, and an empty list still allocates nothing. A
`std::deque` was tried first and removed: libstdc++ allocates for an empty
deque, which made every element pay even with no attributes and failed the
deep-page memory limits. The public iterators are still random-access vector
iterators (`attributes.begin() + index`, `found - list.begin()`). The lazy
index still scans below 32 entries, then maps names to numbers and bisects
the live ascending number range. Copy copies only the live range and drops
the index; move resets the source start so a moved-from list is empty;
`clear`, value update in place, insertion order, `operator[]`,
`find/count/erase` and order-insensitive equality keep their semantics.
`AttributeNamespaces` was not changed.

**Measured.** Whole-process times, best of three, same machine and hour;
baseline is the pre-change binary:

```bash
python3 tools/bench/bench_attributes.py \
  --baseline build/litebird-attributes-baseline --reps 3 --max-ms 3000
make bench-attributes
```

| Case (50,000 attributes unless noted) | Baseline | Current | Speedup |
| :--- | ---: | ---: | ---: |
| front: remove a0..aN | 7,132 ms | 90 ms | 79.0× |
| back: remove aN..a0 | 115 ms | 114 ms | 1.0× |
| ends: alternate front/back | 3,481 ms | 113 ms | 30.8× |
| front half, then update/reinsert | 5,293 ms | 69 ms | 76.8× |
| namespaces: front removal | 8,465 ms | 197 ms | 43.0× |
| short lists: churn below index | 79 ms | 73 ms | 1.1× |

Every case prints the same expected DOM checksum in both binaries. A
script-only `Date.now()` probe measured the front removal loop at 6,078 ms
before and 51 ms after; the back loop stayed in the tens of milliseconds.
The checked-in guard is `make bench-attributes`: six cases, three
repetitions, expected checksums, 3,000 ms whole-process limit.

**How verified.** `make test` passes 135 suites, including
`attribute_list_end_removals_keep_order_and_index` and
`attribute_list_front_removal_is_linear_in_practice`. `make bench-attributes`
passes 6 cases with 0 failures. The release run kept html5lib 1781/1792, JS
840/845, DOM 629/629, URL 1188/1188, XML 1702/1702, WPT 4640/5192;
diff-trees/diff-xml have 0 differing; deep has 1332 runs/0 failed (slowest
25.0 s); bench-css has 20 padded runs/0 failures; security-fetch has 3
checks/0 failures; diff-strings is at the 95-difference ceiling. A generated
out-of-corpus probe run through `tools/conformance/compare_with_jsdom.py`
printed `4 lines, 4 the same` before and after. `make tsan` passed (JsString model/thread under ThreadSanitizer and under ASan+UBSan). `make asan` reported no sanitizer finding: 135/135 suites, JS 839/845, DOM 629/629, URL 1188/1188, XML 1702/1702, WPT 4640/5192; ASan diff-xml 1000 documents/0 differ and round trip 1000/1000; fuzz 400 parser mutations, 400 pipeline runs and 600 XML mutations with 0 findings; ASan deep 882 runs/0 failed (slowest 4.3 s).

**What is still open.** This fixes end removals, not arbitrary middle
removals: a middle erase still moves the vector tail. The HTML tokenizer's
repeated-name search for one element with 50,000 attributes was fixed in
Phase 39 (finding 46). The `Attr`-is-not-a-`Node`, one-qualified-name-in-two-
namespaces and collection named-property rows remain open. No parser, layout,
CSS or JavaScript engine file was changed.

---
