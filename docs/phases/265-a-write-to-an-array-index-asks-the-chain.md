# Phase 265 — a write to an array index asks the chain, and a build does not

Task: the open half of Phase 249 and the blocked #193. A write to an index an
array does not own must ask its prototype chain (§10.1.9.2,
OrdinarySetWithOwnDescriptor), and the methods that **build** an array must
write with CreateDataProperty (§7.3.6), not `[[Set]]`. Finding 269.

#193 (Phase 156, finding 161) implemented the first half and was returned by
the landing session of 22 November: its `set_through_prototype` set
`handled = true` as soon as a prototype's map had the key, before knowing what
it found, so a **data** property on `Array.prototype[0]` refused a write that
should have shadowed it. Combined with the result-building `Set` of #176, the
two made `filter` throw `TypeError: Cannot assign to read only property '0'`
where the language answers `['p']`. This phase is that to-do list, checked
against Node rather than taken on trust.

## 1. What was wrong

```js
function probe(desc) {
  Object.defineProperty(Array.prototype, "0", desc);
  var a = []; a[0] = 5;
  return [a[0], a.hasOwnProperty("0")];       // [value, own?]
}
```

| at `Array.prototype[0]` | a browser | `main` before |
| :--- | :--- | :--- |
| a setter (with or without a getter) | `["p", false]` — setter runs, place stays a hole | `[5, true]` — element written, setter never runs |
| a getter, no setter | `["p", false]` — write refused | `[5, true]` |
| `{ value: "p", writable: false }` | `["p", false]` — write refused | `[5, true]` |
| `{ value: "p", writable: true }` | `[5, true]` — shadowed | `[5, true]` (right, for the wrong reason) |

And the second half, which only bites once the first half exists:

```js
Object.defineProperty(Array.prototype, "0", { set: function () {}, get: function () { return "p"; } });
Array.from([7])[0];        // was "p" — the setter swallowed the value
```

`Array.from`'s two writes and the array `splice` returns were still
`set_place_or_throw`, which is `Set(O, k, v, true)`. That is indistinguishable
from a definition for a fresh array — until a write to a hole starts asking the
chain, and then it is not.

## 2. What changed

**`src/js/JsValue.cpp`** — `set_through_prototype`, called from the element
path of `set_property` when the array does not own the place (a hole, or past
the end). Three answers, each one line of the standard:

- an **accessor with a setter**: it runs with the array as `this`, the place
  stays a hole;
- an **accessor with no setter**, or a **non-writable data property**: the
  write is refused — `false` here, a TypeError in strict code;
- a **writable data property**, or nothing: `handled` stays false and the
  ordinary element write shadows it.

The bug #193 shipped is exactly the third case being reported as the second:
`handled` was set on finding the key rather than on finding an *accessor*, so a
writable data property refused the write. Now only an accessor is `handled`; a
data property's own `Writable` bit decides between refusing and letting the
element write happen.

The gate is the count `g_index_named_keys` that already exists for
`js_array_holes_read_undefined` — any property whose name is an index, in any
map, accessor or data property. An ordinary program's maps hold none, so it
pays one relaxed load per element write and no walk.
`BENCH_JS_BASELINE` on the six core cases: `array` **0.99×**, total **1.01×**
faster, which is noise.

**`src/js/BuiltinsObject.cpp`** — `define_from_descriptor` now *covers* a place
past the end before making it a hole. Phase 249 marked an existing element a
hole and put the attributes in the map, but for an index the elements did not
reach it did neither, so the value stayed in the elements while the attributes
went to the map:

```js
Object.defineProperty(Array.prototype, "0", { value: "p", writable: false, configurable: true });
Array.prototype.hasOwnProperty("0");   // was false    (the map had it)
Array.prototype[0];                    // was "p"      (the element had it)
Object.keys(Array.prototype).length;   // was 0
```

`own` false with a readable value is a property that is half in each place.
Making the place a hole too puts both halves where the read and
`hasOwnProperty` already look.

**`src/js/BuiltinsArray.cpp`** — `Array.from`'s two writes and the array
`splice` returns are CreateDataPropertyOrThrow, the same helper Phase 249 added
for the other seven methods. `Set` stays right for the methods that *move*
places on their receiver (`reverse`, `sort`, `shift`, `unshift`, `push`,
`fill`, `copyWithin`, the moves inside `splice`).

## 3. What is still open

At Phase 265, `sort/precise-comparefn-throws.js` and
`sort/precise-prototype-element.js` were recorded as remaining, while
`sort/precise-prototype-accessors.js` passed. Phase 453 reran all three on the
current tree and found them passing; see [Phase 453](453-sort-accessor-order-regression-coverage.md).
The row in `AGENTS.md` now records that half as closed.

A setter reached this way is called with the array as `this` but is not
re-entered for the length update that may follow — the ordering the standard
gives a `push` that both writes a place and sets `length` is test262's
`set-length-array-*` family, all four of which now pass.

## 4. How it was verified

```
make test                     All 399 engine test suites passed successfully!
                              Cross-origin suite: 114 checks, 0 failure(s)
run_test262.py built-ins/Array   2774 pass / 3083 (90.0%)   was 2767
                                 7 fixed, 0 broken
run_test262.py built-ins/Object  3159 pass (92.6%)          was 3103
                                 56 fixed, 0 broken
run_test262.py language          21269 pass (92.4%)         unchanged
js-conformance                   1149/1158 (99.2%)          unchanged
BENCH_JS_BASELINE  array 0.99x, total 1.01x faster
```

The seven `built-ins/Array` cases:

```
from/iter-set-elem-prop-non-writable.js
from/source-object-length-set-elem-prop-non-writable.js
prototype/push/set-length-array-is-frozen.js
prototype/push/set-length-array-length-is-non-writable.js
prototype/sort/precise-prototype-accessors.js
prototype/unshift/set-length-array-is-frozen.js
prototype/unshift/set-length-array-length-is-non-writable.js
```

The four `set-length-array-*` cases are the ones Phase 249 recorded as its open
half and assigned to this phase — `AGENTS.md` row 971 said so in as many words.
The suite the landing session reported failing,
`a_hole_is_a_place_the_prototype_can_fill` (Phase 98), passes.

The new suite is `tests/suites/265-a-write-asks-the-chain.cpp`: the five shapes
of `Array.prototype[0]` above, the half-in-each-place case from §2, the
strict-mode refusal, `Array.from` and the array `splice` returns under a
prototype setter, and the check that an array with nothing on its chain still
writes its elements without asking.
