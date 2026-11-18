# Phase 68 — Map and Set iterator cursors stay flat

## 1. What was wrong

`CollectionData` stores Map and Set entries outside the JavaScript property
table so it can preserve insertion order and leave a tombstone behind while an
iterator might still point into the entry vector. Its old cursor registry was
therefore a `std::vector<std::weak_ptr<size_t>>`: every iterator put its
position in a shared `size_t`, and deletion/compaction could remap every live
position.

That representation had an accidental second lifetime. A finished iterator
object is not necessarily collected when the loop leaves scope: this engine
collects only when a script asks it to. Its weak cursor therefore stayed
non-expired even though no script could reach the iterator. Once the registry
contained 64 records, `new_cursor()` rebuilt it to discard expired pointers on
**every new iterator**. With no `gc()`, there were no expired pointers to
discard, so every short completed `for...of` loop copied and inspected every
cursor made by the earlier loops.

The result was quadratic creation work for a fixed, three-item collection. The
Phase 63 probe recorded a Map growing from about 12 to 154 microseconds per
loop across eight batches of 1,000, and a Set from about 8 to 129. Calling
`gc()` lowered the Map result again, which identified cursor retention rather
than the entries or the loop body as the cost.

The tempting fix — drop all cursors on each loop — would be wrong. A live
iterator must survive all of these operations:

- deleting enough early entries to compact tombstones;
- deleting and reinserting an unseen key, which puts its replacement at the
  end of insertion order;
- `clear()` followed by additions before the iterator asks for its next item.

## 2. What changed

### A cursor knows its registry slot

`CollectionData::Cursor` now holds both its entry `position` and its `slot` in
the weak registry. When an iterator reaches the end, its callback returns
`false`; `js_make_iterator()` latches that first done result, so that iterator
can never run the callback again. At that point `release_cursor()` swap-pops
its weak record and repairs the one moved cursor's slot. Releasing a completed
iterator is constant-time and does not wait for garbage collection.

The Map and Set `forEach` paths use the same cursor machinery while a callback
can mutate the collection, and unregister it on either normal or abrupt
completion.

Compaction and `clear()` still rebuild the registry because they already have
to visit every cursor that may resume. While rebuilding, they update both the
cursor position and its new slot:

- compaction translates the old position through the tombstone-to-packed-index
  table, so an iterator stays at the same logical next entry;
- `clear()` sets every resumable cursor to position zero before the entries are
  discarded, so entries added before its next call are visible as before;
- an iterator that has actually returned `done` has been unregistered, and the
  generic iterator wrapper keeps it done even if entries are added later.

### Abandoned partial iterators are swept geometrically

An iterator that a script abandons early can still have a cursor until a GC
cycle. `new_cursor()` therefore retains weak-pointer cleanup, but only when
the registry reaches `cursor_prune_at`. The next threshold is at least 64 and
otherwise twice the current registry size. A sequence of `n` such abandoned
cursors visits at most a geometric series of registry records, O(n) total,
rather than rescanning O(n) records for each of n creations. Completed ordinary
loops do better: they remove their cursor immediately, so their registry stays
empty between loops.

The registry is weak throughout. No cursor or iterator is rooted merely to make
compaction safe.

## 3. How it was verified

The two new Node-recorded corpus cases in
`tests/js-conformance/collections-iterators.dat` deliberately keep **two**
live cursors, consume different prefixes, delete `k1` through `k40` (triggering
compaction), append `tail`, then exhaust each cursor. They also make an
iterator before `clear()`, add `fresh` before its next call, and verify both
that it sees `fresh` and that it stays done after a later insertion. One case
uses Map entries and the other Set values.

```bash
make -j2 test
# All 214 engine test suites passed successfully!
# Cross-origin Window/Location and MessageChannel suite: 114 checks, 0 failure(s).

python3 tools/conformance/run_js_conformance.py --filter collections-iterators --reference 'node -e'
# JS conformance, full corpus: 34/34 (100.0%)  [1 files, 0 crashes, 0 timeouts]

python3 tools/conformance/run_js_conformance.py --filter collections-iterators
# collections-iterators.dat 32/34
# The two new Map/Set cases pass. The two older failures require Uint8Array
# and Proxy, neither of which is part of this change.

make bench-collections
# 2 case(s); 0 failure(s). Every batch ran in one process without gc(),
# and each printed checksum 98304.
```

`bench-collections` runs two 8,192-loop batches in the same process for a
three-entry Map and Set. It sums all values in each batch and permits the
second batch no more than 2.25 times the first plus 40 ms, so it measures
progression rather than startup speed or the speed of the host machine.
`make verify-collection-iterators` enforces the focused 32/34 corpus floor
(the other two cases need absent Uint8Array and Proxy); both guards are CI jobs.

For a before/after check, a binary made with the old
`BuiltinsCollections.cpp` was passed as `--baseline`; the least-contended
result of three runs was:

| collection | fixed early → late | old early → late |
| :--- | ---: | ---: |
| Map entries | 58 → 58 ms | 443 → 1,959 ms |
| Set values | 36 → 35 ms | 330 → 1,075 ms |

The old binary fails the checked-in guard (both later batches exceed their
allowed growth); the fixed binary passes it without calling `gc()`. The full
JavaScript scoreboard after adding the two cases is **1126 / 1152** with no
crashes or timeouts. An ASan+UBSan build also passed all 214 engine suites,
the focused 32/34 collection corpus, and the two-case performance guard with
no sanitizer report.

## 4. What is still open

There is still no automatic garbage collector. A partially consumed iterator
that becomes unreachable without ever returning `done` can leave a weak cursor
record until a script calls `gc()` or a geometric sweep, `clear()`, or
compaction encounters it. The new schedule bounds the creation cost, but does
not promise that such abandoned JavaScript objects consume no memory.

A `clear()` or tombstone compaction still visits every cursor that can resume:
that work is required to reset or remap its logical position and is separate
from ordinary repeated iteration. Very many simultaneously live, partially
consumed iterators can therefore still make those mutation operations costly.

This phase does not change the separate Map/Set constructor-dispatch,
`Symbol.species`, iterator-prototype, or automatic-GC work.
