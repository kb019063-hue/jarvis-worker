## 217. `reduce` listed the places that were there before it walked, and a callback can create one

`Array.prototype.reduce` and `Array.prototype.reduceRight` collected the indices
that were present — `present_indices(array, length)` — and then walked that
list, asking only whether each index was still there. ECMA-262 §23.1.3.21
step 6.a and §23.1.3.22 step 6.a ask `HasProperty(O, Pk)` at each index **as
the walk arrives at it**, and the difference is visible: a callback can create
a place ahead of itself.

The suite case that names it, `reduce/15.4.4.21-9-1.js`, fills the hole at 2
from the callback running at index 1:

```js
var arr = [1, 2, , 4, '5'];
function cb(prev, cur) { arr[5] = 6; arr[2] = 3; return prev + cur; }
arr.reduce(cb);                                   // Node.js v22.22.3: "105"
```

The snapshot took the places `[0, 1, 3, 4]` before the first callback ran, so
index 2 — a hole when the list was built, an element four milliseconds later —
was never visited, and the answer was `"75"`. Fifteen of the thirty-two
`reduce`/`reduceRight` failures were this, and the `reduceRight` half was
worse than missing a place: because §23.1.3.22 walks **downwards**, a place a
callback creates *below* itself is still ahead of the walk, and the engine's
"walk upwards, then reverse" could not see it at all —

```js
var arr = [9, , , 9];
arr.reduceRight(function (p, c, i) { if (i === 3) { arr[1] = 1; arr[2] = 2; } return p + c; }, 0);
// Node.js v22.22.3: 21      this engine, before: 18     wrong fix below: 18
```

## What changed

`src/js/BuiltinsArray.cpp`, `make_reduce`. `reduce` now runs the walk that was
already there for `forEach`, `map` and `filter` — `walk_present_indices`, which
asks `HasProperty` as it reaches each index and has a fast path for a real
array with no accessors and no prototype index. `reduceRight` walks `k` down
from `length - 1` itself, asking `has_index_property` at each step, because
`walk_present_indices` only runs upwards and the direction is the whole point.

Two deviations are kept and stated where they are made. A length past
`kMaxWalkedIndices` (2²⁴) cannot be walked down either, so that case takes the
places that are there and visits them in reverse — the same bound and the same
deviation `find` already makes, and it changes only an answer that could not
otherwise be produced. And the start of the walk is still `length`, read once
before the first callback: `arr.length = 99` inside a callback does not extend
it, and `arr.length = 1` does not end it early.

## How it was verified

Three states, each a binary this session built:

| state | my suite | `built-ins/Array` |
| :--- | :--- | :--- |
| the engine before this phase | **red** (`Assertion` on case 1) | 2,726 / 3,083 |
| plausible-but-wrong: `reduce` fixed, `reduceRight` left on the snapshot | **red** | — |
| plausible-but-wrong: both fixed, `reduceRight` by listing upwards and reversing | **red** (case D, 18 ≠ 21) | — |
| the fix | **350 / 350** | **2,745 / 3,083** |

The second wrong fix is the one worth keeping: it passes every `reduceRight`
case in the first draft of the suite, because in `[1, , 3]` with no initial
value the callback never runs at index 2, so filling the hole from there looks
correct. `[9, , , 9]` with an initial value — where the callback *does* run at
the top — is the case that separates them, and it is in the suite.

Full numbers, both binaries, `python3 tools/conformance/run_test262.py
--binary before=/tmp/litebird-before --binary now=build/litebird`:

- `built-ins/Array`: **2,726 → 2,745 / 3,083 (+19 / -0)**
- `built-ins/Array` + the eleven `language/` and `built-ins/` directories that
  reach a function call, 18,069 tests: **17,115 → 17,134 (+19 / -0)**
- `built-ins/Array/prototype/reduce` + `reduceRight`: **488 → 498 / 520**
- `make test`: **350 / 350** suites, 349 before this phase plus the one added.

## What is still open

The 22 `reduce`/`reduceRight` cases that still fail are not this row: 12 want
`BigInt64Array`, 10 are `15.4.4.21-9-b-*` / `15.4.4.22-9-b-*`, which fail on a
`testResult !== true` precondition. `forEach`, `map`, `filter`, `some`, `every`
and the `find` family go through `walk_present_indices` already, and their
remaining failures are in the same two families.

Outside this row and this file: `Object.defineProperty(array, "length",
{writable: false})` is **ignored** — `BuiltinsObject.cpp:365` takes a shortcut
for an array's index or `"length"` that writes the value and throws the
attributes away. The descriptor still reads `writable: true`, and because every
element-moving method ends in `Set(O, "length", …)`, `push`/`pop`/`shift`/
`unshift`/`splice` on such an array succeed where they must throw a
`TypeError` (eleven test262 cases). `src/js/BuiltinsObject.cpp` is held by #244
as this is written, so it is a row of its own rather than a fix in passing.
