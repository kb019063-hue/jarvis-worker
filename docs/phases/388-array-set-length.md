# Phase 388 — ArraySetLength deletes, refuses, and keeps its own attributes

Task: the row "A data property on an index of an array has no attributes of its
own" is struck through for the part Phase 249 fixed, and its **still open**
half names 190 `defineProperty`/`defineProperties` cases as *undiagnosed*. This
phase diagnoses them and fixes the two causes it found. Finding 388.

## 1. What was wrong

The row's measured symptom — `verifyProperty` reporting *"0 descriptor should
not be writable"* and *"`arr.hasOwnProperty("0")` !== true"* — is real but it
is not one fault. Taking the whole family apart by failure message gives two
groups, and each has a single cause.

**Group one — the sweep that never happened (12 cases).** An array's `length`
is an exotic property: §10.4.2.4 says that setting it *deletes* every place at
or above the new length, refuses the whole request with a `TypeError` when one
of them is not configurable, and leaves the length just past the last place
that would not go. The **assignment** path (`a.length = 2`) had all of that,
written for Phase 249. The **descriptor** path (`defineProperty` and
`defineProperties`) had only the range check:

```js
var a = [0, 1];
Object.defineProperty(a, '1', { value: 1, configurable: false });
Object.defineProperty(a, 'length', { value: 0 });
// was: no throw, and places above the new length stayed properties
// Node: TypeError, and a.length stays 2
```

So `Object.defineProperties(arr, { length: { value: 1 } })` was a different
operation from `arr.length = 1`, in an engine where the standard says they are
the same algorithm.

**Group two — `length`'s own attributes (8 cases).** Two faults in the
descriptor path, both about what an array's `length` is allowed to become:

```js
Object.defineProperty([], 'length', { writable: false });
// was: enumerable true, configurable true
// Node: enumerable false, configurable false — the descriptor only asked for
// the one attribute, and `length` starts as writable-only

Object.defineProperty([], 'length', { configurable: true });
// was: silently accepted
// Node: TypeError — §10.4.2.3 step 3.a.i, `length` may never be configurable
```

The first was a wrong attribute mask: the non-writable branch wrote
`Enumerable|Configurable` where it meant `Writable`, which made `length`
enumerable and configurable at the moment it was made non-writable. The second
was a missing refusal.

## 2. What changed

`src/js/BuiltinsObject.cpp`, in `define_own_property`:

- The `length` value branch now performs §10.4.2.4 step 4 in full: it computes
  the places that must go, deletes the configurable ones, and — before
  applying anything — refuses with a `TypeError` and sets `length` just past
  the last non-configurable place when one of them will not go. It reads the
  dense elements *and* the property map, because an index whose attributes are
  not the plain ones lives in the map as a hole (finding 269).
- The block sits **above** the plain-attributes shortcut. That shortcut
  returned early for a plain `{ value }` descriptor on `length`, which is
  exactly the shape every one of these tests uses; putting the sweep below it
  made the fix invisible until the ordering was corrected.
- The non-writable branch writes `PropertyMap::Writable` rather than
  `Enumerable|Configurable`.
- A descriptor asking for `configurable: true` on an array's `length` is
  refused with a `TypeError`.

## 3. What it was measured against

Node v22.22.3 answered every probe first; all five of the probes that the suite
asserts were wrong in the engine and right in Node, and none of the suite's
assertions had to be corrected after the fix — the one correction this phase
needed was to the *code*'s ordering, not to a test.

## 4. The numbers

| corpus | before | now |
|---|---|---|
| test262, all of it | 15,153 (96.6%) | **15,168 (96.7%)**, +15, 0 broken |
| `built-ins/Object/defineProperties` | 580/632 | **586/632** |
| `built-ins/Object/defineProperty` | 1,074/1,131 | **1,080/1,131** |
| `language` | 11,163/11,328 | 11,163/11,328 (unchanged) |
| `make test` | 449/449 | **450/450** |
| `make js-conformance` | 1,152/1,158 | 1,152/1,158 |
| `make wpt` | 9,570/9,824, 0 unexpected | 9,570/9,824, 0 unexpected |

The twelve gained in the first push are `15.2.3.7-6-a-112/113/166/168/169/175`
and `15.2.3.6-4-116/117/168/170/172/173`; the three more from the attribute
fixes are the `length`-attribute cases. Nothing was lost in either run, and
`language` never moved — the sweep deletes places, which is the kind of change
that could have taken a great many other cases with it.

## 5. What is still open

The family is **not** closed. What is left is measured, not guessed:

- **~25 cases** expect a `TypeError` that is not thrown, in shapes this sweep
  does not reach (a descriptor on `length` combining `value` with an attribute
  change, and the `defineProperties` ordering rules of §20.1.2.3.1).
- **~14 cases** still report *"0 descriptor should not be enumerable"* — an
  index written through the dense representation has no attribute bits of its
  own, which is the row's original claim and remains true for the plain path.
- **5 cases** report `"a === 10", actually 0`, which is a different family
  again.
- `BigInt64Array` is not defined, which accounts for 3 of the failures listed
  under this directory and is not a property-descriptor question at all.

The row is updated to say exactly this, so the next phase starts from the
remaining messages rather than from "190 undiagnosed".
