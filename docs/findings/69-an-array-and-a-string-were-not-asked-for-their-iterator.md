# 69. ✅ An array and a string were not asked for their iterator (Phase 63)

> Moved out of `AUDIT.md` — written there as a `## 69. ✅ An array and a string were not asked for their iterator (Phase 63)` section.
> The text below is the section as it stood, unchanged.


**The hole.** `for (x of v)`, `[...v]`, `f(...v)`, `yield* v`, `Array.from(v)`,
`new Map(v)`, `new Set(v)`, `Object.fromEntries(v)` and `Promise.all(v)` are
one operation: ask `v` for the function under `Symbol.iterator`, call it,
and call `next()` on the result until it says `done`. Here all of them end
in `js_iterate` or `js_iterable_to_vector` (`src/js/JsOps.cpp`), and both
began with a shortcut: an array's elements were read by index and a string's
code points were read, without asking. Finding 51 had it under "Still open": the
array fast path ignores an array's overridden `Symbol.iterator`. The rest of
this list was found while fixing that.

| A script | Before | Now, and in Node |
| :--- | :--- | :--- |
| `var a = [1, 2]; a[Symbol.iterator] = function* () { yield 9; yield 8; };` then `[...a]` | `1,2` | `9,8` |
| the same array `a` given one that yields `['k', 'v']`: `for (x of a)`, `f(...a)`, `Array.from(a)`, `new Map(a).get('k')` | the elements; `undefined` | the pair; `v` |
| `class Rows extends Array { *[Symbol.iterator]() { /* last to first */ } }` holding `a, b, c`: `[...r]`, `Array.from(r)` | `a,b,c` | `c,b,a` |
| `Array.prototype[Symbol.iterator]` replaced by a function that counts and calls the original; then `[...[1, 2]]`, `for (x of [3])`, `Math.max(...[4])`: the count | `0` | `3` |
| `String.prototype[Symbol.iterator] = function* () { yield 'x'; }`: `[...'ab']`, `new Set('ab').size` | `a,b`; `2` | `x`; `1` |
| `Promise.all(a)` for an array `a` with an iterator that yields `'p'` | resolves with `1,2` | with `p` |
| `async function* g() { yield 1; }`: `[...g()]` | never returns | `TypeError` |
| `for (x of { next() { … } })`: an object with `next` and no `Symbol.iterator` | runs the loop | `TypeError` |
| `[...{ 'Symbol.iterator': function* () { yield 1; } }]`: a property named by the text | `1` | `TypeError` |
| `Number.prototype[Symbol.iterator] = function* () { yield +this; yield this + 1; }`: `[...5]` | `TypeError` | `5,6` |
| `for (x of it) break;` where the iterator's `return` is the number 5 | no error | `TypeError` |
| the same where `return()` gives back a number | no error | `TypeError` |
| `delete Array.prototype[Symbol.iterator]`: `[...[1, 2]]` | `1,2` | `TypeError` |

What it broke outside a test: a class that extends `Array` and defines how
it is iterated (a paged list, a sparse row set, a collection that yields
wrapped items) was iterated as the bare array by every construct of the
language; a library that instruments `Array.prototype[Symbol.iterator]` saw
nothing; and spreading an async generator, which is a mistake that should
throw, hung the page.

**The fix.** One question, `js_iterator_method(value, method)`, which is the
language's GetMethod(value, @@iterator), and the shortcut kept behind a
second one, `js_iterates_as_builtin(value)`, that answers without running
any script: the array is an ordinary one, its prototype is this realm's
`Array.prototype`, it has no property of its own under the key, and the
prototype still holds the function the realm installed (remembered in
`JsIntrinsics` and kept alive as a root). Anything else is asked; if what it
hands out is the installed function after all, the elements are still read
directly; otherwise the protocol runs, as the language writes it: `next`
read once, `done` before `value`, IteratorClose with its two checks when the
consumer stops early, the body's error kept when an error stops the loop,
and no `return()` for an iterator that failed itself. `src/js/JsOps.cpp` and
its header, and nothing else: every caller already went through the two
functions. ARCHITECTURE.md, Phase 63, has the design and why the direct read
is the same as iterating.

### What each instrument found

| Found by | What |
| :--- | :--- |
| A file of 40 short scripts, run in Node, in `main`'s binary and here | `main` printed what Node prints for 8 of the 40. Beside the shortcut, four things in the forty lines around it: an object with `next` was its own iterator, a property named by the text `Symbol.iterator` counted, a primitive other than a string was refused unasked, and `return` was not checked. All in the list above |
| Timing the first version against two copies of `main`'s binary | It asked with a property read on every loop, and `for…of` over three elements took 1.16 of `main`'s time, spread 1.11 to 1.13. The check that runs no script replaced it. ARCHITECTURE.md, Phase 63, section 5, has the table for the final code |
| test262 over the 25 directories about iteration | Eight more pass, none fewer. That was the wrong run to stop at |
| test262 over all of `built-ins`, `language` and `annexB` | Four tests that passed on `main` and fail here, in directories the first run had not looked at: three of async delegation (`language/{expressions,statements}/async-generator/yield-star-…`) and `language/statements/class/subclass/default-constructor-spread-override.js`. None can be repaired in the two files of this change: `yield*` and the default constructor are in `JsEval.cpp` and `JsParser.cpp`, which another open pull request held. They are the second and third item of the list below. Final run, on the merged tree: `10 pass in now and not in before; 4 pass in before and not in now`, of 47,967 |
| Reading every caller of the two functions | `IterationCursor` (array destructuring) has an array mode of its own and never calls them for an array; `Array.from` decides by `is_array()` before it calls; the `Map` and `Set` constructors do not call `set` and `add`. Each is a file another open pull request held: rows, with failing cases |
| Writing the case for an iterator deleted from `Array.prototype` | `Array.from([1, 2])` then is a `TypeError` here, where Node reads the array as array-like. On `main` it gave the elements, because nothing asked. Worse than before, in `BuiltinsArray.cpp`, a row |
| Probes with an iterator that never ends | `Array.from(endless, mapperThatThrows)` and `new Set(endless)` with an `add` that throws never return, on `main` and here: the first collects before it maps, the second never calls `add`. The corpus cases use iterators that end, and both are in the rows above |
| The benchmark, when one line of it did not finish | Iterating a `Map` or a `Set` gets slower every time it is done. `for (e of map)` over three entries, in microseconds a loop for each successive thousand loops: `12.3 24.6 45.7 66.4 90.5 111.4 129.2 154.1`; after `gc()`, `21.5`. A `Set`: `7.7` to `129.1`. `map.forEach` stays at `2.5` and `for…of` over an array at `1.1`. `CollectionData::new_cursor` (`BuiltinsCollections.cpp`) keeps a weak reference to the cursor of every iterator ever made on the collection and, from the 64th on, walks the whole list for each new one to drop the dead; none is dead until a collection, and there is none without `gc()`. Older than this change, not fixed, a row |
| A unit suite with a frame | An array of another realm has that realm's `Array.prototype`, whose iterator is another function than this realm's. It is asked, so it goes through the protocol: right, and slower than an array of the realm's own. Nothing to fix; section 2 of the phase says why |
| `tools/ci/check_pr.py`, written for AGENTS.md | Its first version asked the local clone whether `main` was in a commit, and a sandbox's clone is shallow: it said `main` had moved when it had not. It asks GitHub now, and nothing else |
| The sanitizer build | Nothing in this change: `make asan-core` here, on the tree before the last merge of `main`, ended with no report from either sanitizer (the unit suites, `JS conformance, full corpus: 1062/1092` against 1063 in the release build, the DOM corpus 669/669, web-platform-tests 7998/8009, both fuzzers). The deep pages after it were cut off when the sandbox was suspended: two pages "timed out" after 6,703 seconds of a clock that had kept running. CI's sanitizer job is the run on the merged tree |
| The annotations of CI's sanitizer job, the day it failed on `main` at `73e23a2` | Not this change, and not chance: a page of web-platform-tests "reported nothing" because the engine stops a page's callbacks after 10 seconds of real time, in silence and with exit status 0. That page's load handlers take 7.3 s in the sanitizer build on an idle machine; with four busy loops beside it they took 18.8 s here and the page ended as it had in CI. The limit can be set now (`--wall-limit`), the engine says so on standard error when it ends a run, and `run_wpt.py` passes its own timeout. ARCHITECTURE.md, Phase 63, section 7, has it; it was found with the step Phase 43 added to that job so that its failures could be read |
| Merging `main` for the fourth time | Phase 60 had meanwhile repaired one of the 14 cases that were recorded to fail (`String.prototype.trim.call(['x', 'y'])` read the array as `[object Object]`): 13 fail, and the row lost a clause. And #71 had been merged as Phase 56, finding 62, the numbers this change had claimed first and was written to: it is Phase 63, finding 69 |

### What is known to be wrong and is not fixed

Three things are **worse than they were on `main`**, each in a case a page
is unlikely to meet and a test suite does:

- **`Array.from([1, 2])` after `delete Array.prototype[Symbol.iterator]`** is
  a `TypeError`; on `main` it gave `[1, 2]` and so does Node. `Array.from`
  takes whatever `is_array()` for iterable and then calls the function that
  now asks. In `src/js/BuiltinsArray.cpp`.
- **`new Derived(1, 2)` for a class without a constructor calls a replaced
  `Array.prototype[Symbol.iterator]`**, because the parser writes the
  default constructor as `constructor(...args) { super(...args); }` and a
  spread now asks. The language's default constructor does not iterate.
  test262's `default-constructor-spread-override.js` passed and fails. In
  `src/js/JsParser.cpp` and `src/js/JsEval.cpp`.
- **`yield*` in an async generator, over an object that has only
  `Symbol.asyncIterator` and whose `next` answers without a promise**,
  worked by accident (the object was taken for its own sync iterator) and
  is a `TypeError`. Three tests of test262 passed that way and fail. Over
  another async generator `main` never returned, and that is a `TypeError`
  too; neither is right. In `src/js/JsEval.cpp` (`eval_yield`).

And three that are as they were:

- **Array destructuring** does not ask: `var [x] = a` reads `a`'s elements
  whatever `a[Symbol.iterator]` is. In `src/js/JsEval.cpp`.
- **The `Map` and `Set` constructors** do not call `set` and `add`, so a
  replaced one is not called and cannot stop the iteration. In
  `src/js/BuiltinsCollections.cpp`.
- **What an iterator is, in small**: `Array.prototype[Symbol.iterator]` and
  `Array.prototype.values` are two functions; an array iterator owns its
  `next`; `arguments` has no iterator of its own; an object whose prototype
  is an array has no elements; and four smaller things.

All six are rows of the task table in `AGENTS.md`, and 13 cases of
`iteration.dat` fail for them and say which.

### How verified

On this branch with `main` at `b1745d3` (Phase 60) merged in. "Before" is
the release binary of that `main`.

- `make test`: `All 206 engine test suites passed successfully!` (204
  before; the two new ones are
  `js_iteration_asks_the_value_for_its_iterator` and
  `js_iteration_of_another_realms_array`), and `Cross-origin Window/Location
  suite: 80 checks, 0 failure(s).`
- `make js-conformance`: `JS conformance, full corpus: 1085/1112 (97.6%)
  [28 files, 0 crashes, 0 timeouts]`; before, 1020/1034. `iteration.dat`
  alone (`run_js_conformance.py --filter iteration`): 65/78; `main`'s binary
  28/78 with two timeouts; Node 78/78 (`--reference "node -e"`).
- The 16 scripts behind the list at the top, in Node, in `main`'s binary and
  here: `main` prints what Node prints for 3, this branch for 16.
- test262, `python3 tools/conformance/run_test262.py --binary before=…
  --binary now=… --timeout 6 built-ins language annexB`: `47967 tests run,
  830 skipped (modules)`; before `29444 pass (61.4%)`, now `29450 pass
  (61.4%)`; `10 pass in now and not in before; 4 pass in before and not in
  now`.
- `python3 tools/conformance/compare_with_jsdom.py` on a page of 17 lines
  that hands arrays with their own iterators to `URLSearchParams`, `Blob`,
  a `MutationObserver`'s `attributeFilter` and `observedAttributes`: `17
  lines, 17 the same`; `main`'s binary, `17 lines, 9 the same`.
- Time, 13 operations and 3 controls, least of 5 processes each, twice:
  this branch is between 0.94 and 1.06 of `main`; a second copy of `main`'s
  binary is between 0.95 and 1.03 of the first. ARCHITECTURE.md, Phase 63,
  section 5, has the table.
- `make dom-conformance`: 669/669. `make wpt`: `9538/9744 subtests pass in
  190 files; 206 fail as listed, 0 fail and are not listed`.
- CI's JS floor is 1085 (1020 before) and `JS_ASAN_FLOOR` 1083 (1018
  before): each is `main`'s plus the 65 cases of `iteration.dat` that pass,
  which they do under ASan as in the release build. Neither was lowered.
- CI: run 44021433604 on `f2430f9`, the commit that holds the code, all
  seven jobs, the sanitizer job with the new floor among them. The commits
  after it change `*.md` only.
