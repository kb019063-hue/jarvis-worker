# 33. ✅ Property deletion and array front edits were quadratic

> Moved out of `AUDIT.md` — written there as a `## 33. ✅ Property deletion and array front edits were quadratic` section.
> The text below is the section as it stood, unchanged.


**The hole.** A large property map rebuilt its entire hash index on every
`delete`: an object with 50,000 keys performed 50,000 linear-index erases,
then 50,000 rebuilds that hashed every remaining key, so deleting the last
key was an operation on all the keys before it. Small property maps avoided
the hash cost by staying linear, but the rebuild ran for the large ones.
Arrays were plain `std::vector<JsValue>` instances, so `shift()` and two-at-a-time
front edits copied (and destroyed) the whole tail each call.

**The fix.** Property entries are now an insertion-ordered `std::list<Entry>`,
and the hash index points into that list instead of into offsets. One index is
built the first time a map grows past twelve entries and is maintained on
insertion and removal; `erase()` removes the list node and the hash entry in
O(1), and no iterator to any surviving property changes. Small maps still use
the linear scan exactly as before, so the common case does not pay for a
hash-map lookup it cannot use. A map that falls
back under half the threshold drops its index, so the retired hash table is
not left behind while the object lives. Array elements
are now `std::deque<JsValue>`, making `shift()` and `unshift()` operations on
the front of the deque; code that indexes, copies or otherwise round-trips an
array re-reads bounded ranges and is unchanged. The full account is in
ARCHITECTURE.md, Phase 30.

**Measured.** The Phase 23 scripts, run on the same machine before and after:

| Script | Before | After |
| :--- | ---: | ---: |
| `delete` of the 50,000 keys of one object | 131,423 ms | 25–29 ms |
| `shift()` until a 50,000-element array is empty | 8,228 ms | 9–10 ms |

`tools/bench/bench_delete_shift.py` keeps the two cases and the adjacent
`unshift()`, front-`splice()` and enumeration-after-delete paths, compares
against Node by default, and exits nonzero if a case times out, errors or
prints a different result. One run printed:

```
python3 tools/bench/bench_delete_shift.py --reps 3 --max-ms 2500
case                                           litebird  reference  result
delete: 50k keys                                   58ms       59ms  0
delete: reinsert after erase                       49ms       61ms  last
shift: until an array is empty                     30ms      189ms  1249975000
unshift: 20k times                                  9ms       50ms  19999 20000
splice: remove the first half                      20ms       25ms  25000 25000
enumerate: order after delete and insert            4ms       23ms  a,c,d,b
```

**What is still approximate.** Property maps below twelve entries have not
changed their lookup algorithm; taking the middle out of an array with
`splice()` is still linear in the number of elements that move; and a large
map keeps its bucket array while it stays above half the threshold, releasing
it only when it drops lower or the object is destroyed. `delete a[i]` still
leaves holes in an array, as it did before.

**How verified.** `make test` prints `All 113 engine test suites passed
successfully!`. The new suite
`js_property_maps_and_arrays_keep_their_order_through_front_edits` covers
insertion order after `delete` and reassignment, enumerated order after a
2,000-property workload, identity through `shift()`/`unshift()`, the state of
`splice(0, n)`, and a full array consumed one element at a time.
`make js-conformance` is 840 of 845 (everyday probe 90 of 90; 839 of 845 in
the ASan build, which cannot fit the deep-nesting case). `make dom-conformance`
is 572 of 572, `make url-conformance` is 1,188 of 1,188 and `make conformance`
is 1781 of 1792, unchanged; the parser was not touched. `make fuzz` reports
0 findings in 400 parser mutations and 400 layout/render and image cases;
`make deep` reports 0 failures in 1,292 runs over 445 pages; and `make asan`
reports no sanitizer finding in the 113 suites, the corpora, the fuzzers and
the ASan deep run (842 executions). The bench command above passed against
Node 22 on the same machine.
