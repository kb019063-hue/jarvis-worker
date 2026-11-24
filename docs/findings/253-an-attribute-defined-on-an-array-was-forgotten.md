# Finding 253 — an attribute defined on an array was forgotten

**Severity:** correctness of property state. `Object.defineProperty` on an array
index or on `length` kept the value and dropped `writable`, `enumerable` and
`configurable`, so a "frozen" length could be assigned and a read-only element
could be overwritten — silently, and by ordinary code (`push`, `map`) rather
than only by a second `defineProperty`.

**Found by:** following Phase 249's task — `push`/`pop`/`shift`/`unshift` not
honouring a non-writable `length` — down to its cause. The first probe showed
`Object.defineProperty(a, 'length', { writable: false })` followed by
`a.length = 0` shrinking the array, which named the cause before the method
fixes started.

## What it was

`define_from_descriptor` (`src/js/BuiltinsObject.cpp`) had one shortcut for
every `value`-bearing descriptor on an array: an index went to
`set_property`, which writes the dense element, and `length` went to
`set_property`, which resizes the element vector. The dense representation is
a `vector<JsValue>` plus a hole bitmap — **it has no attribute bits**. The
three flags were computed a few lines later and then dropped.

So `defineProperty` on an array was a plain assignment with extra steps, and
every method that trusts property state (`push` on a frozen length, `map` onto
a non-configurable place) saw attributes that were never there.

The dense form is what most array code wants, so the fix is not "always use the
map": a plain `Object.defineProperty(a, '0', { value: 5 })` — the form that asks
for nothing but a value — still has to leave a fast element. The line is
`plain_attributes`: writable, enumerable and configurable all truthy, and for
`length` non-configurable as it is by default.

## What the fix holds

Recorded so the next reader does not re-derive them:

- **The map is only consulted for a hole when `has_accessors` is set.** Marking
  a place a hole and writing the map entry is *not* enough: `has_own_property`,
  `getOwnPropertyDescriptor` and the read path all test that flag first. A data
  property that needs attributes set it too. This cost a whole variant of the
  fix that appeared to change nothing (variant A).
- **Capture the element before making it a hole.** `set_array_hole` leaves
  `element_there` stale, so a descriptor applied after it wrote `undefined`
  with zero flags. The seven sites that read `has_accessors` are the reason
  both halves have to agree.
- **`{ writable: false }` carries no `value`.** Without falling back to the
  captured element value, the map entry holds `undefined`: the flags read back
  right while `a[0]` is gone (variant B's symptom).
- **`length` keeps the count as its value.** The map entry carries attributes
  only; the count still comes from the element vector, and the read path falls
  through to it.
- **CreateDataPropertyOrThrow is not Set.** A species result whose place the
  constructor made non-writable but *configurable* must be replaced (`map` on
  `target-array-with-non-writable-property`); one made non-configurable must
  throw. `set_place_or_throw` is `[[Set]]` with Throw=true and refuses the
  first, so the seven result-building methods needed §7.3.6 instead. Its own
  trap: growing the result to an out-of-range index with a plain `resize`
  makes own `undefined`s, which turned `[0].length = 3` concatenated into three
  own places; the skipped places are marked holes.
- **A splice writes its receiver with Set, not CreateDataProperty.** A blanket
  rewrite of the calls inside every method block over-reached and broke
  `S15.4.4.12-9-c-ii-1` and `A4_T2`; the receiver's places are Set, only the
  places of the array it *returns* are CreateDataProperty.

## Still open

Four in-scope cases (`push/set-length-array-is-frozen.js`,
`push/set-length-array-length-is-non-writable.js`, and the two `unshift`
siblings) install a setter on `Array.prototype[0]` that makes `length` read-only
during the call. The standard reaches it through `[[Set]]` on a place the array
does not own; this engine's array `set_property` writes the dense element and
never walks the prototype chain, so the setter is never called. Fixing that is
a change to `set_property`'s array branch — its own finding (254), not this one.
`dense_path` already routes such a receiver to the generic path so the fix will
be exercised when it lands.

`concat/S15.4.4.4_A3_T1.js` is a different, older gap of the same family: a
place the chain answers for must not be copied as an own place of the result.
