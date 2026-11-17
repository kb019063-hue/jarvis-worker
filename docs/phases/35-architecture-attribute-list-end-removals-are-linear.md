# Phase 35 Architecture: Attribute-List End Removals Are Linear

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 35 Architecture: Attribute-List End Removals Are Linear` section.
> The text below is the section as it stood, unchanged.


### 1. What was wrong

`AttributeList` kept the attributes in a `std::vector` and, once a list was
long enough to index, kept a parallel `std::vector<std::size_t>` of the
numbers each name was given. The number index avoided rebuilding positions
on every erase, but `erase(iterator)` still erased from both vectors at the
same offset. Removing the first attribute moved every remaining string pair
and every remaining number: draining 50,000 attributes from the front was
quadratic. The recorded script loop took about 6 s (9 s in the older
measurement); the same removals from the back were tens of milliseconds.

### 2. What changed

Only `include/litebird/dom/AttributeList.hpp` changed. The items and
`Index::numbers` remain `std::vector`s, but each has a logical start offset.
The public iterator aliases are still vector iterators, so
`attributes.begin() + index` in the `NamedNodeMap` bindings and
`found - list.begin()` in callers/tests still work. The lazy index is
unchanged in shape: lists shorter than 32 are scanned; longer lists build a
name-to-number map and an ascending number sequence, and `position()`
bisects the live number range. A back erase pops the vector; a front erase
advances the start and leaves a dead prefix. `compact()` drops that prefix
only when it is at least as large as the live tail, which makes repeated
front erases amortized O(1) and a full drain linear, while an empty list
still allocates nothing. A middle erase removes the name from the map,
erases the parallel number, and moves the vector tail. Copy copies only the
live range and drops the index; move resets the source start so a moved-from
list is empty; `clear`, value update in place, insertion order,
`operator[]`, `find/count/erase` and order-insensitive equality keep their
semantics. `AttributeNamespaces` is a separate side table and was not
changed.

### 3. Measured

Whole-process times, best of three, same machine and hour; baseline is the
pre-change binary saved as `build/litebird-attributes-baseline`:

```bash
python3 tools/bench/bench_attributes.py \
  --baseline build/litebird-attributes-baseline --reps 3 --max-ms 3000
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
repetitions, expected checksums, 3,000 ms whole-process limit. It fails on a
crash, timeout, wrong checksum or slow case.

### 4. Verification

`make test` passes 135 suites. The two new suites are
`attribute_list_end_removals_keep_order_and_index` (model comparison across
the scan/index boundary, both erase overloads, reinsert/update, copy/move,
moved-from emptiness, late index build after a dead prefix, clear and
random-access iterators) and
`attribute_list_front_removal_is_linear_in_practice` (50,000 direct and
script front removals with generous wall-clock guards). `make
bench-attributes` passes 6 cases with 0 failures. A generated out-of-corpus
probe run through `tools/conformance/compare_with_jsdom.py` printed `4 lines,
4 the same` before and after. The merged release run kept html5lib 1781/1792,
JS 840/845, DOM 629/629, URL 1188/1188, XML 1702/1702, WPT 4640/5192,
diff-trees/diff-xml 0 differing, deep 1332 runs/0 failed (slowest 25.0 s),
bench-css 20 padded runs/0 failures, security-fetch 3 checks/0 failures, and
diff-strings at the 95-difference ceiling. The sanitizer scoreboards are
recorded in AUDIT.md, finding 40.

### 5. What is still open

This fixes end removals, not arbitrary middle removals: a middle erase still
moves the vector tail. The HTML tokenizer's repeated-name search for one
element with 50,000 attributes was fixed in Phase 39 (finding 46). The
`Attr`-is-not-a-`Node`, one-qualified-name-in-two-namespaces and collection
named-property rows remain open. No parser, layout, CSS or JavaScript engine file was
changed.

---
