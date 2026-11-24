# Phase 249 — array writes that must refuse

Task: `push`/`pop`/`shift`/`unshift` must honour a non-writable `length`, and a
species-result method must throw when it cannot define a place
(`CreateDataPropertyOrThrow`). Finding 253.

## 1. What was wrong

```js
var a = [];
Object.defineProperty(a, 'length', { writable: false });
a.push(1);                    // grew to 1 — should be a TypeError

var b = Object.freeze([]);
b.push(1);                    // threw, for the wrong reason (frozen, not length)

var arr = [1];
arr.constructor = { [Symbol.species]: function (n) {
  this.length = 0;
  Object.defineProperty(this, '0', { value: 'x', writable: false, configurable: false });
}};
arr.map(function (x) { return x; });   // wrote the place — should be a TypeError
```

Neither refused. The two symptoms came from one cause, which the phase found
after the first probe: **`Object.defineProperty` on an array silently dropped
every attribute it was given.**

```js
var a = [1];
Object.defineProperty(a, '0', { writable: false });
Object.getOwnPropertyDescriptor(a, '0').writable;   // true — wanted false

var b = [1, 2];
Object.defineProperty(b, 'length', { writable: false });
b.length = 0;                 // shrank to 0 — wanted a TypeError
```

`define_from_descriptor` had one shortcut for both cases: for an array, any
`value`-bearing descriptor on an index or on `length` went straight to
`set_property`, which for an index writes the dense element and for `length`
resizes the vector. The dense representation has no attribute bits at all, so
`writable`, `enumerable` and `configurable` were computed and then thrown away.
The fix to the first symptom was therefore the fix to the second: put an
attribute-carrying definition in the property map where attributes can live,
and let the dense element get out of its way.

## 2. What changed

**`src/js/BuiltinsObject.cpp`** — `define_from_descriptor` keeps its shortcut
only while the descriptor asks for the attributes a stored element has anyway
(`plain_attributes`): writable, enumerable, configurable truthy, and for
`length` *non*-configurable. Anything else takes the map:

- **An index.** The element's value and its three attributes are captured
  *before* `set_array_hole(index)` makes the place a hole — reading them after
  is the trap, because `set_array_hole` leaves `element_there` stale and the
  `{ writable: false }` form has no `value` field to fall back on. The map
  entry then carries the value and the flags, and `has_accessors` is set: the
  map is only consulted for a hole when that flag says the map has something.
- **`length`, `{ writable: false }`.** The count stays the value; the map gets
  a `length` entry with `Enumerable | Configurable` (no `Writable`), and
  `has_accessors` is set.
- **`length` as a value** is checked as ArraySetLength first: negative, a
  fraction, or above 2^32−1 is a `RangeError` before any attribute is looked
  at.

`descriptor_for` reads that `length` entry back, so
`getOwnPropertyDescriptor(a, 'length').writable` answers what was defined.

**`src/js/JsValue.cpp`** — the array `length` *read* consults
`properties.find("length")` first and falls back to the element count, which is
what the map entry above needs to be visible to script.

**`src/js/BuiltinsArray.cpp`** — the methods that build their result:

- `create_data_property_or_throw` (§7.3.6) next to `set_place_or_throw`. It is
  *not* `[[Set]]`: a place the species constructor made non-writable but
  configurable is replaced (that is `target-array-with-non-writable-property`
  in seven directories), one it made non-configurable is a TypeError, and a
  place past the end of an object that takes no new properties is a TypeError
  (`target-array-non-extensible`). Growing for an out-of-range index marks the
  places it skipped as holes — a plain `resize` makes own `undefined`s and
  turns `[0,,,].concat()` into three own places.
- `fill_species_result` and `copy_places_into` (which is `slice`,
  `toSpliced` and `with`) define their places through it; `concat`'s result
  too. `map` and `filter` define each result place through it.
- `dense_path` refuses a receiver whose chain answers for an index. A setter
  on `Array.prototype[0]` can make the array's `length` read-only *during* the
  method, and only the index-by-index path re-reads `length` and finds it in
  time.
- `push` and `unshift` on the dense path check `length` before they move
  anything: places are added, and an array whose length cannot rise takes
  nothing.

## 3. What is still open

`push/set-length-array-is-frozen.js` and its three siblings
(`unshift/set-length-*`, and the `set-length-zero-array-is-frozen` pair already
pass) still fail, and not for a reason this phase can fix in the way this phase
fixed the others. They install a **setter on `Array.prototype[0]`** that makes
the array's `length` read-only; the standard's `push` reaches that setter
through `[[Set]]`, because the place has no own property and the chain does.
This engine's array `set_property` writes the dense element and never walks the
chain, so the setter is never called (`calls=0`, where Node calls it once).

The gap is real and is not this phase's: `Array.prototype.reverse.call(...)`
and any other `[[Set]]` on an array index with a prototype setter have it too.
`dense_path` now sends such a receiver to the index-by-index path, which is
correct and is what the four remaining cases need — they still fail because the
generic path's `set_place_or_throw` ends in the same `set_property`. Fixing
`set_property` is finding 254's business.

`concat/S15.4.4.4_A3_T1.js` is in the baseline's failing set and stays there:
`[0, 1]` with `Array.prototype[2] = 2` concatenated must not copy the place the
chain answers for, which is `present_indices` and `HasProperty` on the chain —
a different question from any this phase asked.

## 4. How it was verified

```
make test                     All 393 engine test suites passed successfully!
                              Cross-origin suite: 114 checks, 0 failure(s)
run_test262.py built-ins/Array   2767 pass / 3083  (was 2750)
                                 17 fixed, 0 broken, net +17
make check-numbers            0 failure(s), 22 marked claims match the tree
```

Descriptor probes against Node, all eight matching: an index made
`writable: false` reads back `false`; `length` made non-writable is still
`enumerable: false, configurable: false` and still reads its count; a
`length` value grows the array; assigning after `{ writable: false }` throws;
a plain property keeps its attributes and an index keeps its own.

In-scope cases from the plan, 20 of 24 passing — the four that do not are
named in §3 with the reason and the finding that owns them.

The 17 fixed, from the run's own JSON:

```
of/does-not-use-prototype-properties.js
of/does-not-use-set-for-indices.js
prototype/concat/Array.prototype.concat_sloppy-arguments-with-dupes.js
prototype/concat/Array.prototype.concat_sloppy-arguments.js
prototype/concat/Array.prototype.concat_strict-arguments.js
prototype/concat/S15.4.4.4_A1_T4.js
prototype/concat/create-species-with-non-writable-property-spreadable.js
prototype/concat/create-species-with-non-writable-property.js
prototype/flat/target-array-with-non-writable-property.js
prototype/flatMap/target-array-with-non-writable-property.js
prototype/pop/set-length-array-length-is-non-writable.js
prototype/pop/set-length-zero-array-length-is-non-writable.js
prototype/push/set-length-zero-array-length-is-non-writable.js
prototype/shift/set-length-array-length-is-non-writable.js
prototype/shift/set-length-zero-array-length-is-non-writable.js
prototype/splice/S15.4.4.12_A6.1_T2.js
prototype/unshift/set-length-zero-array-length-is-non-writable.js
```

The species-result half of the task — `target-array-with-non-writable-property`
and `target-array-non-extensible` in **all seven** of `concat`/`filter`/`flat`/
`flatMap`/`map`/`slice`/`splice`, sixteen cases — passes in full; eleven of the
sixteen were already passing at the baseline and five came with this phase,
which is why they are not all in the seventeen above.
