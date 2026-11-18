# Phase 63 Architecture: Iteration Asks the Value for Its Iterator

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 63 Architecture: Iteration Asks the Value for Its Iterator` section.
> The text below is the section as it stood, unchanged.


### 1. What was wrong

`for (x of v)`, `[...v]`, `f(...v)`, `yield* v`, `Array.from(v)`, `new Set(v)`
and `Promise.all(v)` are one operation in the language: ask `v` for the
function under `Symbol.iterator`, call it, and call `next()` on what comes
back until it says `done`. In the engine they all end in two functions of
`src/js/JsOps.cpp`, `js_iterate` and `js_iterable_to_vector`, and both began
with a shortcut. An array was not asked: its elements were read by index. A
string was not asked either: its code points were read.

For an array that nobody has touched that is the same thing, faster. It stops
being the same when a script says otherwise, and a script can say so in three
places:

```js
var a = [1, 2];
a[Symbol.iterator] = function* () { yield 9; yield 8; };       // on the array
class Rows extends Array { *[Symbol.iterator]() { /* … */ } }  // on a subclass
Array.prototype[Symbol.iterator] = wrapped;                    // on the prototype
[...a]            // 1,2 on main, whichever of the three; 9,8 in Node for the first
```

AUDIT finding 51 had met it from the other side: Phase 45 made `Promise.all`
resolve each element before it asks for the next, for every iterable but the
one that is passed to it most.

The forty lines around the shortcut were also wrong in four smaller ways:

- An object that has a `next` method and no `Symbol.iterator` was taken to be
  its own iterator. So `[...asyncGenerator()]` did not throw: it called
  `next()`, got a promise, found no `done` on it, and went on for ever.
- A property whose name is the text `Symbol.iterator` was accepted as the
  method.
- A number or a boolean was refused without being asked. A script can give
  `Number.prototype` an iterator.
- When a loop stopped early, `return` was called if it was a function and
  left alone if it was anything else, and what it gave back was not looked
  at. The language has a `TypeError` for a `return` that is neither callable
  nor missing, and for a result that is not an object.

### 2. One question, and an answer that runs nothing

The question is `js_iterator_method(value, method)`, the language's
GetMethod(value, @@iterator): one property read, so that a getter runs once;
no method when the property is undefined or null; a `TypeError` when it is
anything else that cannot be called. A primitive is asked through its
prototype, with itself as the receiver.

The shortcut stays, behind a second function,
`js_iterates_as_builtin(value)`: true when reading the value directly gives
what iterating it would, *and finding that out ran no script*. For an array
it is the lookup `value[Symbol.iterator]` would make, taken one step at a
time and given up at the first step that is not the plain one:

1. the cell is not a host object and has no legacy getters (those answer
   before the stored properties do);
2. its prototype is this realm's `Array.prototype`: not a subclass, not an
   array of another frame, not one whose prototype was set;
3. it has no property of its own under the key (the table is searched, the
   property is not read, so a getter there does not run);
4. `Array.prototype` holds, under the key, a data property whose value is the
   function the realm put there.

For a string it is step 4 on `String.prototype`, since a primitive has no
properties of its own. "The function the realm put there" is remembered by
`JsIntrinsics::install()` in two new members, `array_iterator_method` and
`string_iterator_method`, which the realm reports to the collector as roots:
a script that replaces the method on the prototype must not be able to make
the original unreachable and so let another function take its address.

When the answer is no, the value is asked with `js_iterator_method`. If what
it hands out is the installed function after all (a subclass that has no
iterator of its own, a getter that returns the built-in one, the built-in one
assigned back), the elements are still read directly. Otherwise the protocol
runs.

**Why reading directly is the same.** The installed array iterator reads
`elements[i]` against the length the array has at that moment, each time
`next()` is called, and its `next` is a property of the iterator object
itself: no function a script can reach stands between the method and the
elements. `visit_elements` reads the same way, so an array that grows or
shrinks under the loop is seen as the iterator would see it. The day array
iterators take `next` from a prototype they share, as the language has it,
that function has to be compared too; the comment in
`js_iterates_as_builtin` says so, and so does the row.

**Realms.** Every realm installs functions of its own. An array made in
another frame has that frame's `Array.prototype`, so from here it is not the
plain case: it is asked, its own realm's iterator is called, and the elements
come through the protocol. Slower, and right:
`js_iteration_of_another_realms_array` replaces the iterator in the frame
and in the top document in turn and checks that each array follows its own.

### 3. The protocol

`iterate_with` is GetIterator and the loop over IteratorStep with the method
already in hand:

- the method is called with the value as `this` (for a string the string
  itself, not an object made from it) and no arguments, and what it returns
  has to be an object;
- `next` is read once; each result has to be an object; `done` is read
  before `value`, and `value` is not read when the iterator is done;
- a loop the consumer ends (`break`, `return`, a visitor that answers false)
  closes the iterator with what the language calls IteratorClose: a missing
  `return` is nothing, one that cannot be called is a `TypeError`, and so is
  a result that is not an object;
- a loop that an error ends closes it too, and that error is the one the
  script sees, whatever `return` is, does or throws;
- an iterator that fails itself (`next()` throws, a result is not an object,
  `done` or `value` throws) is not closed.

### 4. What does not change

The plain case runs the code it ran before, after the check. An array that
changes under the loop, holes, strings by code point: the corpus has them
beside the new cases, and they pass on `main` and here. `js_iterable_to_vector`
still copies a plain array in one piece.

What never asked for an iterator still does not: `apply`, `Reflect.apply`,
`Reflect.construct`, `bind`, rest parameters, template objects and the array
methods read their arrays by index, as the language has it. Two corpus cases
put a counting function on `Array.prototype` and expect it to count nothing.

### 5. Verification and cost

Every number here is printed by the command beside it, on this branch with
`main` at `b1745d3` (Phase 60) merged in. "Before" is the release binary of
that `main`.

| Command | Before | Now |
| :--- | :--- | :--- |
| `make test` | 204 suites | `All 206 engine test suites passed successfully!` and `Cross-origin Window/Location suite: 80 checks, 0 failure(s).` |
| `make js-conformance` | 1020 / 1034 | `JS conformance, full corpus: 1085/1112 (97.6%)  [28 files, 0 crashes, 0 timeouts]` |
| `python3 tools/conformance/run_js_conformance.py --filter iteration` | 28 / 78, 2 timeouts | 65 / 78 |
| the same with `--reference "node -e"`: the file itself, run by Node | | 78 / 78 |
| `make dom-conformance`; `make wpt` | 669 / 669; 9538 / 9744 | the same |
| `python3 tools/conformance/compare_with_jsdom.py` on a page that is in no corpus (below) | 17 lines, 9 the same | 17 lines, 17 the same |

**The corpus.** `tests/js-conformance/iteration.dat` has 78 cases. Each was
added through `tools/conformance/add_js_case.py`, which runs the script in
Node and stores what Node printed, so no expectation was typed in. They
cover the three places a script can put an iterator (the array, a subclass,
the prototype) through each consumer (`for…of`, spread in a call and in an
array, `yield*`, `Array.from`, `Map`, `Set`, `Object.fromEntries`, the
`Promise` combinators), the same for strings, the protocol's order of reads,
closing, and the plain cases that must not change. The 13 that fail are the
cases of section 6, and they are in the file so that whoever takes one of
those rows has the test already.

**The unit suites.** `js_iteration_asks_the_value_for_its_iterator` calls the
four C++ functions directly, which a script cannot: that
`js_iterates_as_builtin` runs no getter (a counter stays at 0), that a
subclass without an iterator is asked and then read directly, that the plain
case comes back when the prototype's method is put back, that an early stop
closes the iterator once, and that the two remembered functions survive a
collection while a replacement sits on the prototype.
`js_iteration_of_another_realms_array` loads a page with a frame and
iterates the frame's arrays from the top document.

**test262**, the whole of `built-ins`, `language` and `annexB`
(`python3 tools/conformance/run_test262.py --binary before=… --binary now=…
--timeout 6 built-ins language annexB`): `47967 tests run, 830 skipped
(modules)`; before, `29444 pass (61.4%)`; now, `29450 pass (61.4%)`; `10 pass
in now and not in before; 4 pass in before and not in now`. Nine of the ten
are what the change is for: `for…of` and `yield*` asking
(`language/statements/for-of/dstr/{const,let,var}-ary-init-iter-get-err-array-prototype.js`,
`language/expressions/yield/star-in-rltn-expr.js`), the two checks of
IteratorClose (`for-of/iterator-close-non-object.js`,
`for-of/iterator-close-non-throw-get-method-non-callable.js`), an array
whose `Symbol.iterator` getter throws
(`built-ins/Promise/{allSettled,any}/iter-arg-is-poisoned.js`), and one of
async delegation (section 6, item 3). The tenth,
`built-ins/String/prototype/repeat/empty-string-returns-empty.js`, timed out
in the run of `main` and is the machine, as are 14 tests of `built-ins/Array`
and `built-ins/Map` that moved between crash, timeout and fail. The four
that are lost are the three of section 6, item 3, and the one of item 2.

The first run, earlier in the work, was over the 25 directories that are
about iteration, and it said eight gained and none lost. The four lost were
in directories that are not: three under `async-generator` and one under
`class/subclass`. A change to something this widely called is measured over
the whole suite or not at all.

**A page that is not in a corpus.** `js_iterable_to_vector` is also how the
DOM's bindings read a sequence argument: the pairs of `new
URLSearchParams(pairs)`, the parts of `new Blob(parts)`, a
`MutationObserver`'s `attributeFilter`, a custom element's
`observedAttributes`. A page of 17 lines hands each of them an array with an
iterator of its own, an instance of a subclass that has one, and plain
arrays while `Array.prototype[Symbol.iterator]` is replaced by a function
that counts. jsdom and this branch print the same 17 lines; `main` prints 9
of them. The page is not in the DOM corpus, which would move that corpus's
floors; the unit suite holds `js_iterable_to_vector` itself.

**The sanitizers.** `make asan` was run here on the tree before the last
merge of `main` (this branch with `73e23a2`). Its first part, `asan-core`,
passed whole: `JS conformance, full corpus: 1062/1092` under ASan against
1063 in the release build (the one case is the deep recursion that a
sanitizer build's stack frames are too large for, as on `main`), the DOM
corpus 669/669, web-platform-tests 7998/8009, and no finding from either
fuzzer. Its second part, the deep pages, was cut off when the sandbox was
suspended and was not repeated here. On the merged tree the sanitizer job
of CI is the whole run, deep pages included; the pull request names it.

**Cost.** The plain case pays for the check: three pointer comparisons, one
lookup in the array's own property table when it has one, and one lookup in
`Array.prototype`'s table. The first version asked with a full property read
every time, and a `for…of` over three elements took 1.16 of `main`'s time;
that is why the check is structural. Least time of 5 processes each, in
nanoseconds for one operation, twice over; beside `main`'s binary a second
copy of the same file, to show what the measure itself varies by:

| One operation | `main`, ns | this branch, ns | branch / `main`, two runs | second copy of `main` / `main`, two runs |
| :--- | ---: | ---: | :--- | :--- |
| `for (x of a)`, 3 elements | 707 | 723 | 1.02, 1.03 | 1.00, 1.01 |
| `for (x of a)`, 1,000 elements | 162,000 | 154,667 | 0.95, 0.97 | 0.98, 1.00 |
| `f(...a)`, 3 elements | 933 | 957 | 1.03, 1.03 | 1.00, 1.00 |
| `Math.max(...a)`, 1,000 elements | 37,333 | 38,000 | 1.02, 0.97 | 1.02, 0.95 |
| `[...a]`, 3 elements | 1,100 | 1,133 | 1.03, 1.02 | 1.02, 0.99 |
| `[...a]`, 1,000 elements | 45,500 | 46,000 | 1.01, 0.94 | 0.99, 0.97 |
| `Array.from(a)`, 3 elements | 800 | 847 | 1.06, 1.04 | 1.02, 0.99 |
| `new Set(a)`, 3 elements | 1,500 | 1,560 | 1.04, 1.03 | 0.99, 1.01 |
| `new Map(pairs)`, 3 pairs | 1,717 | 1,750 | 1.02, 1.00 | 0.98, 1.03 |
| `for (c of 'abc')` | 733 | 733 | 1.00, 1.00 | 1.00, 1.00 |
| `[...'abc']` | 713 | 753 | 1.06, 0.99 | 1.02, 0.98 |
| a generator that does `yield* a`, made and run to its end | 148,000 | 151,000 | 1.02, 1.01 | 1.03, 0.96 |
| `Promise.all(a)`, 3 elements | 4,000 | 4,200 | 1.05, 1.04 | 1.02, 1.03 |
| control: `var [x, y] = a` | 383 | 370 | 0.97, 0.97 | 1.00, 1.00 |
| `for (e of map)`, 3 entries (not an array: the protocol) | 24,000 | 24,000 | 1.00, 1.03 | 0.97, 1.03 |
| control: `for (i = 0; i < 3; i++)` | 1,307 | 1,263 | 0.97, 0.97 | 1.02, 0.99 |

The two lines marked "control" run none of the changed code. Over both runs
this branch is between 0.94 and 1.06 of `main`, and the second copy of
`main` between 0.95 and 1.03 of the first: what the check costs is at the
edge of what this machine can measure.

### 6. What is still open

Six things, each a row of AGENTS.md with its failing cases in
`iteration.dat`. The first four are in files that an open pull request held
while this was written: `BuiltinsArray.cpp` (#63 and #73), `JsParser.cpp`
and `JsEval.cpp` (#68, until it was merged at 03:58 on 17 November, an hour
and a half before this was). The fifth is in `BuiltinsCollections.cpp`,
which nobody held. The sixth is several small things; of the three files
its row names, #63 named two and nobody the third. The first three are where
this change made something **worse than it was on `main`**, in one case each
that a page is unlikely to meet and a test suite does meet. They come first
for that reason.

(Corrected on 18 November. As merged, this paragraph said that each of the
six was in a file an open pull request held. That was written from memory,
and it is true of the first four.)

1. **`Array.from`** (`BuiltinsArray.cpp`) decides what is iterable by
   `is_array()` and by type before it calls `js_iterable_to_vector`. With an
   iterator on the array it is right now, because that function asks. With
   the iterator *deleted* from `Array.prototype` it is a `TypeError`, where
   the language reads the array as array-like, and where `main` gave the
   elements because nothing was asked. It also does not ask a number, and
   maps only after it has collected everything.
2. **The default constructor of a subclass** is written by the parser as
   `constructor(...args) { super(...args); }`, and a spread now asks. A
   replaced `Array.prototype[Symbol.iterator]` is therefore called by
   `new Derived(...)`. The language's default constructor "does not use
   argument evaluation", in the words of test262's
   `default-constructor-spread-override.js`, which passed on `main` (where
   nothing asked an array) and fails here. The repair is a flag on the
   spread the parser makes (`JsParser.cpp`, `JsEval.cpp`).
3. **`yield*` in an async generator** (`eval_yield`, `JsEval.cpp`) runs the
   sync protocol and never asks for `Symbol.asyncIterator`. Delegating to
   another async generator never returned on `main`, through the bare
   iterator of section 1; it is a `TypeError` now. Neither is what the
   language says. Three test262 tests of async delegation passed only
   because their iterable happened to carry a `next` that answers without a
   promise, and fail now; a fourth, which failed, passes for the same kind
   of reason.
4. **Array destructuring** (`IterationCursor`, `JsEval.cpp`) has an array
   mode of its own: `var [x] = a` reads `a`'s elements whatever `a` says.
5. **The `Map` and `Set` constructors** do not call `set` and `add`
   (`BuiltinsCollections.cpp`; the row "A built-in does not make the subclass
   it was called on" already had it), so a replaced one is not called and
   cannot stop the iteration.
6. **What an iterator is, in small**: `Array.prototype[Symbol.iterator]` and
   `Array.prototype.values` are two functions; an array iterator owns its
   `next`; the built-in iterables also have a property named by the text
   `Symbol.iterator`, and the symbol is itself the text `@@iterator`;
   `arguments` has no iterator of its own; an object whose prototype is an
   array has no elements. (A seventh, a `String.prototype` method called on
   an array reading it as `[object Object]`, was repaired by Phase 60 while
   this was in work, and its case passes.)

### 7. Beside the task: the limit that failed the sanitizer job

The sanitizer job of CI failed on `main` at `73e23a2` and on the first
commit of #73, both on 17 November, with one web-platform-test page
reporting nothing:

```text
  encoding/legacy-mb-tchinese/big5/big5-decode-extra.html: (harness)
    ERROR: the harness reported nothing:  |
Web Platform Tests: 5526/5538 subtests pass in 103 files; 11 fail as listed, 1 fail and are not listed
FAIL: 5526 subtests pass, the floor is 7998
```

Those lines are an annotation of the failed job; the step that writes them
(`tools/ci/annotate_failure.py`, Phase 43) is why they can be read at all.
A commit that added nothing to that tree (the claim of #72) passed seven
minutes later, so nobody could have found the cause in a diff.

**What it was.** `EventLoop::run_until_idle()` has, beside its budget of
loop time, a limit on real time: 10 seconds by default, looked at between
turns. Past it the loop returns; the host said nothing about it and exited
with status 0. The page has 2,472 subtests behind `<body onload>` and an iframe, so its
load handlers run in the loop's first turn, and in the sanitizer build they
take 7.3 s on this sandbox when nothing else runs. On a busier machine they
take more than 10, the loop ends after that turn, and the turn that would
have run the tests and reported never starts. Reproduced here with four busy
loops beside the sanitizer binary: the load handlers took 18.8 s, and the
page ended with exit status 0 and nothing on standard output, as in CI.

**What changed**, in `src/main.cpp` and `tools/conformance/run_wpt.py`, and
nothing in the loop itself:

- `--wall-limit=SECONDS` sets that limit for the one-shot modes (`--run-js`,
  `--run-script`, rendering a page once). The default is still 10.
- When the limit is what ended the loop with callbacks still due, one line
  on standard error says so: `litebird: the event loop was stopped after 10
  s of real time with callbacks still due (--wall-limit=SECONDS allows
  more)`. The exit status is what it was.
- `run_wpt.py` passes its own timeout for a file (60 s; 600 s in the
  sanitizer run) as the limit. A page that is too slow is then stopped by
  the runner, which reports it as `TIMEOUT` under its name.

**Verified** with a page whose `onload` spins for 1.5 s behind an iframe and
then sets a timer that prints: with `--wall-limit=1` nothing is printed but
the line above, with the default the timer runs. `make wpt` prints the same
score with the flag as `main` does without it (9538/9744). It has no
automatic test: the limit is chosen in `main()`, which no suite calls.

**Not done.** The runners of the other corpora (`run_dom_conformance.py`,
`run_js_conformance.py`, `run_url_conformance.py`, `run_test262.py`) still
run under the default of 10 s. None has shown the failure; the line on
standard error is what will name it if one does. And whether the two
failures of the sanitizer job on `main` on 16 November were this one is not
known: they left nothing to read.

---
