# Finding 428 — an array's non-writable `length` was written through

**Severity:** `Object.defineProperty(a, 'length', { writable: false })` was
recorded and then ignored. `Object.getOwnPropertyDescriptor` answered
`writable: false`, and every path that moved the length went on ignoring it:
the descriptor path grew a read-only array from 0 to 12, `defineProperty(a,
'0', …)` grew it again, and `a[0] = 1` grew it a third time. Node v22.22.3
throws a `TypeError` at all three.

**Found by:** the row "A data property on an index of an array has no
attributes of its own" and finding 388, which fixed the *sweep* on the
descriptor path — the deletion of the places above a new shorter length — and
closed by naming the shapes its sweep did not reach. This is not that sweep.
388's sweep runs only after the new length has been adopted. It never asked
whether the length was allowed to move at all, which is step 3.b of the same
algorithm and comes first.

## What it was

`ArraySetLength` (§10.4.2.4) has two refusals before anything is deleted:

- **3.a** — the value is not a valid array index → `RangeError`.
- **3.b** — `oldLenDesc.[[Writable]]` is false and the value is not
  `SameValue` to the old one → `return false`.

The engine had 3.a in both of its copies of the algorithm and 3.b in neither.
Both began from `array_elements.size()` and treated the count as the length.
The two copies are `define_own_property` in `src/js/BuiltinsObject.cpp` (the
descriptor path) and `JsValue::set_property` in `src/js/JsValue.cpp` (the
`[[Set]]` path); the duplication is why one missing read was six wrong answers.

The six, measured on a clean build of `c96ccf9a` against Node v22.22.3:

| # | shape | was | Node |
|---|---|---|---|
| B | `defineProperty(a,'length',{value:12})` on a read-only length | `length` 12 | `TypeError` |
| C | `defineProperty(a,'0',{value:1})`, same array | `length` 1 | `TypeError` |
| C2 | `a[0]=1` (sloppy), same array | `length` 1 | `length` 0, place absent |
| C3 | `a[0]=1` (strict), same array | silent | `TypeError` |
| I | `Object.seal([1,2,3])`, then `length:1` | `length` 1 | `TypeError` |
| K | `Object.freeze([1])`, then `length:3` | `length` 3 | `TypeError` |

**I and K are not the same bit**, which is the part worth keeping. `freeze`
makes `length` non-writable, so K is 3.b. `seal` leaves `length` writable and
instead makes every place non-configurable, so I is 4.c — the deletion the
shrink asks for fails. A fix that reads one flag for both breaks one of them,
and there are two already-correct cases that say so: a sealed array **may**
grow (`seal([1]); a.length = 5` → 5), and re-defining a frozen length with the
value it already has **succeeds** (§10.4.2.4 step 3's `SameValue` test).

## What changed

No new state: six reads of bits that were already there, in two places.

- `BuiltinsObject.cpp` — a read-only `length` refuses a new value unless it is
  `SameValue` to the current one; the index path refuses a definition at or
  past a read-only length (step 4.a); the shrink branch refuses when the cell
  is `sealed`, so the dense elements are treated as non-configurable.
- `JsValue.cpp` — `[[Set]]` refuses on the map entry's `Writable` flag as well
  as the cell's `frozen` bit; a shrink on a sealed array returns `false`; an
  index at or past a read-only length returns `false`.

## The asymmetry, which is not a guess

The two paths answer differently, and both ways match Node:

```
readonly, defineProperty {value: -1}          RangeError      (both)
readonly, 'use strict' a.length = -1          TypeError       (both)
readonly, defineProperty {value: a.length}    allowed         (both)
readonly, 'use strict' a.length = a.length    TypeError       (both)
sealed,   'use strict' a.length = -1          RangeError      (both)
frozen,   'use strict' b.length = 2           TypeError       (both)
```

`[[Set]]` never enters `ArraySetLength` — `OrdinarySet` looks at the own
descriptor and gives up at the writable bit — so it refuses *everything* on a
read-only length including the value it already has, and it reports `TypeError`
where the descriptor path reports `RangeError`. Writing both paths to the
letter of the algorithm would break the second and fourth lines.

## Evidence

Measured on one tree, fix stashed then applied, so the comparison is the same
commit twice: test262 **15,185 → 15,199** of 15,686 (96.8% → 96.9%), **+14
fixed, 0 broken**, and all 14 are `length` cases — 7 in
`built-ins/Object/defineProperty`, 7 in `defineProperties`.

`make test` **479/479** with the new suite; `js-conformance` everyday 90/90 and
full corpus **1,153/1,158**; `dom-conformance` **678/678**.

`tests/suites/428-an-array-length-is-not-writable.cpp` **fails without the fix**
— stashed, rebuilt, and it stops on `a read-only length took a new value:
nothing was thrown` — and passes with it. The cases there collect into a
`failures` array that the C++ asserts is empty, rather than letting the script
throw: an exception that leaves a suite is printed and the suite still reports
**PASSED**, so a suite that throws passes whatever it found.

## What this does not fix, measured

Three shapes in the sweep are wrong on both sides of this change, and none of
them is this bit. On the clean build they are wrong the same way; with the fix
they are wrong the same way still:

```js
Object.seal([1,2,3]);
Object.defineProperty(s, '9', { value: 1 });        // silent;  Node: TypeError

var f = Object.freeze([1,2]);
Object.defineProperty(f, '0', { value: 9 });        // silent;  Node: TypeError

var a = []; Object.defineProperty(a,'length',{writable:false});
Object.defineProperty(a, 'length', { writable: true });   // silent;  Node: TypeError
a.length = 3;                                             // was 3; now 0
```

The first two are `define_own_property` refusing to add a place to a sealed or
frozen array without throwing. The third is larger: **an attribute written on
`length` never reaches the property-map entry.** The `{ writable: false, … }`
branch writes the entry with `PropertyMap::Writable` — the *opposite* of what
was asked — so what the map holds and what the cell marks disagree, and
`defineProperty(a, 'length', { writable: true })` changes neither. Node refuses
that third one because a non-configurable, non-writable property cannot become
writable, which is `ValidateAndApplyPropertyDescriptor` and not
`ArraySetLength`. This change deliberately leaves the third's behaviour where
the clean build had it rather than half-fixing it: the read-only check reads
the map, and making the attribute write honest is a change to that branch, to
`freeze`/`seal` (which likewise only set cell flags and never write the entry),
and to the `ValidateAndApplyPropertyDescriptor` checks that go with them. That
is the next row, and it is named here with a measurement rather than a guess.

Fifteen of the 44 remaining `Expected a TypeError` failures on this tree are
`{ enumerable: true }` on `length`, which a descriptor with no `[[Value]]`
reaches through step 3's delegation to `OrdinaryDefineOwnProperty` — the
block this phase touched only runs when a value is present. Also a different
fix; also measured, not guessed.
