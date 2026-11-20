# Finding 114 — an accessor on an array index was never used

Phase 108. The architecture is in
[`docs/phases/108-an-accessor-on-an-array-index.md`](../phases/108-an-accessor-on-an-array-index.md).

## 1. What was wrong

`Object.defineProperty` on an index of an array stored an accessor in the
property map, and the array answered every question about an index from its
elements before it looked there. The getter was never called and the setter
was never called:

```js
var a = [1, 2, 3];
Object.defineProperty(a, "1", { get: function () { return 7; } });
a[1]                       // 7; this engine answered 2

var b = [, 1];
Object.defineProperty(b, "0", { set: function () {}, configurable: true });
b.hasOwnProperty(0)        // true; this engine answered false
b.lastIndexOf(undefined)   // 0; this engine answered -1
```

The first is older than Phase 81. The second is what Phase 81 turned it into
when it made a hole a hole, and it is the form of four of the 25 test262 tests
that phase lost. Phase 98 brought back the other 21.

How it was found: the 25 were listed by running the whole of test262 through
the build before Phase 81's merge and the build after it, and each was then
run alone on `main`. Four still failed, and all four define a setter on a hole.

## 2. What changed

`JsValue::define_accessor` makes the place a hole and leaves the accessor in
the property map, and a hole is where `has_own_property`, a write, the
descriptor and the list of keys ask the map. The methods that read a place
without asking (`join`, `includes`, `at`, `pop`, `shift`, the iterator,
`JSON.stringify`, and the methods that copy) read it as `a[i]` does, through
`js_array_get`. A shorter `length` takes the mapped indices it cuts, and stops
at one that is not configurable. The phase write-up has the files.

## 3. How it is verified

- The 25 tests pass, each run alone.
- The whole of test262 with a build of `main` and a build of this change, the
  same tree otherwise: @T262_LINE@
- `make test` 267 / 267, `make js-conformance` 1137 / 1156,
  `make dom-conformance` 678 / 678.
- The new suite's 58 cases have the answers Node.js 22 printed; 44 of them
  fail on a build of `main`.

## 4. Three things the first form of the change got wrong

They are here because each passed the 25 tests, the unit suites and the two
corpora, and was found by something else.

**Six tests that passed stopped passing.** The first whole run of test262 had
273 tests gained and six lost. Reading a place can now run a getter, and a
getter can change the array. Five of the six define one that makes the array
longer, and expect what the standard says: the length is taken once, before the first
place is read (`indexOf/15.4.4.14-9-11.js`, and
`length-increased-while-iterating.js` for `toReversed`, `toSorted`,
`toSpliced` and `with`). The sixth, `with/no-get-replaced-index.js`, expects
the index that is replaced not to be read. All six had passed because no
getter was ever called. `indexOf`, `includes`, `join` and the methods that
copy now take the length once; `with` and `toSpliced` do not read what they
replace or drop.

**A hole cost a lookup by name.** Timing 24 loops over an array of 200,000
places, 99% of them holes, `for…of` went from 17 ms to 40, spread from 23 to
41 and `JSON.stringify` from 3 to 25: every hole was asked for by name in
`Array.prototype` and in `Object.prototype`. Now
`js_array_holes_read_undefined` remembers the chain whose names it last found
clean, and a count that goes up whenever a name beginning with a digit enters
any property map; the same chain at the same count has the same answer. The
three are back at 18, 22 and 7 ms (the 7 is each hole going to the replacer
path, which is what the standard does with it).

**`toSpliced` read the places it drops.** With a getter on a dropped index,
Node.js does not call it; `main` did not either, because it called no getter
at all; the first form of this change did. No test262 test for an array says
so (the one that does uses an object), so it was found by comparing with
Node.js line by line.

## 5. What is not here

- The methods that move elements in place, `toReversed` and `toSorted`
  keeping holes, and array destructuring: the phase write-up, section 4, and
  the rows of `AGENTS.md`.
- `test262_whole_suite` in `docs/metrics.json` is not changed by this pull
  request, because two open pull requests (#128 and #132) had the file first.
  The figure there is `main`'s before this change; whichever of them merges
  after it measures again.
