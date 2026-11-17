# 41. ✅ A function could not be a prototype

> Moved out of `AUDIT.md` — written there as a `## 41. ✅ A function could not be a prototype` section.
> The text below is the section as it stood, unchanged.


**The hole.** A function is a value in this engine and an object is a heap
cell, and a prototype link is a pointer to a cell. Nothing led from a cell
back to a function, so nothing could have a function as its prototype and
give it back. `Object.getPrototypeOf(Derived)` for `class Derived extends
Base` was an object that was not `Base` and could not be called;
`Object.getPrototypeOf(TypeError)` was not `Error`; `Function.prototype` was
an object. A class kept its parent in a hidden property and a static member
was looked for one step up and no further. Finding 34 listed it as known and
not fixed, for the 18 web-platform-tests subtests that failed on it: every
`assert_throws_js` walks up from the error type it was given with
`Object.getPrototypeOf` until it reaches `Error`.

What it broke outside a test suite is the code most pages ship: a class
compiled for older browsers. The helpers Babel and TypeScript emit are made
of the calls in this table.

| A script | Before | Now, and in Node |
| :--- | :--- | :--- |
| `Object.getPrototypeOf(TypeError) === Error` | `false` | `true` |
| `class Derived extends Base {}`; `Object.getPrototypeOf(Derived) === Base` | `false` | `true` |
| `class C extends B {}`, `class B extends A {}`, `A` has `static s()`: `C.s()` | `TypeError: s is not a function` | runs |
| `typeof Function.prototype`; `Function.prototype()` | `object`; `TypeError` | `function`; `undefined` |
| `Object.getPrototypeOf(Object.create(f))()` | `TypeError: p is not a function` | what `f` returns |
| `Reflect.construct(F, [], G) instanceof G` | `false` | `true` |
| `F.prototype.m = …`, then `Object.defineProperty(F, 'prototype', { writable: false })`: how a compiled class ends | `TypeError: Cannot redefine property: prototype` | accepted |
| `Object.getPrototypeOf(Derived).call(this, x)` in a compiled constructor | `TypeError: Function.prototype.call called on a non-function` | calls `Base` |
| A class with a parent, compiled by Babel 7, by Babel's newer helpers, by TypeScript | stops at its first class, all three | prints what Node prints, all three |
| `Object.getPrototypeOf(HTMLElement) === Element`; `HTMLElement.ELEMENT_NODE` | `false`; `undefined` | `true`; `1` |

**The fix.** A function's cell can know the function it belongs to
(`JsValue::as_prototype()` gives it a copy; `JsValue::make_object(cell)`
gives the function back), every kind of function can have a cell, and the
collector takes the pair apart, since the cell holds the function and the
function holds the cell. The copy lives in a pointer the cell of a function
had no use for, so no object and no value grew. Classes are linked to their
parents, `super()` calls whatever the prototype is when it runs,
`Function.prototype` is a function, `Reflect.construct` honours its third
argument, `Object.defineProperty` follows the standard's
ValidateAndApplyPropertyDescriptor, and the DOM's interface objects have
their parents as prototypes. ARCHITECTURE.md, Phase 34, has the design.

### What each instrument found

The change was written with a new file of the JavaScript corpus beside it,
`prototypes.dat`, whose expected output is what Node prints. What follows was
found by something else.

| Found by | What |
| :--- | :--- |
| Two wrong collectors, built on purpose | With the function a cell stands for counted and not marked, a parent class reachable only as a prototype lost the variables it closed over, and every test passed: the cell keeps the function's name and properties, so checks of those see nothing. A test has to call the function it got back and read a variable it closed over. One suite and one corpus case do now |
| test262, run in the binary of `main` and in this one | Two tests that passed on `main` and failed here, of 15,686. `[] instanceof Function.prototype` with a `prototype` that is a string threw on `main` only because `Function.prototype` was not callable; `instanceof` never looked at what kind of thing `prototype` is. And `{ __proto__: function () {} }` named the function `__proto__`, on `main` as well, where the function could not be got back to see its name. Both fixed, both corpus cases now |
| Reading the whole diff once more before the documents | The mark that says `Function.prototype` is not a constructor was a hidden property, and `'%not-a-constructor' in f` was `true` for every function. It is a bit of the cell now. Looking for what else is stored that way found the row below |
| The same reading | A script can read and write the engine's own slots, which are properties whose names begin with `%`: `el['%classList'] = 5` makes `el.classList` a number. Older than this change, not fixed, a row of the task table |
| The sanitizer build, on a probe for classes in workers | A read of freed memory when a page ends while a worker is starting: 7 of 60 runs of `main`'s release build end in `SIGSEGV` on four `new Worker(...)` and nothing else. Not this change, not fixed, a row of the task table |
| The corpus run on a busy machine | One case of `long-strings.dat` timed out. It builds a string half a surrogate pair at a time, which costs the square of the length (20,000 pairs: 4.3 s, and 5 ms a pair at a time), and takes 6 of the 10 seconds a case is allowed. Same on `main`. The CI floor is one below the score until it is fixed; a row of the task table |
| jsdom, on the interface objects | Nothing wrong here, and three answers of jsdom's that are not the standard's, because its window is another realm than the one its script runs in: `Object.getPrototypeOf(EventTarget) === Function.prototype` is `false` there. `interfaces.dat` stays away from them |

### What is known to be wrong and is not fixed

All of it is the same on `main`. The first seven have a failing case each in
`prototypes.dat` (nine cases), recorded from Node.

- *(Fixed since: finding 49.)* **`new.target` is the function that is
  running**, not the one `new` was applied to. `if (new.target === Shape)
  throw …` in a parent's constructor stops `new Circle()`. The DOM needs the
  same value:
  `Reflect.construct(HTMLElement, [], Derived)` is `Illegal constructor`, so
  a custom element compiled by Babel cannot be made
  (`tests/dom-conformance/open/compiled-custom-elements.js`: 4 of 9 lines as
  jsdom prints them).
- **A built-in does not make the subclass it was called on**: `List.from`,
  `new List().map(f)` and `P.resolve(1)` give a plain array and a plain
  promise. No `Symbol.species`.
- **`instanceof` does not ask `Symbol.hasInstance`**, answers `false` for a
  right side with no `prototype` where it should throw, and does not ask a
  bound function for its target.
- **A function's own property names lack `length` and `name`**, `f.name =
  'x'` takes effect, and async and generator functions have
  `Function.prototype` itself as their prototype.
- **Nothing tells a built-in that is a constructor from one that is not**:
  `new Math.max()` makes an object, `class X extends f.bind(null)` is
  accepted.
- **A property of a prototype that cannot be written does not stop an
  assignment** to an object that inherits it.
- **A function bound to an object that holds it is never collected**, nor
  what a pending timer holds: the collector cannot see into what a native
  function captured.
- **An object a parent's constructor returns does not become `this`** in the
  child (fixed since: finding 49), and `super.x` in a method of a class that
  extends null is `undefined`.
- **Not about functions, found by running all of test262:** `Promise.all`,
  `any` and `race` read an iterable to its end before they look at an
  element, so with one that never ends the process grows until it is killed.
  Three tests end that way in both builds, and a fourth on an array index
  near 2^32 (arrays have no holes). A row of the task table.

### What the numbers do not say

**67 of 76** in `prototypes.dat`: the nine that fail were written to fail,
one for each thing above that a case could be recorded for. The 67 are what
somebody thought to ask.

**10,835 of 15,686** test262 tests in the directories about functions,
classes and objects, 10,727 before. That is 69%, and of the whole suite 54%
pass (26,012 of 47,967; 25,893 before). The number that matters is the
difference: 108 more and none fewer in those directories. In the whole
suite it is 122 more and three fewer, and the three are not noise. They ask
that the prototype of an async function, of a generator function and of an
async generator function is an object that cannot be called. It is
`Function.prototype` itself here, before and now, and the tests passed for
as long as `Function.prototype` was not callable. The fault they are about
is the fourth of the list above; the tests stay failing until it is fixed.

**640 of 640** DOM cases is what jsdom answers. The 11 new ones ask what the
prototype of an interface object is and whether its constants are inherited,
and nothing about what an interface object does when it is called.

**Every measure within a tenth of `main`** is 20 loops on one machine
(ARCHITECTURE.md, Phase 34, section 8). The one that moved is `bind`.

**How verified.** Every line is a command and what it printed on the merged
tree, in the sandbox.

- `make test`: `All 144 engine test suites passed successfully!`, five of
  them new. Linked against the objects of `main`, with the lines that use
  the new C++ interface cut, each of the five fails at least half of its
  checks.
- `make js-conformance`: `JS conformance, full corpus: 907/921 (98.5%) [23
  files, 0 crashes, 0 timeouts]`; `prototypes.dat` 67 of 76. With
  `--reference "node -e"`: 921 of 921. The binary of `main` passes 7 of the
  76.
- `python3 tools/conformance/run_test262.py --binary main=… --binary
  now=build/litebird`: 15,686 tests, `main` 10,727 pass, now 10,835; 108
  pass now and not on `main`, 0 the other way. With `--timeout 4 built-ins
  language annexB` added: 47,967 tests, `main` 25,893, now 26,012; 122 pass
  now and not on `main`, 3 the other way (named above).
- `make wpt`: `Web Platform Tests: 4658/5192 subtests pass in 76 files; 534
  fail as listed, 0 fail and are not listed`. On `main`: 4640, 552 listed.
- `make dom-conformance`: `640/640` in 24 files, and the same from jsdom
  with `--reference`. The binary of `main` passes 629: none of the 11 new
  cases.
- `make conformance` 1781 of 1792, `make url-conformance` 1188 of 1188,
  `make xml-conformance` 1702 of 1702, `make diff-trees` 0 of 6,000 trees
  differ, `make diff-xml` 0 of 4,000 documents and 0 of 3,000 round trips,
  `make diff-strings` 95 of 50,580 results differ, `make bench`, `make
  bench-css` 20 runs and 0 failures, `make security-fetch` 3 checks and 0
  failures: all as on `main`.
- `make deep` and `make asan` were run when `main` was three merges back:
  before a fix to a CSS selector, the attribute list of Phase 35 and the
  media work of Phase 38 came in, with seven suites. The unit tests were run
  again in the sanitizer build after the first two of those merges, 140 of
  140; after the third, CI's sanitizer job is what ran them.
- `make deep`: `1332 runs over 465 pages at depth 200000 on a 8 MB stack, 0 failed; slowest 22.5 s (table [dumps])`.
- `make asan`: 137 of 137 suites; JavaScript 906 of 921 (the one more is a
  case of deep recursion that needs more stack than a sanitizer build's
  frames leave, as before); DOM 640 of 640; URL 1188 of 1188; XML 1702 of
  1702; web-platform-tests 4658 of 5192; 1,000 generated documents and 1,000
  round trips; `fuzz: 400 mutations, 0 finding(s)`, `pipeline fuzz: 400 runs
  … 0 finding(s)`, `fuzz xml: 600 mutations … 0 finding(s)`; `882 runs over
  `882 runs over 315 pages at depth 10000 on a 2 MB stack, 0 failed`. No report from either
  sanitizer. It takes 19 minutes here.

---
