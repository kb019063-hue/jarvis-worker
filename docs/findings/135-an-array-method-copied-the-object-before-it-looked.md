# Finding 135 — an array method copied the object before it looked at it

Phase 130. The architecture and the measurements are in
[`docs/phases/130-an-array-like-is-asked-not-copied.md`](../phases/130-an-array-like-is-asked-not-copied.md).

## 1. What was wrong

Every `Array.prototype` method that is not handed a real array went through one
helper, and that helper built a real array out of the argument:

```cpp
// src/js/BuiltinsArray.cpp, receiver(), before this phase
double length = js_to_integer(js_to_number(me.get_property("length")));
for (double i = 0; i < length && i < 4294967295.0; ++i) {
    elements.push_back(me.get_property(js_number_to_string(i)));
}
return JsValue::make_array(elements);
```

`length` is a number a page writes. So the loop's iteration count was chosen by
the page, its reads ran getters the page could see, and its allocation was as
large as the page asked. Three things followed, and they are one defect:

- **The process ended.** `Array.prototype.every.call({0: 9, length: Infinity},
  f)` asked for 2^32 − 1 places and threw `std::bad_alloc` out of a JS built-in
  — not a JS exception, an uncaught C++ one. 23 tests of
  `built-ins/Array/prototype` did this on `main`.
- **The method answered the wrong question.** The language takes `len` once
  and then, for each index, asks `HasProperty` and `Get` *of the object*. A
  write the callback makes is therefore seen by the next step, and a getter
  runs when its index is reached. A snapshot sees neither.
- **The callback was handed the wrong third argument.** `Array.prototype.map`
  and its relatives pass the object the method was called on (`O`), not a copy
  of it, and `[].map.call(o, cb)` where `cb` compares its third argument with
  `o` answered `false`.

The row also guessed 28 crashing tests. Measured: 23 crashed and 434 failed.

## 2. What changed

`src/js/BuiltinsArray.cpp` only.

`receiver()` is replaced by `receiver_view()`, which does `ToObject` and
`LengthOfArrayLike` and returns a **view** rather than a copy: an array whose
`internal` (a `std::shared_ptr`, already traced by `heap.mark_shared_value`)
holds the object and the length, taken once. `view_has` / `view_get` /
`view_this` ask the object at the step that needs it. Methods that allocate —
`map`, `filter` — check the length first and raise a `RangeError` for one no
array can hold, which is where Node.js instead tries to allocate it and stops
responding.

A real array with no hole to ask about keeps the engine's dense path: the view
is only made for something that is not an array.

## 3. Verification

- `make test` → **286 / 286**, new suite `an_array_like_is_asked_not_copied`
  (20 checks; 19 printed by Node.js v22.22.3 first).
- `built-ins/Array/prototype` → **2551 / 2812 (90.7%)** from **2355 / 2812
  (83.7%)**; crashes **23 → 0**.
- No scoreboard moved backwards: js-conformance 1143/1158, dom 678/678, url
  1188/1188, wpt 9567/9824 with 0 unlisted.

## 4. Still open

- `concat` does not read `Symbol.isConcatSpreadable` (23/69) — a separate row.
- `slice` is 50/71, mostly `Symbol.species`.
- `reduce` without a seed over an endless length walks forever, which is the
  standard's own shape and is the same in Node.js.
