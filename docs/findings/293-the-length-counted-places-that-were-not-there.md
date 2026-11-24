# Finding 293 — the length counted places that were not there

Phase 288. Agent: Heron.

## The bug in one sentence

An array's `length` is the number of places it names; this engine answered
every length assignment by building a real element for each one, so a
length that named a hundred million empty places asked for 8.8 GB of them.

## How it looked

Two ways to die, both from one line:

```
$ ./build/litebird --run-js "var a=[]; a.length=100000000; console.log(a.length)"
Killed                                        # exit 137, the OOM killer
$ ./build/litebird --run-js "var a=[]; a.length=4294967295; console.log(a.length)"
Uncaught std::bad_alloc                       # a C++ exception reachable from script
```

The second is the worse one. `a.length = 4294967295` is a *legal program* —
that is the maximum array length the language defines — and a page that writes
it gets an exception no `catch` in any script can ever see, because
`std::bad_alloc` is not a JavaScript value. An engine that can throw a C++
exception into a page has a hole in its boundary, not just a slow path.

## Why it was there

```cpp
obj->array_elements.resize(next, JsValue::make_undefined());
```

`array_elements` is the array's dense element vector, a `std::deque<JsValue>`,
and `sizeof(JsValue)` is 88. The engine's model was "an array of length *n*
has *n* elements, some of which are marked as holes". That model is simple and
it is what most of the code assumes, and it is wrong in exactly the way it
looks: **a hole is the absence of storage, and this stored one anyway.**

The measurement that made the shape of the fix obvious:

| | 10 million places |
| :--- | ---: |
| `vector<JsValue>` resize | 460 ms, 880 MB |
| `Holes::set()` per place | 22 ms |

The bits that say "nothing here" cost 22 ms. The `JsValue`s that say nothing
at all cost 460 ms and 880 MB. Only one of those was ever needed.

## Why the audit did not find what it was looking for

The row asked for an audit of `reverse`, `sort`, `splice`, `fill`,
`copyWithin`, `shift`, `unshift`, `with`, `toSpliced`. Thirteen probes of those
methods against Node found **no** disagreement in what they answer — holes
preserved, holes filled, off-by-one at the ends, all correct. The audit found
the defect by doing something the row did not ask for: giving an array a large
`length` and seeing whether the engine survived.

That is worth writing down. The row was a good row, and following it exactly
would have produced a green report on methods that were all correct and missed
the fact that the thing they operate on could not exist. **An audit confirms
the cases it names; the bug is usually in the case nobody named.**

## What the fix had to relearn, one failing test at a time

Changing "the elements are the array" into "the elements are up to the length"
touched five places, and none of them were found by reading:

- `push` on a length-10 holes array wrote to index 0 and answered 1 (Node: 11),
  because `dense_path` checked attributes and not coverage;
- `[...a]` on five holes answered `[]`, because the iterator walked the
  elements;
- `JSON.stringify(new Array(5))` answered `[]`;
- `Object.getOwnPropertyDescriptor(a, 'length').value` answered 0;
- `Object.getOwnPropertyNames` listed `length` twice;
- `log.length = 0; log.push('a')` left `length` at 0, because the entry and the
  element count had become two sources of truth that disagreed.

Each one is now an assertion in `tests/suites/288-a-length-cannot-be-counted.cpp`,
so the next person who changes the invariant finds out from the suite instead
of from a page.

## What was deliberately left undone

`findIndex` on a 20-million-hole array answers `-1` where Node answers `0`: the
documented deviation above the 2^24 bound, which visits present places only. I
fixed it, measured **10.7 s** for the 20 million iterations, and reverted the
fix.

The reason matters more than the revert. An *empty* 20-million-iteration loop
takes **3.65 s** in this engine and **13 ms** in Node. The cost was never the
hole walk; it is what this interpreter charges per iteration, roughly 280× V8's.
Walking holes correctly is the right behaviour and it is unusable until the
bytecode VM is the default path. Shipping it would have traded a small wrong
answer for a large hang, and a hang in a page is worse than a wrong answer in a
`findIndex`.

So the deviation stays, with the measurement beside it, and the honest next
step is a VM phase rather than an array patch. Writing that down is more useful
than the fix would have been.

## The one-line version

`a.length = 1e8` was the difference between an engine that can be given a large
array and one that cannot; it cost a deque resize that should never have
happened, and it is now 0 ms instead of 507 ms.
