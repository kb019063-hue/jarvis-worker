# Phase 108 — an accessor on an array index is the index's property

Task row: **An array index the prototype has is one `HasProperty` finds, but a
descriptor on an array's own index is not used.** Part (1) of what that row
left open. Finding 114.

## 1. What was wrong

An array keeps its elements in `array_elements` and everything else in the
property map. `Object.defineProperty(a, "1", { get() { … } })` put the accessor
in the map, and every question about an index of an array was answered from the
elements before the map was looked at. So the accessor was there and was never
used:

```js
var a = [1, 2, 3];
Object.defineProperty(a, "1", { get: function () { return 7; } });
a[1]            // 7 (the standard); this engine answered 2
a.join()        // "1,7,3"; this engine answered "1,2,3"

var seen = [];
var b = [1, 2, 3];
Object.defineProperty(b, "1", { set: function (v) { seen.push(v); } });
b[1] = "x";
seen            // ["x"]; this engine answered [], and b[1] was "x"
```

That was so before Phase 81. Since Phase 81 made a hole a hole, the same
definition on a *hole* was not even an own property, and that is the form four
test262 tests have. They define a setter and no getter on a hole and then ask
`reduce`, `reduceRight` or `lastIndexOf` about it:

```js
var a = [, 1];
Object.defineProperty(a, "0", { set: function () {}, configurable: true });
a.hasOwnProperty(0)        // true; this engine answered false
a.lastIndexOf(undefined)   // 0; this engine answered -1
```

They are the four of the 25 tests Phase 81 lost that Phase 98 did not bring
back: `lastIndexOf/15.4.4.15-8-b-i-20.js`, `reduce/15.4.4.21-8-b-iii-1-18.js`,
`reduce/15.4.4.21-9-c-i-18.js` and `reduceRight/15.4.4.22-9-c-i-18.js`, all
under `built-ins/Array/prototype/`.

Around it, in the same code:

- An accessor defined past the end did not make the array longer: `length`
  stayed `1` after `Object.defineProperty([1], "3", { get … })`.
- A shorter `length` did not take the accessor with it, so it was there again
  when the array grew back; and one that is not configurable did not stop the
  array from being cut below it (`length = 0` left `0` where the standard
  leaves the index + 1).
- `Object.keys` listed the index twice, once for the element and once for the
  accessor.
- `join`, `includes`, `at`, `pop`, `shift`, the iterator (`for…of`, spread),
  `JSON.stringify` and the methods that copy (`slice`, `concat`, `flat`,
  `toReversed`, `toSorted`, `toSpliced`, `with`) read `array_elements` and did
  not ask what `a[i]` asks. That is the same fault one step out: they did not
  see an index `Array.prototype` has either, which Phase 98 had made `a[i]`
  and the callback methods see.

## 2. What changed

**The accessor makes the place a hole.** The elements hold values and nothing
else, so the accessor stays in the property map, and
`JsValue::define_accessor` marks the place a hole. A hole is where every
question about an index goes on from the elements, so the hole is what makes
the accessor the index's property. Past the end, the array grows to the index
and the places in between are holes.

- **`src/js/JsValue.cpp`**
  - `define_accessor`: the above. An index more than 2^24 places past the end
    is a `RangeError`, as a write that far past the end already was (an array
    has no sparse form here, so the places in between are all made).
  - `has_own_property`, `set_property`, the descriptor and `own_keys` ask the
    map for a hole (`get_property` went on to it already). A write calls the
    setter, writes a data property the map has if it is writable, and is
    refused otherwise. `own_keys` lists a mapped index where its number puts
    it, among the elements, once. `has_own_property` looks in the map only for
    an array that has had an accessor defined on it, so no other array pays.
  - A shorter `length` erases the mapped indices it cuts. One that is not
    configurable stops it at that index + 1 and the write is refused, which is
    the standard's `ArraySetLength`.
  - `js_array_get(array, i)`: what `a[i]` reads, the element when there is one
    and otherwise what the map and the prototype have.
    `js_array_holes_read_undefined(array)`: true when no hole of the array
    can read as anything, which is nearly always, and then a hole is
    `undefined` without being looked up by name. It looks at the array's own
    flags and at each prototype's every time, and remembers the last chain
    whose property names it found clean together with a count,
    `g_index_named_keys`, that `PropertyMap::entry` raises whenever a name
    beginning with a digit enters any map. The same chain at the same count
    has the same answer.
- **`include/litebird/js/JsOps.hpp`** declares the two.
- **`src/js/BuiltinsObject.cpp`**, `define_from_descriptor`: an accessor on an
  element starts enumerable and configurable, as the element was; a sealed or
  frozen array refuses; an array that takes no new properties lets an element
  become an accessor and refuses one past the end; a data descriptor takes
  the dense shortcut unless the map has the index.
- **`src/js/BuiltinsArray.cpp`**: `join`, `includes`, `at`, `pop`, `shift`
  read through `js_array_get`. `pop` takes the accessor with the place
  (`TypeError` if it is not configurable). `read_through()` gives the methods
  that copy an array in which each hole that reads as something is an
  element. The length is taken once, as the standard takes it, by `indexOf`,
  `includes`, `join` and the methods that copy, because reading a place can
  now run a getter that changes it. `with` does not read the index it
  replaces and `toSpliced` does not read the places it drops, and both ask
  what their arguments are after taking the length and once.
- **`src/js/JsOps.cpp`** (`visit_elements`: `for…of`, spread, `Array.from`)
  and **`src/js/BuiltinsJson.cpp`** (`JSON.stringify`) read through
  `js_array_get`. `JSON.stringify` now hands a hole to a replacer function,
  as the standard does: `JSON.stringify([, 1], (k, v) => v === undefined ? "x" : v)`
  is `["x",1]` where it was `[null,1]`.
- **`tests/test_suite.cpp`**: `an_accessor_on_an_array_index_is_its_property`,
  58 cases in ten groups. Each case was run through Node.js 22 first and its
  expected text is what Node.js printed.

## 3. How it is verified

The measurements are of `main` at `01bbf07` with this change, against a
build of `main` at that commit. `main` moved once more before the merge, by
Phase 109 (bidi), which changes nothing of the script engine; the unit suites
and the two corpora were run again on the tree with it.

- **The 25 tests Phase 81 lost pass**, each run alone with the harness
  (`assert.js`, `sta.js`, `propertyHelper.js`, `compareArray.js`). 21 passed
  since Phase 98; the four above failed before this change and pass now.
- **The whole of test262, two builds**
  (`python3 tools/conformance/run_test262.py --binary main=… --binary fix=… built-ins language annexB`,
  47,967 tests): `main` **31,366** passing, this change **31,643**. 277
  pass that did not, and none that passed fails. 163 of them are under
  `built-ins/Array/prototype` and 108 under `built-ins/Object`
  (`defineProperty` and `defineProperties` on an index of an array). The
  other six are a reviver of `JSON.parse`, a `for…of` over an array and the
  constructors of `Map` and `WeakMap`, each meeting a getter on an index that
  throws, and an accessor on an index of `arguments`. Two of them, the two of
  `Map`, do not fail on `main` but never end.
- **`make test`** → **269 / 269** suites and 114 cross-origin checks.
- **`make js-conformance`** 1141 / 1158 and 90 / 90 everyday,
  **`make dom-conformance`** 678 / 678: neither moved.
- **The new suite fails without the change**: 48 of its 58 cases fail when
  they are run through a build of `main`.
- **Time.** For arrays, 24 loops over an array of 200,000 places. The
  machine ran at two speeds while this was measured (one loop took 20 ms or
  40 from one process to the next, with either build), so each figure is the
  lowest of five runs of the script, which is 25 runs of the loop, in
  milliseconds:

  | loop | `main` | this change |
  | :--- | :--- | :--- |
  | index read, dense | 57 | 56 |
  | index write, dense | 84 | 82 |
  | index read, sparse | 73 | 72 |
  | index write into holes | 90 | 89 |
  | join, dense | 10 | 10 |
  | join, all holes | 26 | 29 |
  | join, sparse | 1 | 1 |
  | for-of, dense | 31 | 30 |
  | for-of, sparse | 17 | 19 |
  | spread, dense | 33 | 35 |
  | spread, sparse | 29 | 33 |
  | includes, dense, absent | 4 | 5 |
  | includes, sparse, absent | 1 | 1 |
  | slice, dense | 9 | 10 |
  | slice, sparse | 21 | 22 |
  | concat, sparse | 78 | 88 |
  | JSON.stringify, dense | 14 | 12 |
  | JSON.stringify, sparse | 3 | 8 |
  | pop until empty, dense | 84 | 85 |
  | pop until empty, holes | 96 | 104 |
  | at, dense | 106 | 103 |
  | hasOwnProperty, sparse | 90 | 82 |
  | Object.keys, sparse | 1 | 0 |
  | forEach, sparse | 20 | 19 |

  "Sparse" is one element in a hundred, the rest holes. What a hole costs
  more is the question whether it can read as anything, which the iterator
  asks again for each one, because in a `for…of` a script runs between two
  of them: 10 to 20 ns, which is `for…of` 17 → 19 and spread 29 → 33 over
  the sparse array. `JSON.stringify`
  of it, 3 → 8, also takes each hole the way it takes an element now, so that
  a replacer function is asked about it. `join` of holes is 26 → 29. The 10
  and 8 ms of `concat` and of `pop` of holes are not in other runs of those
  two loops (`concat`: 85, 93, 103 against 75, 88, 89; `pop`: 92, 103, 116
  against 88, 102, 104), and the rest differ by less than runs do.
  Two things did cost more before they were looked at. `for…of`, spread and
  `JSON.stringify` over the sparse array took 40, 41 and 25 ms while each
  hole was looked up by name in `Array.prototype` and `Object.prototype`,
  which is why the chain is remembered. And `join` of the holes took 37 ms
  while `join_array` made an empty value for every place before it asked
  whether the place was a hole.
- **Under the sanitizers** (`make asan-build`, then `build-asan/litebird`),
  on the head of #134, which is this change before `main` moved under it by
  four merges that touch none of its files, and before `join` was reordered:
  the suite's 58 cases, the nine scripts this was compared with Node.js by,
  and 1,231 test262 tests, each run alone, of the sixteen methods of
  `Array.prototype` this touches (the tests that give a method an object
  with a `length` near 2^32 or 2^53 left out). No report from either
  sanitizer. CI's four sanitizer jobs run the unit suites and the corpora on
  the merged tree.

## 4. What is still open

Each of these is a row of `AGENTS.md`. None is new with this change, except
that two of them answer differently than they did, as said below.

- **The methods that move elements in place** (`reverse`, `sort`, `shift`,
  `unshift`, `splice`, `fill`, `copyWithin`) move and write the places, and
  not what an accessor makes of them. `[1, 2, 3]` with an accessor on index 0
  whose getter returns `"g"` and whose setter does nothing: after `reverse()`,
  Node.js has `a[0]` and `a[2]` both `"g"`; this engine has `3` and
  `undefined`. Before this change the accessor was ignored and the answer was
  `3` and `1`, so the answer is wrong in a different way than it was.
- **`toReversed` and `toSorted` keep a hole a hole**, where the standard
  writes `undefined` to every place (`1 in [1, , 3].toReversed()` is `true`).
  `toReversed` reads from the first place up, where the standard reads from
  the last down, which a getter can tell.
- **Array destructuring reads the elements** (`IterationCursor::pull` in
  `src/js/JsEval.cpp`): `var [x] = a` with an accessor on index 0 binds
  `undefined`, where it bound the element under the accessor before and
  Node.js binds what the getter returns; `for (x of a)` and `[...a]` give
  what the getter returns now. It is one line, `js_array_get` in place of
  `array_element`, in a file an open pull request (#129) named.
- **`length` is not inherited from an array prototype**:
  `Object.create([7, 8, 9]).length` is `undefined`. Part (2) of the row this
  phase was taken from.
- **A data property on an index of an array keeps no attributes**:
  `Object.defineProperty(a, "0", { value: 1, writable: false })` leaves the
  index writable. The property map with the place a hole, which is where an
  accessor is kept now, is where such a property could be kept too.
- **`delete` of a property that is not configurable does not throw in strict
  code**, for any object, and `delete a.length` answers `true`.
- **`Reflect.getOwnPropertyDescriptor`, `Reflect.isExtensible`,
  `Reflect.preventExtensions` and the four `__defineGetter__` relatives do
  not exist.**
- **An array method called on an object with a huge `length` copies the
  object first** and runs out of memory. Tests that do it are why a test262
  run with two jobs can report a neighbouring test as timed out or killed.
