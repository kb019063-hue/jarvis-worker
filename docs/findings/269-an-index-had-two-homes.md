# Finding 269 — an array's index lived in two places, and only one of them had the attributes

**Severity:** correctness of property state, in two halves that hid each other.
A non-writable data property defined on `Array.prototype[0]` did not refuse a
write from an array: `a[0] = 5` wrote the element where the language leaves the
array empty. And the property itself was split — `hasOwnProperty('0')` said
`true` while the *value* was still in the element, so the descriptor and the
read disagreed.

**Found by:** the landing session of 22 November, returning #193 (Phase 156)
from a lot it could not merge, with the failing assertion and the two causes
named. This phase is that write-up, checked against Node and finished.

## What it was

Phase 249 taught `define_from_descriptor` to keep an index's attributes in the
property map when they are not the plain ones, and to make the place a **hole**
so the map's entry is what a read finds. It did that for a place the elements
already covered:

```cpp
if (in_range && element_there) { object.set_array_hole(index); cell->has_accessors = true; }
```

For an index *past the end* neither half ran, so the value went to the element
write and the attributes to the map. An array whose elements do not reach an
index never asks the map for it, so:

```
Object.defineProperty(Array.prototype, "0", { value: "p", writable: false })
Array.prototype.hasOwnProperty("0")   // false — the map has it, the elements do not announce it
Array.prototype[0]                    // "p"   — the element has it
Object.keys(Array.prototype).length   // 0     — fine, but for the wrong reason
```

The second half is what #193 was: the chain was never asked at all, because
`set_property`'s element path wrote `array_elements[index]` for a hole or a
place past the end. The two are one bug seen from each side — an index has two
homes and neither was authoritative.

#193 fixed the asking and got the answer wrong in one case. It reported the
chain as `handled` when a prototype's map **had** the key, then returned `false`
for a data property, and its caller refuses the write on `handled`. So a
*writable* data property on the prototype refused the write it should have been
shadowed by — and the comment over the `return false` said the opposite, which
is how it survived review.

## What the fix holds

- **Only an accessor is `handled`.** A data property's own `Writable` bit is
  the answer: writable means "let the element write shadow it", non-writable
  means "refuse". Setting `handled` on finding the key conflates the two.
- **The gate has to count data properties too.** #193 gated on a count of
  index-named *accessors*, which is right for the setter question and wrong for
  the non-writable-data one: a program with only the second never walked the
  chain. The count of index-named *keys* (`g_index_named_keys`, which
  `js_array_holes_read_undefined` already keeps) answers both.
- **A place past the end has to be covered before it is a hole.** Marking a
  hole is only meaningful for an index the elements reach; the read asks the
  map for a hole, and an index past the end is not one. Both halves or neither.
- **`Set` and `CreateDataProperty` only differ once the chain is asked.** That
  is why the two bugs could not be found separately: with the chain never
  asked, `Array.from`'s `set_place_or_throw` and a definition are the same
  thing. The moment a write to a hole asks the chain, a setter on
  `Array.prototype[0]` swallows every result `Array.from` builds.

## Still open

`sort` does not do its `Get`, `Set` and `DeleteProperty` in the standard's
order when `Array.prototype` has accessors (`sort/precise-comparefn-throws.js`,
`sort/precise-prototype-element.js`); `precise-prototype-accessors.js` passes
now. That is the other half of `AGENTS.md` row 971 and stays open.
