# Phase 213 — `reduce` asks for each place as it reaches it

**Finding 217** (`docs/findings/217-reduce-listed-the-places-before-it-walked.md`).

## What was wrong

`Array.prototype.reduce` and `reduceRight` called `present_indices(array,
length)` before the first callback ran, then walked that list. The standard
asks `HasProperty` at each index as the walk arrives, so a place a callback
creates ahead of itself must be visited — and was not.

`reduceRight` had a second fault from the same shortcut. §23.1.3.22 walks
**down** from `length - 1`, and a place a callback creates below itself is
still ahead of that walk; a list built upwards and then reversed cannot
contain a place that did not exist when it was built.

## What changed

`src/js/BuiltinsArray.cpp` only — `make_reduce`, the shared factory behind both
methods.

`reduce` now uses `walk_present_indices`, the walker `forEach`, `map` and
`filter` already share: it asks `has_index_property` at each index as it
reaches it, skips a run of absent places in one step when the receiver is a
real array with no accessors and no prototype index, and keeps the callback
count bounded when `length` is enormous.

`reduceRight` walks `k` from `length - 1` down to 0 and asks
`has_index_property` at each step. Above `kMaxWalkedIndices` (2²⁴) it falls
back to the places that are there, visited in reverse — the same bound and the
same stated deviation `find` makes.

Unchanged: `length` is read once, before any callback; an accumulator with no
initial value starts at the first place the walk reaches; an empty receiver
with no initial value is a `TypeError`.

## How it was verified

Three states, each a binary built from this branch:

| state | suite | `built-ins/Array` |
| :--- | :--- | :--- |
| before | **red** | 2,726 / 3,083 |
| wrong fix: `reduce` only | **red** | — |
| wrong fix: `reduceRight` lists upwards, reversed | **red** | — |
| the fix | **350 / 350** | **2,745 / 3,083** |

`python3 tools/conformance/run_test262.py --binary before=… --binary
now=build/litebird built-ins/Array` — 19 gained, 0 lost. Over `built-ins/Array`
plus the eleven `language/` directories that reach a call, 18,069 tests: 19
gained, 0 lost. `make test`: 350 / 350.

The suite is `reduce_asks_for_each_place_as_it_reaches_it` in
`tests/test_suite.cpp`; every expectation in it is what Node.js v22.22.3
prints for the same expression.

## What is still open

`Object.defineProperty(array, "length", { writable: false })` is ignored —
`src/js/BuiltinsObject.cpp:365` writes the value and discards the attributes,
so the eleven test262 cases that require the element-moving methods to throw on
such an array still fail. That file is held by another open pull request
(#244); it is a row of AGENTS.md now, not a fix in passing.
