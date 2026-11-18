# Finding 74 — Retained Map and Set cursors made short iteration progressively slower

**Severity:** medium — correct results whose cost changed from small constant
work into quadratic work in a common `for...of` shape.

**Found by:** the Phase 63 iteration benchmark, recorded in finding 69. A
three-entry Map took roughly 12 microseconds per loop in its first thousand
loops and 154 in its eighth; a Set rose from roughly 8 to 129. `map.forEach`
and array iteration stayed flat, and `gc()` made the Map loop fast again.

## 1. What was wrong

A Map or Set iterator needs a movable entry position because deletion leaves a
tombstone and eventual compaction packs entries. `CollectionData` retained a
weak pointer to every shared `size_t` cursor so compaction could update its
position.

The engine does not collect automatically, so a finished JavaScript iterator's
native closure and cursor were still live until explicit `gc()`. From 64
records onward, `new_cursor()` scanned and copied the whole weak-pointer vector
for every next iterator, looking for expired entries that did not exist. The
cost of a completed loop thus grew with every previous completed loop.

## 2. What changed

Phase 68 replaces the bare cursor position with a cursor state that also knows
its registry slot. When an iterator first answers `done`, the generic iterator
wrapper latches it done and `CollectionData` swap-removes that cursor in O(1).
A normal completed Map/Set loop no longer leaves any registry record behind.

Live cursor behavior remains intact: compaction remaps both position and slot,
and `clear()` resets resumable positions while discarding expired records. A
partial iterator that is abandoned before completion is still weakly retained,
but cleanup runs only at geometrically growing registry sizes (minimum 64,
then doubling), making the aggregate scan work linear rather than quadratic.

## 3. How it was verified

```bash
make bench-collections
# 2 case(s); 0 failure(s). Every batch ran in one process without gc(),
# and each printed checksum 98304.

python3 tools/conformance/run_js_conformance.py --filter collections-iterators --reference 'node -e'
# JS conformance, full corpus: 34/34 (100.0%)  [1 files, 0 crashes, 0 timeouts]
```

The benchmark makes two equal 8,192-loop batches in one process and fails if
the later batch exceeds 2.25 times the first plus 40 ms. In the
least-contended run of three, the old Map went from 443 to 1,959 ms and the old
Set from 330 to 1,075 ms; the fixed Map was 58 to 58 ms and the fixed Set 36
to 35 ms.
The old binary fails the new guard without `gc()`; the fixed binary passes.

The two new corpus cases keep two Map or Set iterators live through
compaction, deletion/reinsertion order, `clear()` plus an insertion, and a
post-`done` insertion. They confirm the result against Node rather than
against the engine's old answer. The focused Litebird run is 32/34; its two
older failures are missing `Uint8Array` and `Proxy`, not either new case. An
ASan+UBSan build passed all 214 engine suites, this focused corpus, and the
two benchmark cases without a sanitizer report.

## 4. What is still open

The engine still has opt-in rather than automatic collection. An iterator that
is abandoned while partially consumed may keep a weak cursor record and its
JavaScript object until `gc()`; geometric pruning prevents that from degrading
subsequent cursor creation but does not reclaim all memory immediately.

`clear()` and tombstone compaction intentionally scan cursors that can still
resume, because their positions must be reset or remapped. Very many live
partial iterators therefore still make those mutation paths proportional to
their count. Constructor dispatch, `Symbol.species`, iterator-prototype, and
automatic-GC work remain separate from this fix.

See [Phase 68](../phases/68-map-set-iterator-performance.md) for the cursor
invariants and regression coverage.
