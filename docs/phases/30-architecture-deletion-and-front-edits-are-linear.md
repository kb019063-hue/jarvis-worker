# Phase 30 Architecture: Deletion and Front Edits Are Linear

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 30 Architecture: Deletion and Front Edits Are Linear` section.
> The text below is the section as it stood, unchanged.


Phase 23 measured two containers that went quadratic with their input. A
large object's property map rebuilt its hash index on every `delete`; an array
was a `std::vector<JsValue>`, so `shift()` copied the whole tail each call.
This phase changes the storage, not the public semantics.

### 1. The property map

An insertion-ordered table still has to be an insertion-ordered walk for
`Object.keys`, `for...in` and `JSON.stringify`. `PropertyMap` now holds:

- an `std::list<Entry>` with the properties in insertion order; and
- an optional `std::unordered_map<std::string, std::list<Entry>::iterator>`.

Up to twelve entries, `find` and `entry` are a linear list walk, exactly as
they were for small vectors. The first time a map grows past twelve entries,
the index is built once. Every later insertion and deletion keeps the list and
the index in step: an erase removes one list node and one hash entry, and no
surviving iterator moves. If the map later drops to six entries or fewer, its
bucket array is released and the map becomes linear again until it crosses the
threshold once more. `clear()` resets both structures. The enumeration loops
in `src/js/JsValue.cpp`, `src/js/JsEval.cpp` and the property-order users
elsewhere are unchanged: they see a list iterator with the same dereference
shape as the vector they saw before.

This is O(1) erase and O(1) lookup for a large map, at the cost of one list
node per property (the same order of overhead a hash entry already had) and
the inability to use raw index arithmetic. That trade is confined to dynamic
JavaScript objects; no parser, DOM or layout table uses `PropertyMap`.

### 2. Arrays

`JsObject::array_elements` is now a `std::deque<JsValue>`. The ends are the
hot paths a page meets early:

- `shift()` and `unshift()` move only a deque chunk boundary, not every
  surviving element;
- `push()` remains amortized O(1);
- reading by position still re-computes from the deque's block map, not by
  shifting elements when the front is removed;
- cloning into a vector (`js_iterable_to_vector`, JSON and `structuredClone`)
  copies through an explicit range; and
- `splice(0, n)` takes the front of a deque instead of memmoving the tail.

Middle `splice()` is still linear in the number of elements moved, because a
deque is not a random-access gap buffer; that is the same order as before and
is not what the phase set out to solve. Deleting an array element with
`delete a[i]` still creates a hole.

### 3. Compatibility

The change touches storage only. `Object.keys`, `for...in`, `JSON.stringify`,
`Object.assign`, cloning, DOM bindings and array builtins all retain their
order rules. `tests/test_suite.cpp` adds one suite,
`js_property_maps_and_arrays_keep_their_order_through_front_edits`, covering
order after `delete` and reassignment, a 2,000-property workload, identity
through `shift()`/`unshift()`, `splice(0, n)` and a full array consumed from
the front.

### 4. Measured

The Phase 23 scripts, run on the same machine before and after this phase:

| Script | Before | After |
| :--- | ---: | ---: |
| `delete` of the 50,000 keys of one object | 131,423 ms | 25–29 ms |
| `shift()` until a 50,000-element array is empty | 8,228 ms | 9–10 ms |

`tools/bench/bench_delete_shift.py` compares the two cases and the adjacent
front paths against Node and exits nonzero on a wrong result, a timeout or a
case over `--max-ms`. One verification run printed:

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

`make test` passes all 113 suites (the new one included). `make js-conformance`
is unchanged at 840 of 845 (everyday probe 90 of 90; 839 of 845 in the ASan
build, the same one deep-nesting case the sanitizer cannot fit); `make
dom-conformance` is 572 of 572, `make url-conformance` is 1188 of 1188, and
`make conformance` is 1781 of 1792, unchanged — the parser was not touched.
`make fuzz` reports 0 findings in 400 parser mutations and 400 layout/render
and image cases; `make deep` reports 0 failures in 1,292 runs over 445 pages;
`make asan` builds the engine with ASan and UBSan, reports no sanitizer
finding, and its ASan deep run covers 842 executions.

### 5. What is still approximate

- Property maps below twelve entries keep the linear scan; that is deliberate.
- A map that grew large keeps its bucket array until it falls to six entries
  or is cleared/destroyed. The bucket array is not shrunk incrementally.
- `splice()` from the middle is linear in the elements moved.
- Holes from `delete a[i]` are still holes in the array, not `undefined`
  tombstones that traversal can distinguish; this is recorded elsewhere as a
  missing feature, not changed here.

---
