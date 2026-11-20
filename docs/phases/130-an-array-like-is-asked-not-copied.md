# Phase 130 — an array-like is asked for each place, not copied first

Task row: **An array method called on an object that is not an array copies the
object first, however long it says it is.** Finding 135.

## 1. What was wrong

`receiver()` is the helper every `Array.prototype` method that is not handed a
real array calls. It did this:

```cpp
double length = js_to_integer(js_to_number(me.get_property("length")));
for (double i = 0; i < length && i < 4294967295.0; ++i) {
    elements.push_back(me.get_property(js_number_to_string(i)));
}
```

Every index from 0 to `length` was read into a `std::vector` **before the
method took its first step**. That is wrong in three ways, and all three are
the same mistake: the method worked on a snapshot instead of on the object.

| probe | the language | this engine, before |
| :--- | :--- | :--- |
| `[].every.call({0: 9, length: Infinity}, f)` | reads index 0, calls `f`, `false` | **`std::bad_alloc`, process gone** |
| `[].some.call({0: 9, length: Infinity}, f)` | `true` | `std::bad_alloc` |
| `[].join.call({0:'a', 1:'b', length: Infinity}, '-')` | `"a-b"` | `std::bad_alloc` |
| `[].map.call(o, (v, i, arr) => …)` with `o = {…}` | `arr` is `o` | `arr` was the copy |
| `[].forEach.call(o, (v) => { o[1] = 99 })` | the next step sees `99` | the snapshot kept `2` |
| a getter on `o[1]` | runs when index 1 is reached | ran before the first callback |

The row said 28 tests of `built-ins/Array/prototype` ended in a crash, a
timeout or a `std::bad_alloc`. Measured, the number was **23 tests that
crashed the binary** and 434 that failed:

```
python3 tools/conformance/run_test262.py built-ins/Array/prototype
  main:  2355 / 2812 (83.7%)  [crash 23  fail 434]
  here:  2551 / 2812 (90.7%)  [fail 261]
```

## 2. What changed

`src/js/BuiltinsArray.cpp` — one file, and no other file in the tree.

An array-like is now **wrapped, not filled**. `receiver_view()` does what every
method does (`ToObject`, then `LengthOfArrayLike`), and returns a *view*: an
array whose `internal` — the `std::shared_ptr` slot the collector already marks
(`heap.mark_shared_value`) — holds the object and the length taken once.

- `view_has`, `view_get` ask the **object**, each time the step arrives. So a
  write the callback makes is seen by the next step, a getter runs when its
  index is reached, and an index the prototype has is visited.
- `view_this` is what a callback gets as its third argument: the object the
  method was called on, not the wrapper standing in for it.
- `view_first_absent` stops a copying method at the first place the object does
  not have, so `slice.call({length: Infinity})` is `[]` and not an allocation.
- `checked_length` is the one place a length can now fail: `map` and `filter`
  build a result of `len` places *before* the callback is asked for, so a
  length no array can hold is a `RangeError`. Node.js, on the same input, tries
  to allocate it and never returns.

Each method was moved to the standard's own form for the view: the length is
taken once, `HasProperty` and `Get` are asked per index, and the copy is built
only from places that exist. A real array with no hole to ask about still
takes the engine's dense path, unchanged.

## 3. How it was verified

- `make test` → **286 / 286** (285 on `main`). The new suite
  `an_array_like_is_asked_not_copied` has 20 checks; 19 were printed by Node.js
  v22.22.3 first, and the 20th (`map`/`filter` over `Infinity`) is the
  documented divergence above, argued in the suite's own comment.
- `make js-conformance` → **1143 / 1158 (98.7%)**, unchanged; `make
  dom-conformance` → **678 / 678**; `make url-conformance` → **1188 / 1188**;
  `make wpt` → **9567 / 9824, 0 unlisted**; `make test`'s cross-origin suite →
  **114 checks, 0 failures**.
- `built-ins/Array/prototype` → **2551 / 2812**, from **2355 / 2812**, with
  **23 crashes → 0**. `built-ins/Array` → 2688 / 3083.

The methods that gained the most are the ones the row named: `every` 97.7%,
`some` 97.7%, `reduce` 96.9%, `indexOf` 95.5%, `forEach` 95.8%.

## 4. What is still open

- **`concat` is 23 / 69.** It reads `Symbol.isConcatSpreadable` nowhere and
  checks `is_array()` instead, so an argument that is an array-like is appended
  as one value — the standard's `IsConcatSpreadable` is `@@isConcatSpreadable`
  or `IsArray`. That is a different defect from this one and is its own row.
- **`slice` is 50 / 71**, mostly `Symbol.species`: a subclass of `Array` gets a
  plain array from the methods that allocate (the row on built-ins that do not
  make the subclass they were called on).
- **`reduce` without a seed over `{0: 5, length: Infinity}`** still walks
  forward to the end of a length nothing has, and so does not return. The
  standard has exactly that walk, so this is the language's shape and not this
  phase's; Node.js is the same.
- **`sort` and the methods that write** still require a real array for a view
  in some paths (`dense_path` returns false for one), and an array-like is
  written through `set_property`, which is what `[].sort.call({0:3,1:1,2:2,
  length:3})` needs and gets.
