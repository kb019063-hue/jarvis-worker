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

- **The 25 tests Phase 81 lost pass**, each run alone with the harness
  (`assert.js`, `sta.js`, `propertyHelper.js`, `compareArray.js`). 21 passed
  since Phase 98; the four above failed before this change and pass now.
- **The whole of test262, two builds of the same tree**
  (`python3 tools/conformance/run_test262.py --binary main=… --binary fix=… built-ins language annexB`,
  47,967 tests): `main` @T262_MAIN@, this change @T262_FIX@. @T262_GAINED@
  pass that did not, and none that passed fails. @T262_WHERE@
- **`make test`** → **267 / 267** suites and 114 cross-origin checks.
- **`make js-conformance`** 1137 / 1156 and 90 / 90 everyday,
  **`make dom-conformance`** 678 / 678: none moved.
- **The new suite fails without the change**: its 58 cases run through a
  build of `main` fail in 44.
- **Time** (`make bench` is strings; this is 24 loops over an array of 200,000
  places, best of five runs, each build run twice, in milliseconds):

  | loop | `main` | this change |
  | :--- | :--- | :--- |
@BENCH_ROWS@

  Nothing moved but `JSON.stringify` of an array that is 99% holes, which now
  asks the replacer path about each hole: 25 ns a hole. Before the chain was
  remembered, `for…of`, spread and `JSON.stringify` over that array took 40,
  41 and 25 ms, because each hole was looked up by name in `Array.prototype`
  and `Object.prototype`.
- **Under the sanitizers** (`make asan-build`, then the suite's cases, the
  probes and `make asan-js`): @ASAN@

## 4. What is still open

Each of these is a row of `AGENTS.md`, and none is new with this change except
where it says so.

- **The methods that move elements in place** (`reverse`, `sort`, `shift`,
  `unshift`, `splice`, `fill`, `copyWithin`) move and write the places, and
  not what an accessor makes of them. `[1, 2, 3]` with an accessor on index 0
  that returns `"g"`: after `reverse()`, Node.js has `a[0]` and `a[2]` both
  `"g"`; this engine has `3` and `undefined`. Before this change the accessor
  was ignored and the answer was `3` and `1`, so the answer is wrong in a
  different way than it was.
- **`toReversed` and `toSorted` keep a hole a hole**, where the standard
  writes `undefined` to every place (`1 in [1, , 3].toReversed()` is `true`).
  `toReversed` reads from the first place up, where the standard reads from
  the last down, which a getter can tell.
- **Array destructuring reads the elements** (`IterationCursor` in
  `JsEval.cpp`, the row about destructuring and the iterator): `var [x] = a`
  with an accessor on index 0 is `undefined`, where it was the element under
  the accessor and Node.js has what the getter returns.
- **`length` is not inherited from an array prototype**:
  `Object.create([7, 8, 9]).length` is `undefined`. Part (2) of the row.
- **`delete` of a property that is not configurable does not throw in strict
  code**, for any object.
- **`Reflect.getOwnPropertyDescriptor`, `Reflect.isExtensible`,
  `Reflect.preventExtensions` and the four `__defineGetter__` relatives do
  not exist.**
