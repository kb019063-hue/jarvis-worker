# 49. ✅ `new.target` was the function that was running

> Moved out of `AUDIT.md` — written there as a `## 49. ✅ `new.target` was the function that was running` section.
> The text below is the section as it stood, unchanged.


**The hole.** `new.target` is the function `new` was applied to: in a
constructor reached through `super(...)` it is the subclass, and inside `F`
under `Reflect.construct(F, args, G)` it is `G`. Here it was the function
that was running. The value was bound on entry to a function, to the callee
whenever the call was a construction, and the call that got there carried
nothing else: `JsValue::construct(args, this)` was all `super(...)` and
`Reflect.construct` had to say. Finding 41 listed it as known and not fixed,
together with the second thing below; the rest was found while fixing those
two.

| A script | Before | Now, and in Node |
| :--- | :--- | :--- |
| `class Shape { constructor() { if (new.target === Shape) throw new TypeError('abstract'); } }`, `class Circle extends Shape {}`: `new Circle()` | `TypeError: abstract` | a `Circle` |
| `class MyErr extends Error { constructor(m) { super(m); this.who = new.target.name; } }`, `class Sub extends MyErr {}`: `new Sub('m').who` | `MyErr` | `Sub` |
| `function F() { this.nt = new.target; }`: `Reflect.construct(F, [], G).nt === G` | `false` | `true` |
| `function Parent() { return custom; }`, `class Child extends Parent { constructor() { super(); this.added = 2; } }`: `new Child() === custom`; `custom.added` | `false`; `undefined` | `true`; `2` |
| `class Child extends Parent { f = 7; }` over the same parent: `custom.f` | `undefined` | `7` |
| `var r = super()`: `r === this` | `false` | `true` |
| `constructor() { super([[1, 2]]); try { super(); } catch (e) {} }` in a subclass of `Map`: what was caught, and the map's size | nothing; `0` | `ReferenceError`; `1` |
| `class B extends A { constructor() {} }`: `new B()` | an object | `ReferenceError` |
| `constructor() { super(); return 1; }` | an object | `TypeError` |
| `Reflect.construct(F.bind(null), [], G)`: `new.target` in `F` | `F` | `G` |
| `Reflect.construct(Function, ['return 3'], G)`: the prototype of what is made | `Function.prototype` | `G.prototype` |
| `Reflect.construct(F)`, with no list of arguments | accepted | `TypeError` |
| `function Old() { var self = Reflect.construct(HTMLElement, [], new.target); self.made = true; return self; }` with a prototype, given to `customElements.define`: `new Old()`; `document.createElement('x-old') instanceof Old` | `TypeError: Illegal constructor`; `false` | an `Old`; `true` (this row and the next two: as in jsdom) |
| An `<x-old>` that stands in the page when that `define` runs: `el.made` | `undefined`, after an uncaught `Illegal constructor` | `true` |
| `class NotAnElement extends Event { constructor() { super('x'); made = this; } }`, given to `customElements.define` with such an element in the page: `made === el` | `true`: the element's object was made an event, and the element counted as defined | `false`: an event of its own, and the upgrade fails |

What it broke outside a test: an abstract class written the usual way
refused its own subclasses; an error class that names itself with
`new.target.name` named every subclass after the parent; a class that
extends a singleton, a factory function or a class compiled for older
browsers got a second object and lost the one its parent made, without an
error; and a custom element compiled by Babel could neither be constructed
nor take over an element of the page.

**The fix.** The call context that every call has beside its arguments got a
field for `new.target`, and `construct()` an argument for it; `super(...)`
passes on the one of the constructor it stands in and `Reflect.construct`
its third argument, also through a bound function. What a parent's
constructor returns becomes `this` in the child, where the class's fields
then go, and is what `new` gives. `this` is bound once, when `super(...)`
returns, and a derived constructor that returns without it, or returns a
primitive, throws. `Reflect.construct(Native, args, NewTarget)` hands a
built-in that builds on a given object (the DOM's interfaces) an object with
`NewTarget.prototype`, made when the built-in asks, so that the property is
read where the language reads it. And when the function named as
`new.target` is at that moment being run on an element that exists (an
upgrade), the element's interface is handed that element, once, whether
`super()` or `Reflect.construct` reached it: the HTML standard's
construction stack, kept beside the call context because the files of the
DOM bindings belonged to another open pull request. Four files of `src/js`
and two headers; ARCHITECTURE.md, Phase 43, has the design.

### What each instrument found

The change was written with a new file of the JavaScript corpus beside it,
`construct.dat`, whose expected output is what Node prints, and a new file
of the DOM corpus, `compiled.dat`, recorded from jsdom. What follows was
found by something else.

| Found by | What |
| :--- | :--- |
| test262, run in the binary of `main` and in this one | One test that passed on `main` and failed here: `built-ins/Promise/get-prototype-abrupt-executor-not-callable.js`. The first version made the object for a built-in before running it and so read `NewTarget.prototype` before `Promise` had looked at its executor; the test has a getter there that throws. The object is made when the built-in asks for it now (`JsValue::construct_for`), and built-ins that never ask get the prototype afterwards |
| A probe of 51 lines on when `this` is bound, run in Node, in `main`'s binary and here | The rows of the list above on a second `super()`, on none at all and on a returned primitive, which nobody had listed: all three were accepted. Also that on `main` a parent that returns a function or an array was a `TypeError` |
| Reading the whole diff once more before the documents | A second `super()` has to run the parent (a script can count the runs) and then throw. Run on the constructor's own `this` it overwrote what the first had made: a `Map` came out empty, an error's message was replaced, the parent's fields were reset. The second run gets a spare object now, and a case holds it |
| The same reading, a second time | `Reflect.construct(Event, ['q'], NewTarget)` read `NewTarget.prototype` twice when what it found there was not an object: once when the constructor asked for its object and once afterwards. Once on every path now, with an assertion for each |
| `compare_with_jsdom.py` on `open/compiled-custom-elements`, after the constructor worked | Three of the page's 11 lines still differed, and they were the common case: an element that stands in the page, comes from `innerHTML` or exists before `define` was not upgraded by a compiled constructor (`The custom element constructor returned a different object.`), because `Reflect.construct(HTMLElement, …)` made a second element. That is what the construction in place is for |
| The same tool, on a page with a class and a subclass of it compiled for ES5 | The upgrade still failed there: the compiled constructor constructs the class on a new object, and the class's `super()` built a second element on that. The interface is given the element under construction when it asks, however it was reached, and `super(...)` makes that element `this` |
| The same tool, on a class that makes a second instance of itself while it is being upgraded | On `main`, and in the first version of this, the inner instance was a new element; the standard refuses it once the element has been taken, and before that gives it the element. Both as the standard now, for a class and for a compiled constructor |
| Reading the code that hands the element out | Any interface that asked would have been given it: `Reflect.construct(Event, [], new.target)` in the constructor of a custom element would have made the element's own object an event. And `main` did exactly that through `super()`, to a class that extends `Event` and is given to `customElements.define` (the last row of the list above). Only the interface the element belongs to is given it now |
| The same probes | A case of the new file passed for the wrong reason. It counted how often the constructor ran for `createElement` and `cloneNode`, and the constructor did run for the clone before its upgrade failed with an error on standard error, which the DOM runner does not look at. The case prints what the constructor left on each element now |
| Writing the case for fields on an object a parent returned | A private name is a property whose name begins with `#`: `obj['#p']` reads a private field from outside its class, and `o.#p` for an object without the field is `undefined` where it is a `TypeError`. Older than this change, not fixed, a row of the task table |
| A case for `new.target` inside `eval` | `eval` inside a function is global code: no local variable, no `this`, no `new.target`, no `super`. Older than this change, not fixed, a row of the task table, and one failing case |
| jsdom, on `Reflect.construct(Event, …)` | Two answers of jsdom's that are not Web IDL's: it reads `NewTarget.prototype` twice, and when that is not an object it makes something that is not an event. `compiled.dat` stays away from both and the unit suite asserts the standard's |
| Asking, while checking that the collector sees the list of constructions in place, which thread it belongs to | It was a list for each thread, and the body of a generator or of an async function that awaits runs on a thread of its own. A constructor that asked for its element from there, during an upgrade, found no construction and was refused (`The custom element constructor returned a different object`): 4 of the 6 lines of a page written for it differed from jsdom. Nobody compiles a constructor that way, but the list was in the wrong place: it is the agent's now, as the standard's construction stack is the definition's. One case of `compiled.dat` and two assertions of the unit suite |
| Reading `eval_super_call` once more while the sanitizer build ran | Two things about a class that is no element and is given to `customElements.define`. A parent that is no interface (`Map`, `Error`, `Array`) still made the element's object what it had built, as on `main`: `Array.isArray(el)` was `true` and the element matched `:defined`. And an interface that builds its own object whatever it is offered (`Text`, `DocumentFragment`) left `this` an empty object in the second version of this change, where on `main` it had made the element's object the wrapper of a text node: the code that hands `this` over had taken for granted that an interface builds on what it is given. Nothing is taken over into a platform object now; one case of `compiled.dat` (5 classes), three assertions |
| A table of 28 timings against two copies of `main`'s binary, made for the section on cost (ARCHITECTURE.md, Phase 43, section 7) | The first version cost a third of a microsecond for every `super(...)`: `new` on a class three levels deep was 20% slower, on a subclass of `Map` 33%, on a custom element class two levels deep 39%. Two things it had been built around did it, both of them there because a header belonged to another pull request at the time: `new.target` reached a function through a thread-local slot that every call wrote and cleared, with a copy for every construction, and whether `this` was bound was a binding with a name, declared whenever a derived constructor was entered and looked up by name three times before it returned. That pull request had merged by then. `new.target` is an argument and the flag a field of the scope now; the same three measures stand at 1.03 to 1.09 in two runs, and a second probe of 24 lines on when `this` is bound, written to check the rewrite, prints what Node prints |
| The sanitizer build, on the new cases, the probes and the six new suites | Nothing in this change: no report from either sanitizer in four whole runs of `make asan` in the sandbox, the last one on the tree with the two changes of the row above ("How verified" has what it ran) |
| CI's sanitizer job, once it could say what failed | It had failed on this branch two times running, with `Process completed with exit code 2` and nothing else to go by: a job's log is served by a host the sandbox cannot reach. The same step had failed once on `main` the day before and once, in the same half hour, on another branch whose tree was `main`'s, and `make asan` passed here every time. So the job was made to keep its output, with a last step that runs only after a failure and prints what led up to it as annotations, which the API itself serves (`tools/ci/annotate_failure.py`). The next run passed; the one after failed and said: `Web Platform Tests: 7968/8010 subtests pass in 103 files; 41 fail as listed, 1 fail and are not listed`, with the unit suites and the other four corpora behind it, all passing. One row more than there are subtests, and none fewer passing: that is the harness's own row. testharness.js gives a page 10 seconds, and reports `TIMEOUT` for the file when the page takes longer than that to reach its load event, though every subtest has passed by then (it finishes from a zero-delay timer set in the load event, and the 10-second timer is due first). A page with one subtest that takes 11 seconds shows it: `PASS`, and `(harness) TIMEOUT`. The sanitizer run gave each file 600 seconds and the harness inside it 10; under ASan here the longest file needs 6 seconds to its load event, alone on a core, and a runner is not always that quiet. `run_wpt.py` scales the harness's limit with its own now, the way wptrunner does for slow builds, and names the failures that are not listed even when it is asked to be quiet, which is what would have said all this the first time. Which file it was on CI is not known, for that reason |

### What is known to be wrong and is not fixed

All of it is the same on `main`.

- **An upgrade runs the constructor on the element itself**, where the
  standard constructs it the ordinary way and lets `HTMLElement` find the
  element. A class cannot tell, and neither can a compiled constructor that
  does what it is for. In a constructor written as a function `this` is the
  element (`this === self`); one that never reaches `HTMLElement` has
  written on the element and its upgrade counts as done, where the standard
  refuses it; and the element is given the class's prototype before the
  constructor runs, so it keeps that prototype when the upgrade fails early.
  In `src/js/DomObserverBindings.cpp` (`run_upgrade`);
  `tests/dom-conformance/open/compiled-custom-elements.js` has the lines.
- **A customized built-in whose upgrade failed still matches `:defined`.**
  The same page.
- **`this` can be used before `super(...)` has run**, where it is a
  `ReferenceError`; `let` and `const` have no dead zone either. One failing
  case of `construct.dat`.
- **`eval` inside a function is global code.** One failing case of
  `construct.dat`.
- **A private name is a property whose name begins with `#`**, readable and
  writable as `obj['#p']`.
- **A script's own property whose name begins with `#`, `%` or `@@`, or has
  the shape `__x__`, is left out of `Object.keys`, `JSON.stringify`,
  `for…in`, `Object.assign` and spread.** `var m = {}; m['#/route'] =
  'home'; JSON.stringify(m)` is `{}`. It is how the engine hides its own
  slots (finding 41) seen from the other side, and it loses a script's data
  without an error.
- **`new Map(iterable)` and `new Set(iterable)` do not call the `set` and
  `add` of the object they fill**, so a subclass that overrides one is not
  asked.
- **Any function is accepted as `new.target` and as a parent**:
  `Reflect.construct(F, [], Date.now)` and `class X extends f.bind(null)`
  (finding 41's "nothing tells a built-in that is a constructor from one
  that is not").

### What the numbers do not say

**47 of 49** in `construct.dat`: the two that fail were written to fail
(`this` before `super()`, `eval` inside a function). The 47 are what
somebody thought to ask, and the answers are Node's.

**25 of 25** in `compiled.dat` is what jsdom answers, and jsdom runs an
upgrade the way the standard says. The engine does not: `run_upgrade` still
runs the constructor on the element itself. Where a constructor can tell,
the answers differ, and those lines are not in the corpus, because a case
that fails is not recorded there. They are on the open page (5 lines, 4
differ) and in the page of classes that are no elements (5 of 13
lines as jsdom prints them). A full file means that the shapes a compiler
emits work, not that an upgrade is what the standard says it is.

**43 more test262 tests and none fewer**, of 47,967, of which 58.1% pass.
The 43 are about this change: what `super(...)` binds and returns, and
`new.target`. Six tests that make an array near 2^32 entries long end by
memory or by the clock, in both builds, and not the same way each time.

**The comparisons are four pull requests old.** Everything that sets this
change against `main` (the two corpora in `main`'s binary, test262, the
pages against jsdom, the timings) was measured with `main` at `95d7fb2`.
`main` took four more pull requests while that ran, and for three quarters
of an hour of it did not build (one of them called a function that the next
had removed). What was run again on the tree as merged is what checks and
does not compare: the unit suites, the corpora, web-platform-tests, the
differential runs. None of the four touches a call or a construction, so
the comparisons are taken to hold; they were not made again.

**The cost** (ARCHITECTURE.md, Phase 43, section 7) is 28 measures on one
machine, on which two copies of the same binary measure up to 5% apart and
measures that run none of the changed code moved by four percent either way
between runs. What it shows for certain is what the first version cost (a
fifth to two fifths on a chain of classes), which is gone. What is left is
about a tenth of a construction at most and is not resolved further.

**The sanitizer build has not run this tree here.** `make asan` passed four
times in the sandbox on this change, the last time on the commit that last
changed the engine; five pull requests of `main` have been merged in since
(the layout of floats, text on a canvas, `<select>`, the attribute model,
CSS colours), and on the tree with those it is CI's job that runs it.

**CI's sanitizer job** failed three times on this branch and passed once,
all before the runner was changed. What the third failure showed is the
web-platform-tests step with one row more than there are subtests (the
table above); the harness's time limit explains that row and is scaled now.
That the first two failures were the same thing is likely (the same step of
the same job, at the same point of it) and was not seen: there was nothing
to read then. A run that passes after the change shows little by itself,
since one passed before it.

**How verified.** Every line is a command and what it printed, in the
sandbox. The first six are from the tree as merged (`main` at `a4952ed`).

- `make test`: `All 191 engine test suites passed successfully!`, six
  of them new.
- `make js-conformance`: `JS conformance, full corpus: 974/989 (98.5%)  [25
  files, 0 crashes, 0 timeouts]`; `construct.dat` 47 of 49, `prototypes.dat`
  68 of 76.
- `make dom-conformance`: `DOM conformance, full corpus: 669/669 (100.0%)
  [25 files, 0 crashes, 0 timeouts]`; `compiled.dat` 25 of 25.
- `make conformance`, `make url-conformance`, `make xml-conformance`, `make
  wpt`: 1792/1792, 1188/1188, 1718/1718, and `Web Platform Tests: 7998/8009
  subtests pass in 103 files; 11 fail as listed, 0 fail and are not listed`.
- `make diff-trees`: four times 1,500 pages, `0 differ` each. `make
  diff-xml`: `4000 documents … 0 DIFFER` and `3000 trees, 3000 come back the
  same`. `make diff-strings`: `50485 equal to node's, 95 differ`, as on
  `main`. `make verify-promise-iterators`: 34/34.
- `make deep`, one page at a time (`--jobs 1`; two at a time need more
  memory than the sandbox has): `deep pages: 1332 runs over 465 pages at
  depth 200000 on a 8 MB stack, 0 failed; slowest 21.5 s (table [dumps])`.

From this branch with `main` at `95d7fb2` merged in (`357b861`), where "the
binary of `main`" is a release build of that commit:

- The corpora in the binary of `main`: JavaScript 939 of 989, 13 of the 49
  of `construct.dat`, 67 of the 76 of `prototypes.dat`; DOM 646 of 669, 2 of
  the 25 of `compiled.dat`. In Node (`--reference "node -e"`) 989 of 989; in
  jsdom (`--reference`) 669 of 669.
- `python3 tools/conformance/run_test262.py --timeout 6 --binary main=…
  --binary now=build/litebird built-ins language annexB`: 47,967 tests,
  `main` 27,829 pass, now 27,872; 43 pass now and not on `main`, 0 the
  other way. Of those, the 21 directories `make test262` runs: 15,686
  tests, `main` 12,275, now 12,316; 41 and 0.
- `python3 tools/conformance/compare_with_jsdom.py PAGE SCRIPT`, lines the
  same as jsdom's, here and in the binary of `main`: 9 of 9 and 1; 6 of 6
  and 0; 5 of 5 and 3; 8 of 8 and 3; 7 of 7 and 0; 3 of 4 and 2; 5 of 13 and
  3; the open page 1 of 5 and 0 (the pages are named in ARCHITECTURE.md,
  Phase 43, section 6).
- `make tsan`: passed. `make security-fetch`, `make security-worker`: 3 and
  17 checks, 0 failures. `make verify-media`: 35 checks. `make fuzz-media`:
  1,800 clean.

And from `b740984`, the last commit that changed the engine:

- `make asan`: exit 0, no report from either sanitizer. 183 suites;
  JavaScript 973/989 (the one fewer is `d(2000)` in `functions.dat`, which
  needs more stack than the sanitizer build's frames leave; the same on
  `main`); DOM 669/669; URL 1188/1188; XML 1718/1718; WPT 7968/8009 with
  the 41 listed then; `1000 documents … 0 DIFFER`, 1000 of 1000 round
  trips; `fuzz: 400 mutations, 0 finding(s)`, `pipeline fuzz: 400 runs … 0
  finding(s)`, `fuzz xml: 600 mutations … 0 finding(s)`, media 1,800 clean;
  `deep pages: 882 runs over 315 pages at depth 10000 on a 2 MB stack, 0
  failed; slowest 3.5 s`.

CI: the runs are in the pull request; what its sanitizer job said when it
failed is in the table above.

---
