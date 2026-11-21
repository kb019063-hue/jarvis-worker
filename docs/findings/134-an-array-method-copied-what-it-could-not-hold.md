# Finding 134 — an array method copied the receiver before it read an argument

Phase 129. Status: **resolved** for the eleven Array prototype methods that
test262's `built-ins/Array/prototype` covers. §4 says what is still open rather
than implying the whole family is done.

## 1. What was wrong

Every `Array.prototype` method that could be called on something that is not an
array began the same way: it called `receiver()`, which **made an array of the
receiver by reading index 0 up to its `length`** — before the method had looked
at a single argument.

```cpp
// src/js/BuiltinsArray.cpp, before this phase
double length = js_to_integer(js_to_number(me.get_property("length")));
for (double i = 0; i < length && i < 4294967295.0; ++i) {
    elements.push_back(me.get_property(js_number_to_string(i)));
}
```

`length` is whatever the script says it is. So:

- **`{ 0: 9, length: Infinity }`** — four billion property reads, pushing four
  billion values. `Array.prototype.every.call(obj, f)` ended in
  `std::bad_alloc`, or the process was killed for the memory it took:

  ```
  $ ./build/litebird --run-js 'console.log([1,2,3].every.call({0:9,length:Infinity}, x => x !== 9))'
  Uncaught std::bad_alloc
  ```

- **`{ length: 2**32 }`** — an allocation where the standard asks for a
  `RangeError` from `ArrayCreate`, because a real array cannot be that long.

- **`{ length: 2**53-1, [2**53-2]: el }`** — a length that cannot be counted at
  all, for one property.

- And the copy was **wrong even when it fit**: `map` over `{0: 1, length: 3}`
  produced `[2, NaN, NaN]`, because reading the missing indices through
  `get_property` invented values the source never had. The standard's `map`
  leaves a place the source does not have as a hole.

## 2. What the Oracle said

`tools/conformance/run_test262.py` was already in the tree, so this was
measured against test262 at the pinned commit (`2e58f87d`) rather than against
what the engine believed. Eleven directories, 1,632 tests:

| | pass | crash | fail |
| :--- | ---: | ---: | ---: |
| before | 1,391 (85.2%) | **22** | 219 |
| after | **1,523 (93.3%)** | **0** | 109 |

Twenty-one of the twenty-two crashes were `exit -9` — SIGKILL, the out-of-memory
killer — across seven methods: `every` 2, `indexOf` 5, `map` 4, `slice` 4,
`some` 3, `lastIndexOf` 2, `includes` 1. **All 21 now pass.** 132 tests that
failed now pass; **not one that passed now fails.**

`make test` went 285 → 287 suites (two added by this phase) and all pass.
The other scoreboards are unchanged: JS conformance 1,143/1,158, DOM 678/678,
tree construction 1,973/1,973.

## 3. What the fix was

Three different questions, because the methods ask three different things.

**The places that are there.** `present_indices` returns the indices `object`
answers `HasProperty` for, below a limit, in ascending order — its own elements
that are not holes, its property map's integer keys, keys past 2^32-1 (a
`length-exceeding-integer-limit` source has them), a String object's
characters, and the indices its prototype chain has. It is a finite list, so a
length of 2^53 costs what the object holds and not what it claims.

**The index it reaches.** For a length short enough to walk (16 million), the
spec's literal loop — `HasProperty`, then `Get`, then the callback — is cheap
enough to run as written, and it is the only form that is *right*: a callback
can fill a hole the loop has not reached (`arr.forEach(function(){ arr[2] = 3 })`
over `[1,2,,4,5]` runs five callbacks, not four), and a getter can throw, and a
callback can shorten the array and end the loop early. All three of those were
tests this phase first broke and then fixed.

**An exotic receiver.** A `NodeList` keeps its indices outside the property
map, so nothing that reads only the map finds them. `own_keys` asks the object.
Without it, `Array.prototype.slice.call(host.childNodes)` answered the right
length full of `undefined` — and the engine's own
`js_compiled_custom_elements_are_upgraded` suite caught it, which is the
argument for having that suite.

Two bounds were added, both stated in the code where they are made: a method
that builds a real array throws the `RangeError` of `ArrayCreate` before the
first callback when the result would exceed 2^32-1, and `join` throws
`RangeError: Invalid string length` when the separators alone would exceed
512 MiB — which is what V8 and SpiderMonkey answer for
`{ length: 2**53-1 }.join()`, and better than a loop that never returns.

## 4. What is still open

- **Ten directories are still under 100%, 109 tests fail.** This phase fixed
  the crashes and the shapes around them; it did not chase the rest, and the
  ones that remain are the honest next step rather than a surprise.
- **`find`/`findLast` deviate above the 16 million bound.** They visit every
  index including holes, and the places that are not there all read the same
  `undefined`: past the bound the run of missing places is not walked. The
  comment in the code says so. A length under the bound is exact.
- **The other array methods were not audited.** `reverse`, `sort`, `splice`,
  `fill`, `copyWithin`, `shift`, `unshift`, `with`, `toSpliced` and the
  iterator objects still take their own paths; some of them call `length_of`
  and loop to it.
- **`length` is still read through the chain, but an own `length` on an
  array-like object shadows a prototype one.** That is what the standard says,
  and it is why `objectVal->klass == String` had to be special-cased at all —
  a String object is the one receiver whose `length` is *not* where a plain
  object's is.
