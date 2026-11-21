# Phase 137 — a pattern reads the place, not the vector

Task row: **Array destructuring reads an array's elements, not its places.**
Finding 142.

## 1. What was wrong

`IterationCursor` (`src/js/JsEval.cpp`) is what a pattern asks for its source
(Phase 102, `docs/phases/102-a-pattern-asks-for-the-iterator.md`). It has three
modes: a string it walks by code point, an array whose `Symbol.iterator` is
still the installed method it reads place by place, and everything else by the
protocol. The array mode read the places out of `array_elements` — the dense
vector — which is the array's *storage* and not its properties:

```cpp
if (m_index < elements.size()) return m_source.array_element(m_index++);
```

Since Phase 108 a place can be an **accessor**: `Object.defineProperty(a, "0",
{ get })` puts it in the property map and leaves a hole in the vector. Since
Phase 98 a hole can be a property of `Array.prototype`. The array's own
iterator, `for…of`, the spread, `join` and `JSON.stringify` all read a place as
`a[i]` reads it (`js_array_get`); the pattern alone read the vector, so the
same engine that answered `"g"` for `[...a]` answered `undefined` for
`var [x] = a`, and two lines of one script disagreed:

```js
var a = [1, 2, 3];
var hits = 0;
Object.defineProperty(a, "0", { get: function () { hits++; return "g"; },
                                configurable: true });
var [x] = a;          // "g" (the standard); this engine: undefined, hits 0
for (var v of a) …    // "g" here, and in this engine since Phase 108
```

Measured on a build of `main` at `e1414f6` before the change, and in Node.js 22,
with the two sentences of the row and eleven more:

| script | Node.js | this engine, before | after |
| :--- | :--- | :--- | :--- |
| `var [x, y, z] = a` with a counting getter at 0 | `["g",2,3,1]` | `[null,2,3,0]` | `["g",2,3,1]` |
| `var [x, ...rest] = a` with a getter at 1 | `[1,["g",3]]` | `[1,[null,3]]` | `[1,["g",3]]` |
| `Array.prototype[1] = "p"; var [x, y, z] = [1, , 3]` | `[1,"p",3]` | `[1,null,3]` | `[1,"p",3]` |
| the same with a counting getter on `Array.prototype[1]` | `[1,"p",1]` | `[1,null,0]` | `[1,"p",1]` |
| `var [[p], [q, r]] = a`, a getter at 1 returning `[9, 8]` | `[1,9,8]` | `throws TypeError` | `[1,9,8]` |
| a getter that throws through a pattern | `throws RangeError` | `undefined` | `throws RangeError` |
| `var [x] = a` over an accessor with a setter | `["from getter",null]` | `[null,null]` | `["from getter",null]` |

The second call `throws TypeError` on the engine because the nested pattern
walked the place's `array_elements` — `[9, 8]` was `undefined` there, and
destructuring `undefined` is the `TypeError` the language gives.

What was *not* wrong, and is why the fix is one line: a source whose
`Symbol.iterator` a script replaced goes through the protocol mode and was
already read correctly (`["v0","v1"]` in both builds and in Node.js), a
string is the code-point mode, and a place no prototype fills reads `undefined`
either way.

## 2. What changed

- **`src/js/JsEval.cpp`**, `IterationCursor::pull()`, the `Mode::Array` case:
  `js_array_get(m_source, m_index++)` where it read
  `m_source.array_element(m_index++)`. `js_array_get`
  (`include/litebird/js/JsOps.hpp`) is what `a[i]` reads: the element when
  there is one, and otherwise what the array's own property map and then its
  prototype chain have for the index, `undefined` when they have nothing. It
  answers an element out of the vector, so a place that is an element costs
  what it did; a hole asks `js_array_holes_read_undefined` first, which is the
  question whether any index could be on the chain at all, and answers it from
  a remembered chain and a counter for nearly every array.
- The comment above the "not iterable" throw in the same constructor said that
  `for…of` accepts an object that only has `next`, which was true before Phase
  102 and is not now. It says what is measured: `var [x] = { next() {} }` and
  `for (var x of { next() {} })` are both `TypeError`s, here and in Node.js.
- **`tests/test_suite.cpp`**: `array_destructuring_reads_the_place_not_the_vector`,
  13 checks. Each was run through Node.js 22 first and its expected text is
  what Node.js printed; 9 of the 13 fail when the suite is run through a build
  of `main` before this change.

## 3. How it is verified

- **The 13 checks**, in Node.js 22 and in this build: identical text for all
  thirteen. Nine of them differ on a build of `main` at `e1414f6` — the seven
  in the table above plus the last-place rest and the agreement case, where
  the pattern bound `null` while `for…of` and the spread beside it in the same
  script bound `"g"`.
- **`make test`** → **300 / 300** suites (299 on `main` after the merges below it,
  296 when the change was made) and 114 cross-origin
  checks, all passing.
- **The corpora, unchanged**: `make js-conformance` 1143 / 1158 (98.7%) and
  90 / 90 everyday; `make dom-conformance` 678 / 678; `make conformance`
  (tree construction) 1973 / 1973; `make url-conformance` 1188 / 1188; `make
  wpt` 9567 / 9824 with 257 failures, all listed and none unlisted. Nothing in
  the recorded corpora destructures an array that has an accessor on a place
  or a prototype that fills one, so this adds no case to any of them — the
  minimum cost of a suite that would have caught it.
- **test262, two builds** (`python3 tools/conformance/run_test262.py
  --binary before=/tmp/litebird-before --binary now=/tmp/litebird-after`):
  - `language/statements/{variable,let,const}/dstr` and
    `language/expressions/assignment/dstr`: **591 → 591 of 651**, 0 gained,
    0 lost.
  - `built-ins/Array`, `language/statements/for-of` and
    `language/expressions/array`: **3192 → 3192 of 3884**, 0 gained, 0 lost,
    run twice with the same pass count. Of the 3884, 3869 have the same
    verdict in both builds; the other 15 are the tests that build an array
    with a `length` near 2^53 − 1 — `map/15.4.4.19-3-8.js`,
    `findLast/maximum-index.js`, `includes/length-boundaries.js`,
    `slice/length-exceeding-integer-limit.js` and their neighbours — which
    are killed by the OOM killer or time out *in both builds*: the split
    between "crash" and "timeout" differs from run to run of the same build
    (19/6 in one before run, 20/4 in another), and none of the 15 passes.
- **Time.** Two loops of 200,000 patterns of ten places each, the lowest of
  five runs, in milliseconds:

  | script | `main` | this change |
  | :--- | :--- | :--- |
  | ten places, dense array | 200 (201, 202, 200) | 197 (197, 203, 199) |
  | ten places, every place a hole | 190 (193, 195, 192) | 201 (207, 203, 203) |

  Four runs of each build, alternating, and the other three are beside the
  lowest one. An element costs nothing measurable: the
  first branch of `js_array_get` is the vector read. A hole costs the
  question whether it could read as anything, about 5 ns, which over two
  million holes is 190 → 201 ms: the pattern asks again for every place,
  because a getter on the place can run a script between two of them, and
  `js_array_holes_read_undefined` is what keeps that question from walking two
  prototypes' sixty names each time.

## 4. What is still open

Rows of the table in `AGENTS.md`, none of them new with this change.

- **An inherited `length`.** `Object.create([7, 8, 9]).length` is `undefined`
  where the language reads `3` — an index is looked for on the chain, a
  `length` is not. The corpus case is `tests/js-conformance/iteration.dat`
  line 729 (`1,2,3 3 2 false`).
- **A data property defined on an index of an array keeps no attributes**, so
  `Object.defineProperty(a, "0", { value: 1, writable: false })` leaves the
  place writable and `delete`able as an element.
- **`toReversed` and `toSorted` keep a hole a hole** where the standard writes
  `undefined` to every place, and `toReversed` reads from the first place up
  where the standard reads from the last down.
- **The `js_iterate()` row for `{ next }` is measured false, and is left for
  its owner.** On this build and in Node.js 22 the four scripts agree:
  `for (var x of { next() {} })` and `[...{ next() {} }]` throw `TypeError`
  in both, `Array.from({ next() {} })` is `[]` in both (which is correct: it is
  read by `length`), and `for (var x of {})` throws in both. The fallback the
  row describes was removed before this phase; nothing in this change touches
  `js_iterate`. The row is still in the table, and the one gap the probe did
  find next to it — `Object.getPrototypeOf(gen)[Symbol.iterator]` is
  `undefined` here where Node.js has the function — is not written up
  anywhere.
