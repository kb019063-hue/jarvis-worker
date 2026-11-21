# Phase 129 — an array method must not copy a receiver it cannot hold

Task row: **An array method called on an object that is not an array copies the
object first, however long it says it is.** `AGENTS.md` → Open tasks.
Finding 134.

## 1. What was wrong

`receiver()` in `src/js/BuiltinsArray.cpp` turned any non-array receiver into a
real array before the method ran:

```cpp
for (double i = 0; i < length && i < 4294967295.0; ++i) {
    elements.push_back(me.get_property(js_number_to_string(i)));
}
```

`length` is script-controlled. `{ length: Infinity }` is a loop that allocates
until the process dies; `{ length: 2**32 }` is an allocation where the standard
wants a `RangeError`; and even for a receiver small enough to copy, the copy
*invented* values for places the source did not have, so
`Array.prototype.map.call({0: 1, length: 3}, x => x * 2)` was `[2, NaN, NaN]`
where the standard has `[2, <hole>, <hole>]`.

## 2. How it was measured

By test262, not by the engine's opinion of itself. `tools/conformance/run_test262.py`
already fetched the suite at a pinned commit; this phase pointed it at
`built-ins/Array/prototype`, whose 1,632 tests cover exactly these methods:

```
before:  1,391 pass (85.2%)   22 crash   219 fail
after:   1,523 pass (93.3%)    0 crash   109 fail
```

The crashes were `exit -9` — SIGKILL — across `every`, `some`, `map`, `slice`,
`indexOf`, `lastIndexOf` and `includes`. **All 21 now pass.** 132 tests that
failed now pass, and none that passed now fails.

## 3. What changed

`src/js/BuiltinsArray.cpp` only — 1,502 lines before, and the copy is gone from
all of it.

**`receiver()` no longer copies.** It checks the receiver exists, and that a
method that would write it has something writable, and returns it.

**Three answering mechanisms replace the copy.**

| Question | Mechanism | Why |
| :--- | :--- | :--- |
| which places does this receiver have? | `present_indices` | a finite list, so a length of 2^53 costs what the object holds |
| is *this* index there, now? | `walk_present_indices` (≤ 16M) | the spec's literal loop, which is the only form a callback filling a hole, a getter that throws, and a callback that shortens the array all agree with |
| what does an exotic receiver hold? | `own_keys` | a NodeList keeps its indices outside the property map |

Every method that read the copy was rewritten onto one of them: `slice`, `map`,
`filter`, `flatMap`, `some`, `every`, `forEach`, `indexOf`, `lastIndexOf`,
`includes`, `find`/`findIndex`/`findLast`/`findLastIndex`, `reduce`/`reduceRight`,
`concat`, `flat`, `at`, `join`, `toLocaleString`, `toString`, and the internals
`join_array` and `flatten_into`.

**Two `length` faults fixed on the way**, both found by tests that had been
passing for other reasons:

- `length_of` read `length` with `get_property`, but a `String` **object** keeps
  `length` and its characters on `String.prototype`. `every.call(new String("432"))`
  visited nothing because the receiver answered a length of 0.
- `Array.prototype.toString` did not exist, so arrays fell through to
  `Object.prototype.toString` and `[1,2].toString()` was `"[object Array]"`.
  The standard's form is `join` looked up **on the receiver**, which is now
  what is there — and it is why joining an array into itself produces the text
  test262 expects.

**Two bounds, both stated where they are made.** A method that builds an array
throws `ArrayCreate`'s `RangeError` before the first callback when the result
would exceed 2^32-1. `join` throws `RangeError: Invalid string length` when the
separators alone would exceed 512 MiB, which is what V8 and SpiderMonkey answer
for `{ length: 2**53-1 }.join()`.

## 4. Verifying it

```
make test                    287 / 287 suites (285 before; two added here)
test262 Array/prototype      1,523 / 1,632  (1,391 before), 0 crash
make js-conformance          1,143 / 1,158  (unchanged)
make dom-conformance         678 / 678      (unchanged)
make conformance             1,973 / 1,973  (unchanged)
check_numbers, check_docs    0 failures
```

Two suites hold the fix in place:

- `array_methods_survive_a_length_they_could_never_reach` — the shapes the 21
  crashing cases use, with each of the three faults separate, plus the string
  and negative forms, the hole a callback fills, and the length a callback
  shortens.
- `array_methods_over_an_exotic_receiver_ask_it_for_its_indices` — slice, map,
  forEach, join and indexOf over a real `NodeList`.

One assertion in the first draft was written from expectation and was wrong:
`every` over `{length: "Infinity"}` was guessed to visit nothing, and visits
one index, because `ToNumber("Infinity")` is `Infinity`. It is measured now.
Keeping it in the write-up because the measurement is the point of the phase.

## 5. What is still open

109 tests in these directories still fail; this phase took the crashes and the
shapes around them, not the whole family. `find`/`findLast` deviate above the
16-million bound by not walking a run of missing places that all read
`undefined` — the comment in the code says so. The array methods that move
elements (`reverse`, `sort`, `splice`, `fill`, `copyWithin`, `shift`,
`unshift`, `with`, `toSpliced`) and the iterator objects were not audited; some
of them still loop to `length_of`.
