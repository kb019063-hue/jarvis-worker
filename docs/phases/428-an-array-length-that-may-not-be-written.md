# Phase 428 — an array's length that may not be written

Task row: **A data property on an index of an array has no attributes of its
own** (its still-open half, after Phases 249, 265 and 388). Finding 428.

## 1. What was wrong

`ArraySetLength` (§10.4.2.4) refuses a new length in two places before it
deletes anything:

- **3.a** — the value is not a valid array index → `RangeError`.
- **3.b** — `oldLenDesc.[[Writable]]` is false and the value is not `SameValue`
  to the old one → `return false`.

The engine had 3.a in both of its copies of the algorithm and 3.b in neither;
both began from `array_elements.size()` and treated the count as the length.

```js
var a = [];
Object.defineProperty(a, 'length', { writable: false });
Object.defineProperty(a, 'length', { value: 12 });   // was: a.length === 12
Object.defineProperty(a, '0', { value: 1 });         // was: a.length === 1
a[0] = 1;                                            // was: a.length === 1
var b = Object.seal([1, 2, 3]);  b.length = 1;       // was: b.length === 1
var c = Object.freeze([1]);      c.length = 3;       // was: c.length === 3
// Node v22.22.3: TypeError, TypeError, length stays 0, TypeError, TypeError
```

The last two are different steps — `seal` leaves `length` writable and makes
the *places* non-configurable, so a shrink fails at step 4.c, while `freeze`
makes the length itself read-only, which is 3.b. Two answers that look alike
and must not be collapsed: a sealed array can still grow, and a frozen length
can still be re-defined with the value it already has.

## 2. What changed

Six reads of bits that were already there, in the two copies.

**`src/js/BuiltinsObject.cpp`** (`define_own_property`):

1. A read-only `length` refuses a value that is not `SameValue` to the current
   one — the map entry's `Writable` flag, or the cell's `frozen` bit.
2. A definition on an index at or past a read-only length refuses (step 4.a).
3. The shrink branch refuses when the cell is `sealed`, so the dense elements
   count as non-configurable for the deletion step.

**`src/js/JsValue.cpp`** (`JsValue::set_property`):

1. `[[Set]]` refuses on the map entry's `Writable` flag as well as `frozen`.
2. `obj->sealed && next < previous` → `false`.
3. An index at or past a read-only length → `false`.

## 3. The asymmetry, and the case that made me check

`[[Set]]` never enters `ArraySetLength`: `OrdinarySet` reads the own descriptor
and gives up at the writable bit, so it refuses *everything* on a read-only
length — including the value it already has — and reports `TypeError` where the
descriptor path reports the `RangeError` of step 3.a.

```
readonly, defineProperty {value: -1}          RangeError      (both)
readonly, 'use strict' a.length = -1          TypeError       (both)
readonly, defineProperty {value: a.length}    allowed         (both)
readonly, 'use strict' a.length = a.length    TypeError       (both)
```

Fifteen shapes went through Node v22.22.3 and this engine identically. Writing
both paths to the letter of the algorithm would break lines two and four.

**The regression the sweep caught.** An early version let
`{ writable: true }` through so that a read-only length could be made writable
again — a real need, because `Object.defineProperty(a,'length',{writable:true})`
was how the sweep's case 24 tried to restore it. The exemption passed case 24
and silently changed what a frozen array answered. Reverting it kept the clean
build's behaviour exactly, and the shape is recorded in finding 428 §"What this
does not fix" instead, under the larger defect it belongs to.

## 4. Evidence

- test262, one tree, fix stashed then applied: 15,185 → **15,199** of 15,686,
  **+14, 0 broken**, all 14 `length` cases.
- `make test` **479/479**; `js-conformance` 90/90 everyday and 1,153/1,158
  full; `dom-conformance` 678/678.
- Eight-case and twenty-four-case differential probes: the eight defect cases
  are identical to Node; the twenty-four differ only in three shapes that are
  wrong identically on the clean build (finding 428 §"What this does not fix").
- The suite **fails without the fix** — stashed, rebuilt, stops on `a read-only
  length took a new value: nothing was thrown` — and passes with it.

## 5. What the next row starts from

Not "the TypeError family", which is 44 cases and several causes. Measured on
this tree:

- **15** are `{ enumerable: true }` on `length`, which arrives through step 3's
  delegation to `OrdinaryDefineOwnProperty` without a `[[Value]]`, so the block
  this phase fixed never runs.
- **3** shapes are the integrity-level family in this phase's §3: a definition
  adding a place to a sealed or frozen array, and an attribute write on
  `length` that never reaches the property-map entry — the `{ writable: false }`
  branch writes `PropertyMap::Writable`, the opposite of what was asked.

The second is the bigger one and the one to take: `freeze`/`seal` only set cell
flags, the attribute write does not agree with them, and nothing implements the
`ValidateAndApplyPropertyDescriptor` refusals those two need.
