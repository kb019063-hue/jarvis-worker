# Phase 288 — a length cannot be counted

Task: `AGENTS.md`, the `Array.prototype` row — *"the methods that **move**
elements — `reverse`, `sort`, `splice`, `fill`, `copyWithin`, `shift`,
`unshift`, `with`, `toSpliced` — and the iterator objects were not audited"*.
Finding 293. Agent: Heron.

## 1. What the audit found

The row asked for an audit of the methods that move elements. The first thing
the audit did was the thing the row did not: give an array a large `length`
and see what happens.

```
$ ./build/litebird --run-js "var a=[]; a.length=100000000; console.log(a.length)"
Killed
$ echo $?
137
```

```
$ ./build/litebird --run-js "var a=[]; a.length=4294967295; console.log(a.length)"
Uncaught std::bad_alloc
```

Node v22.22.3 answers both instantly. So do the browsers this engine imitates.

The cause is one line:

```cpp
obj->array_elements.resize(next, JsValue::make_undefined());
```

`array_elements` is a `std::deque<JsValue>` and `sizeof(JsValue)` is **88
bytes**. A length of 2^32-1 — legal, and the maximum the language defines —
asked for **377 GiB**. The process was killed before it could fail cleanly, or
`std::bad_alloc` reached script, which is the engine leaking C++ into a
language that has no such exception.

**A hole is not storage.** The whole of the defect is the engine paying 88
bytes for the statement "there is nothing here".

## 2. The three measurements that decided the design

| measurement | result |
| :--- | :--- |
| `vector<JsValue>(10M)` | 460 ms, 880 MB |
| `Holes::set()` 10M times | 22 ms |
| `a.length = 1e7`, before → after | 507 ms → 0 ms |

The hole *bits* were never the cost — 22 ms for ten million. The elements were.
That is what made the fix obvious: keep the bits honest, stop the elements.

## 3. What changed

`length` may now be held in the array's own property entry, and the elements
hold only the places that are there. The invariant is:

> **`array_elements.size()` is the count when there is no `length` entry, and
> the elements cover no more than the length when there is one.**

Five places had to learn it, each found by a failing test rather than by
reading:

1. **`set_property`, the grow branch** (`JsValue.cpp`): a grow past the
   elements records the length and allocates nothing. The invented `2^26` cap
   in `new Array` went with it.
2. **`set_property`, an index write** (`JsValue.cpp`): a place far past the
   elements (`a[99999999] = 1`) stores itself and moves the length, where it
   used to throw a RangeError invented at 2^24.
3. **`dense_path`** (`BuiltinsArray.cpp`): the dense element vector is only
   the array when it *covers* the length. `push` on a length-10 holes array
   took the dense path and wrote to index 0, answering 1 where Node answers 11.
   Three of its six callers were already adding this test by hand — which is
   what a rule that belongs in the helper looks like when it lives at the call
   sites.
4. **`visit_elements` and `array_snapshot`** (`JsOps.cpp`, `JsValue.cpp`):
   the iterator yields every place under the length, not every element —
   `[...a]` on five holes is five `undefined`. It re-reads the length each
   step, because `visit` runs script and `for (x of a) a.push(4)` must see 4.
5. **`Json.cpp` and `BuiltinsJson.cpp`**: JSON writes the length, holes as
   `null`.

Plus the descriptor's value, `Object.getOwnPropertyNames` not listing `length`
twice, and dropping the entry again once the length agrees with the elements
(without that, `log.length = 0; log.push('a')` left the length at 0).

## 4. What the audit *cleared*

Thirteen probes of the hole-preserving methods against Node — `slice`,
`concat`, `reverse`, `copyWithin`, `filter`, `map`, `flat`, `toReversed`,
`toSorted`, `splice`, `entries`, `forEach`, `indexOf`/`includes`/`join` —
agree on every one, holes and all. `with`/`toSpliced` fill their holes, which I
first entered as a bug and is what Node does too: `CreateDataProperty` makes
each place present. The suite pins all of it so the next audit starts from a
measurement.

## 5. Checks

```
make -j2 test                            419 suites, all passing
test262, before / after                  15135 → 15139 pass, 0 lost
make js-conformance                      1149/1158 (unchanged)
make dom-conformance                     678/678 (unchanged)
make conformance                         1973/1973 (unchanged)
make url-conformance                     1188/1188 (unchanged)
```

The four test262 tests gained are all `Object.defineProperty` and
`Object.defineProperties` on an array's own `length`; the one that a first
attempt lost (`getOwnPropertyNames/order-after-define-property`) is the `length
listed twice` case above, and it passes again.

Dense arrays are untouched: 200,000 `push`es followed by a summing read took
333 ms before and 340 ms after, which is noise.

## 6. What is still open

- **`find`/`findIndex` above 2^24 skip the holes.** They visit only the places
  that are present, so `[].length = 2e7; a.findIndex(v => v === undefined)`
  answers `-1` where Node answers `0`. I fixed this, measured it at 10.7 s for
  20 million places, and reverted it: an *empty* 20-million-iteration loop
  already takes 3.65 s here against Node's 13 ms, so the cost is the
  interpreter's per-iteration price and not `find`'s walk. Doing it honestly
  needs the bytecode VM on by default, which is a phase of its own. The
  deviation stays, and the row says why.
- **The elements still materialise up to `kMaxDenseGap` (2^16) places past the
  end** on a stray write, which is 5.7 MiB. That is a deliberate trade for
  keeping `a[a.length] = x` dense; the number is a constant with its reason
  written beside it, not a limit the language has.
- **`pop`, `shift`, `splice` at a length of 2^32-1** still walk to the far end
  in the generic path. They are correct, and they are slow for the same
  interpreter reason `find` is.
