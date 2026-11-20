# Finding 125 — `reverse` ran over the accessor

Phase 116. The architecture and the measurements are in
[`docs/phases/116-the-methods-that-move-places.md`](../phases/116-the-methods-that-move-places.md).

## 1. What was wrong

A place of an array can be a property, and since Phase 108 a place of an array
in this engine can be an **accessor**: `Object.defineProperty(a, "0", { get,
set })` puts it in the object's property map and leaves a hole in the dense
element vector. A read of `a[0]` and a write of `a[0]` ask the map first.

The methods that move places inside an array did not. They opened the vector:

```cpp
JsValue array = receiver("reverse", true);
std::reverse(elements_of(array).begin(), elements_of(array).end());
```

So:

```js
var log = [];
var a = [1, 2, 3];
Object.defineProperty(a, 0, { get: function () { log.push('get'); return 10; },
                             set: function (v) { log.push('set ' + v); } });
a.reverse();
// log:      'get,set 3'   <- nothing; both are empty
// a[0]:     3             <- written straight into the vector
// a[0] descriptor: { value: 3, writable: true }   <- the accessor is gone
```

`reverse` did not merely miss the accessor: `a[2] = 10` wrote the vector and the
getter at index 0 was replaced by the element that used to be at index 2. The
other six were the same shape: `sort` never called a getter, `splice` read the
places it removed out of the vector, `copyWithin` copied vector entries,
`fill` wrote them, `shift`/`unshift` moved them. `toReversed`, `toSorted`,
`toSpliced` and `with` went through `read_through`, which answers for a *hole*
(since Phase 81 a hole reads `undefined` unless a prototype fills it) but has
no idea an index can be an accessor.

And all thirteen began with `receiver(name, /*mutates=*/true)`, which is

```cpp
if (!array.is_array()) js_throw_type_error("Array.prototype.shift requires an array");
```

for a method whose first step in the standard is `ToObject(this)`. So
`Array.prototype.reverse.call(true)`, `.call({0: 'a', 1: 'b', length: 2})`,
`.call('abc')` and `.call(function () {})` all threw "requires an array" —
sixteen test262 cases that only ask for the standard's behaviour.

Two smaller things came out of the same reading of the standard: `splice` and
`unshift` must throw a `TypeError` when the new length would pass 2^53 − 1
(they looped forever instead — `Array.prototype.splice.call({ length: 2 ** 53 - 1 }, 0, 0, null)`
never returned), and every built-in function has no `[[Construct]]`, so
`new Array.prototype.reverse()` must be a `TypeError` (nine test262 cases).

## 2. What changed

Everything is in `src/js/BuiltinsArray.cpp`; the detail is in the phase
write-up. In one paragraph: the seven methods that move places and the four
that copy them now use `this_object`, `length_of`, `has_place`, `get_place`,
`set_place_or_throw`, `delete_place_or_throw` and `set_length_or_throw` — the
standard's `ToObject`, `LengthOfArrayLike`, `HasProperty`, `Get`, `Set(…, true)`,
`DeletePropertyOrThrow` and `Set(O, "length", …, true)` — in the order the
standard names them, whenever the dense vector cannot answer the same question.
`dense_path`/`dense_readable` decide that: a real array, no accessor, not
frozen, sealed or non-extensible, a writable `length`, and holes that read
`undefined`. A plain parallel array keeps the memmove; a bench run of
`shift`/`unshift`/`splice` is unchanged at 33/10/25 ms.

## 3. How it was verified

```
python3 tools/conformance/run_test262.py \
  built-ins/Array/prototype/{reverse,sort,copyWithin,splice,fill,shift,unshift,toReversed,toSorted,toSpliced} \
  --binary before=<branch before this change> --binary now=build/litebird
```

- ten directories: **184 → 285 of 324** (56.8% → 88.0%); 101 fixed,
  **0 that passed before fail now** (compared test by test).
- `built-ins/Array` as a whole: **2331 → 2487 of 3083** (75.6% → 80.7%);
  156 fixed, 0 lost, and the same 26 tests killed by the OOM killer and 1
  timing out in both builds (`map`/`every`/`some`/`slice` on a length near the
  integer limit — methods this phase does not touch).
- Of the 39 still failing in the ten directories: 14 need `Symbol.species`
  (which does not exist), 8 typed arrays, 7 `Proxy`, 2 `$262`, 2 a `BigInt`
  literal, 7 a refused write (`Object.defineProperty` on an array index's
  attributes), 1 a setter on `Array.prototype` written through, 1 `sort`'s
  exact get/set order, and 2 a `copyWithin` case where V8 answers differently
  from the standard's own text.
- `make test` 279/279 (278 on `main` before the merge, plus this one); `make conformance` 1973/1973;
  `make js-conformance` 1142/1158 (98.6%); `make check-numbers` 0 failures.
- The new suite `array_methods_that_move_places_ask_the_index` holds 14 checks;
  each expected string was taken from Node.js 22 by running the same script,
  never from this engine.

## 4. What is still open

- **`Object.defineProperty` still ignores an array index's attributes.**
  `Object.defineProperty(a, "0", { writable: false })` is not recorded, so a
  write to it succeeds and `splice`/`shift`/`unshift` cannot throw for a
  non-writable index (`splice/target-array-with-non-configurable-property.js`,
  `splice/S15.4.4.12_A6.1_T2.js`). The same gap makes an array's `length`
  always writable, which four `shift`/`unshift` tests ask about. That is
  `JsValue::define_own_property` in `src/js/JsValue.cpp`, which another open
  pull request holds — it is a row of the table in `AGENTS.md`, not this one.
- **A setter on `Array.prototype` is not written through** when the write is to
  an array index: `set_property` goes to the element first.
  (`shift|unshift/set-length-array-is-frozen.js`.)
- **`Symbol.species`** is not consulted by `splice` (ten `create-species-*`
  cases); this engine has no `Symbol.species` at all.
- **`sort`'s exact `get`/`set` order against `Array.prototype` accessors**
  still differs in three `precise-*` cases: the values are right, the count and
  order of the calls is not.
- **`Proxy` and typed arrays** account for 15 of the 40 remaining failures in
  these ten directories; nothing about them is this task.
- The copying methods keep a dense fast path only for a hole-free plain array
  (a hole must not survive into their answer, so the vector alone cannot be
  copied); everything else reads place by place, which is slower but correct.
  There is no benchmark for `toReversed`/`toSorted`/`toSpliced`/`with` to say
  what it costs.
