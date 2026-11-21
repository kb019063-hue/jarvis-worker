# Phase 156 — a write to an array index asks the prototype for a setter

Task row: **a write to an index of an array does not ask the prototype for a
setter**, the second half of the Phase 116 section 4 row (`AGENTS.md`).
`src/js/JsValue.cpp` was named by no open pull request. Finding 161.

## 1. What was wrong

ECMAScript §10.1.9.2, `OrdinarySetWithOwnDescriptor`: when an object has no own
property at a key, the write does not simply make one. The prototype chain is
asked first, and a setter found there runs with the object as `this` and the
property is never created. An array index is a key like any other, so this
program writes nothing:

```js
Object.defineProperty(Array.prototype, "0", {
  set(v) { log.push(v) },
  get() { return "p" },
  configurable: true
});
var a = [];
a[0] = 5;
// a browser: log is [5], a[0] is "p", a.hasOwnProperty(0) is false, a.length is 0
// here before this phase: log is [], a[0] is 5, a.hasOwnProperty(0) is true, a.length is 1
```

The element path in `JsValue::set_property` went straight to
`array_elements[index] = val` whenever the place was a hole or past the end —
exactly the two cases where the chain has the say — and the prototype walk
exists only on the ordinary-property path below it. Two test262 cases stated
the rule and failed: `Array/prototype/sort/precise-prototype-accessors.js`
(`Expected SameValue(«undefined», «"set with 3"»)` — a sort's write to a hole
is a setter on `Array.prototype`) and
`Array/prototype/unshift/set-length-array-is-frozen.js`.

## 2. What changed

`src/js/JsValue.cpp`, in `set_property`:

- **The chain is asked when the array has no own property there** — a hole, or
  an index past the end — which is the only case the standard gives it a say
  in. An element the array already holds is written without a walk, so
  `a[0] = 9` on `[1,2,3]` still writes `9` and never asks.
- **A setter runs with the array as `this`** and the place stays a hole; a
  setter that throws leaves the array untouched; an accessor with no setter
  refuses the write, which is a `TypeError` in strict code.
- **A *data* property on the prototype is deliberately left to the ordinary
  element write.** Its attributes cannot be read here: an index on an array is
  a value in `array_elements` and nothing else, so the value and its flags are
  not separable — that is the neighbouring open row, *a data property on an
  index of an array has no attributes of its own*. Answering from a value
  whose flags are unknown would be a guess, and it would let a call to
  `Object.freeze(Array.prototype)` change the meaning of every later index
  write. It waits for that row rather than approximating it.

### What it costs

The question is on the path of every `a[i] = v`, so it is gated on a counter:
`g_index_named_accessors`, bumped in `define_accessor` for every property whose
name begins with a digit, beside the `g_index_named_keys` counter that
`js_array_holes_read_undefined` already keeps. An ordinary program has no
index-named accessor anywhere, so the whole of it is one relaxed atomic load
and a compare per element write.

```bash
cat > /tmp/cost.js <<'JS'
var a = [];
var t0 = performance.now();
for (var i = 0; i < 3000000; i++) { a[0] = i; }
console.log(Math.round(performance.now() - t0) + ' ms');
JS
./build/litebird --run-script /tmp/cost.js /tmp/probe.html
#   before (main at 4b3491a): 860 / 895 ms
#   after:                    873 / 914 ms
```

## 3. How it was verified

The suite is `a_write_to_an_array_index_asks_the_prototype_for_a_setter`, in
`tests/test_suite.cpp`: nine checks, every expected value recorded by running
the same script through Node.js v22.22.3 first. It asserts the setter runs and
the array keeps nothing (`["p", false, 0, [5]]`), that an own element is
written without asking, that a place past the end of `new Array(7)` is asked
about, that a throwing setter leaves the array alone, that an accessor with no
setter refuses and throws in strict code, that an ordinary object's prototype
answers the same way, that a setter two links up answers, that a hole is a
place the chain answers for, and that nothing on the chain means the element
is made.

```bash
make test                     # All 314 engine test suites passed successfully!
make js-conformance           # 1144/1158 (98.8%), 29 files, 0 crashes, 0 timeouts
make conformance              # 1973/1973 (100.0%)
make gc-oracle                # PASS — cycles reclaimed, live set flat
```

test262, two builds of `built-ins/Array` (3,083 tests):

```bash
python3 tools/conformance/run_test262.py \
    --binary before=/tmp/before2/build/litebird --binary now=build/litebird built-ins/Array
#   before  2662 pass (86.3%)  [fail 421]
#   now     2664 pass (86.4%)  [fail 419]
#   2 pass in now and not in before; 0 pass in before and not in now
```

The two gained are the two named above —
`sort/precise-prototype-accessors.js` and
`unshift/set-length-array-is-frozen.js` — and nothing was lost. `language`
(19,912 / 23,015) and `built-ins/Object` (3,018 / 3,411) are identical on both
builds: 0 gained, 0 lost.

## 4. What is still open

- **A data property on a prototype index.** A non-writable one should refuse
  the write (`Object.defineProperty(Array.prototype, "1", { value: 7 })` then
  `b[1] = 9` leaves `b` empty in Node) and a writable one should be written in
  the prototype's own place. Neither can be answered until an index's
  attributes are stored, which is the row *a data property on an index of an
  array has no attributes of its own*.
- **`sort` still does not do its `Get`, `Set` and `DeleteProperty` in the
  standard's order**: `precise-comparefn-throws.js` and
  `precise-prototype-element.js` fail on their own, and `sort` now passes the
  accessor case only because the write it makes is the standard's write.
- **The counters are monotonic.** An accessor that is deleted leaves
  `g_index_named_accessors` where it was, so the walk is still attempted after
  the accessor is gone; it finds nothing and costs a chain walk per element
  write that misses. It is not reset because a reset would need a moment when
  no thread is running script.
