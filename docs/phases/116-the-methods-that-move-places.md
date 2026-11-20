# Phase 116 — the methods that move places go through the accessor

Finding: [`docs/findings/125-reverse-ran-over-the-accessor.md`](../findings/125-reverse-ran-over-the-accessor.md).
Everything here is in `src/js/BuiltinsArray.cpp`.

## 1. What was wrong

An array in this engine is a dense `std::vector<JsValue>` of elements plus a
bitset of holes (`array_elements`, `array_holes`), and since Phase 108 a place
on an array can also be an *accessor*, kept in the object's property map. Reads
and writes made by script go through the one path that knows this:
`JsValue::get_property`/`set_property`, and `js_array_get` for an index.

Ten methods did not. They reached into the vector:

`reverse`, `sort`, `splice`, `copyWithin`, `fill`, `shift`, `unshift`, `push`,
`pop`, and — copying rather than moving — `toReversed`, `toSorted`,
`toSpliced`, `with`.

Every one of them began with

```cpp
JsValue array = receiver("reverse", true);   // mutates = true
```

and `receiver(..., mutates = true)` refused anything that was not an array with
`TypeError: Array.prototype.reverse requires an array` — for a method whose
first step in the standard is `ToObject(this)`.

The consequences, all of them observable before this phase:

```js
var a = [1, 2, 3];
Object.defineProperty(a, 0, { get: function () { log.push('get'); return 10; },
                             set: function (v) { log.push('set'); } });
a.reverse();      // log is EMPTY: neither the getter nor the setter ran
                  // and a[0] is now a data property: the accessor is gone
```

| method | what it did instead of `Get`/`Set` |
| --- | --- |
| `reverse` | swapped elements; a getter at either end was never called and the accessor was destroyed |
| `sort` | read the vector, so a getter was never called; wrote it back over the accessor |
| `splice` | read the removed places from the vector; wrote over accessors |
| `copyWithin` | copied vector entries, so a getter was never called |
| `fill` | wrote vector entries, so a setter was never called |
| `shift`/`unshift`/`push`/`pop` | moved vector entries (`pop` was already handled in Phase 108) |
| `toReversed`/`toSpliced` | `read_through`, which answers for a hole but not for an accessor |
| `toSorted`/`with` | read the vector, but wrote the answer as own data properties already |

`fill` on a frozen array did throw; `reverse`, `sort`, `splice`, `copyWithin`,
`shift` and `unshift` on a frozen array did *not*, because they never asked
whether the write was allowed.

## 2. What changed

### The place-by-place form

Seven helpers next to the existing ones:

| helper | the standard |
| --- | --- |
| `this_object(method)` | `ToObject(this)`, `TypeError` only for `null`/`undefined` |
| `length_of(o)` / `to_length(v)` | `LengthOfArrayLike`, clamped to 2^53 − 1 |
| `has_place(o, k)` | `HasProperty` |
| `get_place(o, k)` | `Get` — `js_array_get` for an array, so a hole still reads through the prototype |
| `set_place_or_throw(o, k, v)` | `Set(o, k, v, true)` |
| `delete_place_or_throw(o, k)` | `DeletePropertyOrThrow` |
| `set_length_or_throw(o, n)` | `Set(o, "length", n, true)` |

`set_place_or_throw` asks one extra question before it writes, `read_only_own`:
the engine's `set_property` reports success when the value written is the one a
read-only property already holds, and the standard's `Set` with `Throw = true`
calls that a refusal. That is what makes
`Array.prototype.shift.call(function () {})` a `TypeError` (a function's own
`length` is not writable) and what stops a write to a read-only index.

Then each method was rewritten in the shape the standard is written in:

- `reverse` — `HasProperty`/`Get` of the lower place, **then** the upper place,
  then `Set`/`Delete` in the standard's order. Reading the lower place can run
  a getter that removes the upper one; the order matters and one test262 case
  (`reverse/get_if_present_with_delete.js`) is exactly that.
- `sort` — `SortIndexedProperties` with skip-holes: the places the object has
  are read, sorted, written back from 0, and the places after them deleted.
- `splice` — the removed places are read, then the moving places are shifted
  one at a time in the direction the standard names, then `length` is set.
- `copyWithin` — the direction rule (`from < to && to < from + count`) decides
  whether it copies up or down.
- `fill`, `shift`, `unshift`, `push`, `pop` — as written in the standard,
  including `Set(O, "length", …)` at the end.
- `toReversed`, `toSorted`, `toSpliced`, `with` — `ToObject` of any receiver,
  then `ArrayCreate(len)` and **every** place written with a value from `Get`.
  A hole is not preserved by these four, which is what test262's
  `holes-not-preserved.js` in each of their directories says: `[0, , 2]` has
  three own places in `toReversed()`'s answer, the middle one `undefined`.

Two more things the standard asks for came with it:

- `push`, `unshift` and `splice` throw a `TypeError` when the places they add
  would pass 2^53 − 1, and `toReversed`/`toSorted`/`toSpliced`/`with` throw a
  `RangeError` when `ArrayCreate` cannot make the length (> 2^32 − 1), with
  `toSpliced` checking 2^53 − 1 first (a `TypeError`) as the standard has it.
  Every one of those was a hang or an allocation before: `splice` on
  `{ length: 2 ** 53 - 1 }` looped forever.
- `splice()` with no arguments still writes `length` (`Set(O, "length", len,
  true)`), which is why a read-only `length` refuses it and why the getter and
  setter for `length` on an array-like are each called exactly once.
- Every method of `Array.prototype` is now marked `not_constructor`, so
  `new Array.prototype.reverse()` is a `TypeError` (nine test262 cases).

### Why the dense path is still there

`dense_path(o)` answers "may this method use the vector?": the receiver is a
real array, has no accessor, is not frozen, sealed or non-extensible, and its
`length` is not read-only. `dense_readable(o)` adds `js_array_holes_read_undefined`
(Phase 81/98): a hole may be used only if reading it would give `undefined`
anyway, because otherwise `Get` finds what `Array.prototype` put there.

So a plain `[1, 2, 3]` still takes the memmove, and everything else takes the
place-by-place form. `tools/bench/bench_delete_shift.py`, which times exactly
these patterns, still prints

```
shift: until an array is empty                     39ms      190ms  1249975000
unshift: 20k times                                 11ms       58ms  19999 20000
splice: remove the first half                      23ms       32ms  25000 25000
```

(`--reps 3`, best of three; the reference column is Node.js. The numbers move a
few milliseconds between runs — a separate run of the same binary printed 56 ms
for `shift` and 203 ms for Node — so read them as "no worse by an order of
magnitude", not as three digits.)

## 3. How it was verified

Two builds, the same tests: `git stash` on this branch is the "before" binary.
test262's own suites for the ten methods that move or copy places —

```
python3 tools/conformance/run_test262.py \
  built-ins/Array/prototype/{reverse,sort,copyWithin,splice,fill,shift,unshift,toReversed,toSorted,toSpliced} \
  --binary before=… --binary now=build/litebird
```

| | before | after |
| --- | --- | --- |
| ten directories | **184 / 324 (56.8%)** | **285 / 324 (88.0%)** |
| `built-ins/Array` as a whole | 2331 / 3083 (75.6%) | 2487 / 3083 (80.7%) |

101 tests go from failing to passing in the ten directories and **none** of the
184 that passed before fails now; over the whole `built-ins/Array` suite 156 go
from failing to passing and none that passed fail now (compared test by test,
not by the total). The crash and timeout counts are the same in both builds —
26 and 1 — and they are `map`/`every`/`some`/`slice` on a length near the
integer limit, killed by the OOM killer, in methods this phase does not touch.

The 39 that remain, by what they need (one test can need two things):

- 14 are `splice`'s `@@species` machinery; this engine has no `Symbol.species`,
  so `splice` cannot build its answer from the receiver's constructor;
- 8 need typed arrays (`resizable-buffer`, `typed-array-resize`), 7 need
  `Proxy`, 2 need `$262`, 2 need a `BigInt` literal — none of which exist;
- 7 need a write to be **refused** where the engine allows it: an array index's
  attributes and an array's `length` writability are still dropped by
  `Object.defineProperty` (`src/js/JsValue.cpp`);
- 1 needs a setter on `Array.prototype` to be written through when the write is
  to an index of an array;
- 1 is `sort`'s exact `get`/`set` order against `Array.prototype`'s accessors;
- 2 are `copyWithin` cases where a `valueOf` shortens the array mid-call: V8
  stops the copy at the last place the array still has, and the standard's text
  (which takes `len` before the coercion) makes it 25 places instead of 19. The
  standard's form is what this engine does.

The rest of the checks:

```
make test            281/281 suites (one of them new, with 14 checks)
make conformance     1973/1973
make js-conformance  1143/1158 (98.7%)
make check-numbers   0 failure(s)
```

Each of the 14 checks in `array_methods_that_move_places_ask_the_index` was
written by running the same script in Node.js 22 first and pasting what it
printed; the expected text is Node's, not this engine's.

## 4. What is still open

- `Object.defineProperty(array, "0", { writable: false })` is still ignored
  (the attribute is not recorded), so `shift`/`unshift`/`splice` cannot throw
  for a non-writable *index*, and four test262 cases that make an array's
  `length` read-only still pass nothing. That is `JsValue::define_own_property`
  — `src/js/JsValue.cpp`, which another pull request holds.
- An array whose place is a *setter on `Array.prototype`* is not written
  through: `set_property` on an array index writes the element instead of
  asking the prototype chain. Two more test262 cases
  (`shift|unshift/set-length-array-is-frozen.js`).
- `@@species` is not consulted by `splice`, so the ten `create-species-*`
  cases still fail; `Symbol.species` does not exist in this engine.
- `sort`'s exact ordering of `get`/`set` against `Array.prototype`'s
  accessors (`sort/precise-prototype-accessors.js`,
  `precise-comparefn-throws.js`, `precise-prototype-element.js`).
- The copying methods keep a fast path only for a hole-free plain array; a
  holey one takes the place-by-place form even when it does not need to.
