# Phase 34 Architecture: A Function Can Be a Prototype

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 34 Architecture: A Function Can Be a Prototype` section.
> The text below is the section as it stood, unchanged.


### 1. What was wrong

A function in this engine is a value, not a heap cell: a `JsValue` whose
payload its copies share. An object is a cell, and a prototype link is a
pointer from one cell to another. So a link could not point at a function,
and on `main` three things stood in for one:

- The prototype of every function was `Function.prototype` by a rule, not by
  a link, and `Function.prototype` was an object.
- A class kept its parent in a hidden property, `%super`, and a static member
  was looked for there: one step and no further. With `class A { static s()
  {} }`, `class B extends A {}` and `class C extends B {}`, `C.s()` was `s is
  not a function`. The DOM's interface objects were tied together the same
  way, so `HTMLElement.ELEMENT_NODE`, a constant of `Node` two steps up, was
  undefined.
- `Object.setPrototypeOf(g, f)` and `Object.create(f)` stored a pointer to
  the cell that holds the properties of `f`. A lookup found those properties
  through it. `Object.getPrototypeOf` made an object out of that cell: not
  `f`, and not something that can be called.

| A script | `main` | Now, and in Node |
| :--- | :--- | :--- |
| `Object.getPrototypeOf(TypeError) === Error` | `false` | `true` |
| `class Derived extends Base {}`; `Object.getPrototypeOf(Derived) === Base`, `Derived.__proto__ === Base`, `Base.isPrototypeOf(Derived)` | `false`, `false`, `false` | `true`, `true`, `true` |
| `typeof Object.getPrototypeOf(function () {})`; `Function.prototype()` | `object`; `TypeError: prototype is not a function` | `function`; `undefined` |
| A static of a grandparent: `C.s()` above | `TypeError: s is not a function` | runs |
| `class B extends A {}` with `static get tag() { return this.name; }` in `A`: `B.tag` | `A` | `B` |
| `var o = Object.create(f)`; `Object.getPrototypeOf(o) === f`; `Object.getPrototypeOf(o)()` | `false`; `TypeError: p is not a function` | `true`; what `f` returns |
| `Reflect.construct(F, [], G) instanceof G` | `false` | `true` |
| `F.prototype.m = …`, then `Object.defineProperty(F, 'prototype', { writable: false })`: how a compiled class ends | `TypeError: Cannot redefine property: prototype` (and with `prototype` never read before, it became `undefined`) | accepted, and `F.prototype = 5` then changes nothing |
| `Object.getPrototypeOf(Derived).call(this, x)`, the first thing a compiled constructor does | `TypeError: Function.prototype.call called on a non-function` | calls `Base` |
| `class C extends A { constructor() { super(); } }`; `Object.setPrototypeOf(C, B)`; `new C()` | runs `A` | runs `B` |
| `class X extends (() => 1) {}`; `Reflect.construct(F, [], () => 1)` | accepted; accepted | `TypeError`; `TypeError` |
| `Object.setPrototypeOf(Object.preventExtensions({}), {})` | accepted | `TypeError`; `Reflect.setPrototypeOf` says `false` |
| `F.prototype = 5`; `({}) instanceof F` | `false` | `TypeError` |
| `Object.getPrototypeOf(HTMLElement) === Element`; `HTMLElement.ELEMENT_NODE` | `false`; `undefined` | `true`; `1` |

Two kinds of program met it. web-platform-tests met it first:
`assert_throws_js(TypeError, …)` checks that what it was given is an error
type by walking up with `Object.getPrototypeOf` until it reaches `Error`, so
every such assertion failed, 18 subtests in the part of the suite that is
vendored (Phase 28). The other kind is most of what pages ship: a class
compiled for older browsers. Babel's helpers (`_inherits`, `_createSuper` or
`_callSuper`, `_wrapNativeSuper`) and TypeScript's `__extends` build a class
out of exactly the calls in the table, and each of the three compiled
programs in the corpus stopped at its first class.

### 2. The cell that stands for a function

A function still is a value. What changed is that it can have a cell that
knows which function it belongs to, and that every kind of function can have
one.

- **One place for the cell.** The handle to a function's cell moved from
  `FunctionData`, which only script functions have, to the payload every
  function has (`JsValue::Extra::cell`). `property_cell(true)` makes a cell
  for a script function when a property is first set on it, as before. A
  native function keeps its few properties in a table, as before, until
  `function_cell()` is called on it: that moves the table into a new cell,
  and from then on the function is stored the way a script function is.
- **`as_prototype()`** is what everything that makes a link calls
  (`Object.create`, `Object.setPrototypeOf`, the `__proto__` setter, a class
  with a parent, `Reflect.construct`). For an object it is the object's
  cell. For a function it is the function's cell, and the cell is given a
  copy of the function (`JsObject::set_function_value`).
- **`JsValue::make_object(cell)`** is what everything that follows a link
  calls. For a cell that has a function it gives back the function. That
  one line is why `Object.getPrototypeOf(Derived)` is `Base`, has `Base`'s
  identity under `===`, and can be called and constructed.

The copy is kept in `JsObject::internal`, the pointer a `Date` keeps its time
in and a `Map` its entries, which the cell of a function had no use for
(`function_value()` reads it when the cell's class is `Function`). No object
and no value grew: `sizeof(JsObject)` is 328 and `sizeof(JsValue)` 88, as on
`main`.

A function has three properties it does not store: `name`, `length`, and
for a constructor `prototype`, an object made the first time it is asked
for. They are answered where the function is the receiver, and now also
where a chain runs through a function's cell (`lookup_chain`, `has_property`
in `src/js/JsValue.cpp`), so `Object.create(f).name` is the name of `f`.
`prototype` is an own property before
anything has read it (`hasOwnProperty`, `getOwnPropertyDescriptor`, `delete`
and `defineProperty` agree: writable, not enumerable, not configurable), and
an assignment to it keeps those flags, so a loop that copies the static
members of a constructor no longer copies `prototype` with them.

### 3. The collector

The cell owns a copy of the function, and the function's payload owns a
handle to the cell. That is a cycle of two reference counts, and reference
counts never free a cycle. It is the collector's to take apart, and it is the
reason a function gets the copy only when it becomes a prototype: most
functions never do, and a closure that nothing was set on still has no cell
at all (100,000 of them: 0 cells, on `main` and now).

The collector already knew one such pair: an object held by a value that is
held by the object's cell (Phase 23, section 5). Before it marks, it counts
for each payload how many of its holders are cells; a payload with no holder
outside the heap is not a root. `JsObject::trace` reports the function copy
with the use count of the pointer that holds it
(`Heap::mark_held_value(*function, internal.use_count())`), and the payload
reports the cell it has a handle to (`PayloadInfo::cell`), so a class and its
cell are found to hold only each other and go together, and with them the
scopes the class closed over.

Two wrong versions of those lines were built and run against the tests, to
see that the tests can tell:

| Changed in `src/js/Heap.cpp` | What happens | Caught by |
| :--- | :--- | :--- |
| The function a cell stands for is not reported | Every class with a child stays for ever | `make test` stops in `gc_roots_are_complete_under_empty_root_set`, a suite of Phase 23; of the new suites `js_classes_and_their_parents_are_collected` and `js_interface_objects_are_collected_with_their_page` fail; five more cases of `prototypes.dat` fail |
| It is counted and not marked | A parent reachable only as a prototype loses what it closed over: `secret is not defined` | `js_classes_and_their_parents_are_collected`; one more case of `prototypes.dat` |

The second was passed by every test until one test called the function it
had reached through `Object.getPrototypeOf` and read a variable the function
closed over. The cell keeps the function's `name` and its properties, so
checks of those cannot see that the closure is gone.

### 4. Classes, `super` and `new`

`%super` is gone. `class Derived extends Base` links
`Derived`'s cell to `Base.as_prototype()` (`define_class`,
`src/js/JsEval.cpp`), and everything that asked `%super` asks the link:

- A static member is an ordinary lookup along the chain, to any depth, and a
  static getter runs with the class it was read from as `this`.
- `super(...)` calls whatever the prototype of the constructor is when it
  runs, as the language says, so `Object.setPrototypeOf(Derived, Other)`
  changes it. `super.name` in a static method starts one step up from the
  class the method lives in (`super_base`, `js_get_from_chain`).
- `js_is_constructor()` is one answer to "can this be constructed", asked by
  `new`, by `extends`, by `super()` and for both function arguments of
  `Reflect.construct`: a function that is not an arrow function, a method,
  an async or a generator function.
- `js_construct(ctor, args, new_target)`: with a third argument the object
  gets `new_target.prototype`, whether `ctor` is a script function or a
  built-in that makes its own object.

**`Function.prototype` is a function.** It takes anything and returns
undefined, its cell is the one every function's chain already ended in
(`function_cell(adopt)`), and it is the one built-in marked as not a
constructor: `new Function.prototype()` throws, and so does `super()` in a
class that extends null, whose parent it is. The mark is a bit of the cell
(`JsObject::not_constructor`, in the padding beside the other flags). It
was a hidden property at first, and every function then answered `true` to
`'%not-a-constructor' in f`.

### 5. `Object`, `Reflect`, `bind`, the error types

- `js_set_prototype()` refuses a link that would close a loop, as before,
  and now also a change to an object that is not extensible; setting the
  prototype an object already has is always allowed. `Object.setPrototypeOf`
  throws on a refusal, `Reflect.setPrototypeOf` returns `false`, and the
  `__proto__` setter ignores what is neither an object nor null.
- `Object.defineProperty` follows the standard's
  ValidateAndApplyPropertyDescriptor (`define_from_descriptor`,
  `src/js/BuiltinsObject.cpp`). What it used to refuse and compiled classes
  do: redefining a property that cannot be configured with the flags and
  the value it has, and taking `writable` away from one. What it used to
  accept: changing such a property's getter, or its kind.
  `Reflect.defineProperty` returns `false` where it used to throw.
- `f.bind(x)` has the prototype of `f`, so a bound class finds the static
  members of the class's parent.
- `EvalError`, `RangeError`, `ReferenceError`, `SyntaxError`, `TypeError`
  and `URIError` have `Error` as their prototype.
- `x instanceof F` throws a `TypeError` when `F.prototype` is there and is
  not an object. It has to now: `Function.prototype` is callable, so
  `x instanceof Function.prototype` no longer stops at "not callable", which
  is where it used to end. With no `prototype` at all the answer is still
  `false` (section 9 says why it does not throw).
- `{ __proto__: function () {} }` sets a prototype and defines no property,
  so the function is not named after the key. It was named `__proto__`, on
  `main` as well, where the function could not be got back to see it.

### 6. The DOM's interface objects, and the bytecode VM

`Realm::proto` (`src/js/DomBindings.cpp`) tied an interface object to its
parent with `%super`. It makes the link now, as Web IDL has it:
`Object.getPrototypeOf(HTMLDivElement)` is `HTMLElement`, then `Element`,
`Node`, `EventTarget`, `Function.prototype`. Constants are inherited to any
depth, and a class that extends `HTMLElement` has it as its prototype.

The VM has an `Object` of its own with three functions (`create`,
`getPrototypeOf`, `setPrototypeOf`, `src/js/VM.cpp`), which read and wrote
`object_val->prototype` directly. They make the same three calls the
interpreter's do.

Both files were added to this phase's pull request only after Phase 33,
whose pull request named them, was merged.

### 7. How this is verified

| Check | Result |
| :--- | :--- |
| JavaScript corpus (`make js-conformance`) | 907 / 921 in 23 files. `prototypes.dat` is new: 76 cases, 67 pass, and the 9 that do not are in section 9. The binary of `main` passes 7 of the 76 |
| The corpus in Node (`--reference "node -e"`) | 921 / 921 |
| test262, the 21 directories this change touches (`tools/conformance/run_test262.py`) | 15,686 tests: 10,835 pass, 10,727 in the binary of `main`. 108 that failed pass; none that passed fails |
| test262, all of `built-ins`, `language` and `annexB` (the same runner with those three names) | 47,967 tests: 26,012 pass, 25,893 in the binary of `main`. 122 that failed pass. Three that passed fail, for a reason that is in section 9 (below) |
| web-platform-tests (`make wpt`) | 4658 / 5192; the 18 `function-as-prototype` subtests pass and are off the list, which has 534 left, 495 of them frames |
| DOM corpus (`make dom-conformance`) | 640 / 640 in 24 files; `interfaces.dat` is new, 11 cases. The binary of `main` passes none of the 11. In jsdom (`--reference`): 640 / 640 |
| Unit tests (`make test`) | 144 / 144; five are new (a function as a prototype, in the interpreter and in the VM; classes and their parents collected; classes compiled to ES5; a class that extends a DOM interface; interface objects collected with their page) |
| Classes compiled to ES5 | Three programs in the corpus: Babel 7's helpers, Babel's newer ones (`_callSuper`), TypeScript's `__extends`. `main` stops at `Cannot redefine property: prototype` in the first two and at `call is not a function` in the third |
| A page that is not in the corpus (`tools/conformance/compare_with_jsdom.py`) | `tests/dom-conformance/open/compiled-custom-elements.js`, custom elements compiled by Babel: 4 of 9 lines as jsdom prints them (1 on `main`). The other five are section 9 |
| html5lib, XML, URL, `make diff-trees`, `make diff-xml` | 1781 / 1792, 1702 / 1702, 1188 / 1188, 0 of 6,000 trees and 0 of 4,000 documents differ: as on `main` |
| `make deep` (when `main` was three merges back: before a fix to a CSS selector, the attribute list of Phase 35 and the media work of Phase 38 came in, with seven suites) | `1332 runs over 465 pages at depth 200000 on a 8 MB stack, 0 failed; slowest 22.5 s (table [dumps])` |
| `make asan` (ASan + UBSan; at that state too, so with 137 suites. The unit tests were run again in the sanitizer build after the first two of those merges, 140 / 140; after the third, CI's sanitizer job is what ran them) | 137 / 137; JavaScript 906 / 921 (one case of deep recursion needs more stack than a sanitizer build's frames leave, as before); DOM 640 / 640; URL 1188 / 1188; XML 1702 / 1702; web-platform-tests 4658 / 5192; 1,000 generated XML documents and 1,000 round trips; `fuzz: 400 mutations, 0 finding(s)`, `pipeline fuzz: 400 runs … 0 finding(s)`, `fuzz xml: 600 mutations … 0 finding(s)`; `882 runs over 315 pages at depth 10000 on a 2 MB stack, 0 failed`. No report from either sanitizer |
| Two pages alive at once, one destroyed and collected while the other runs on | The chain questions get the same answers in each throughout, and the cell count comes back to where it started: a block of the last new unit suite, and the same sequence as a side harness in the sanitizer build, with no report |

**What test262 found.** The corpus here is recorded from Node and was
written with the change; test262 was written by neither. It was run in the
binary of `main` and in this one, for the difference and not for the score.
(69% of these directories pass. Of the 4,851 tests that do not, 1,259 write
an identifier with a Unicode escape, `\u{6F}`, which the lexer does not
read, and 1,027 are scripts that should be refused before they run and are
not.) The first run had two tests that passed on `main` and failed
here, and both were true: the `instanceof` and the `__proto__:` of section
5, each a fault that only showed once a function could be got back from a
link. Both are cases of `prototypes.dat` now.

The rest of the suite was run once after that. Three tests that pass on
`main` fail: `AsyncFunctionPrototype-is-not-callable.js` and the same
question for generator and async generator functions. They ask that the
prototype of an async function is an object that cannot be called. Here it
is `Function.prototype` itself, on `main` as now, and the tests passed for as
long as `Function.prototype` was not callable. They stay failing: what they
ask for is three objects the engine does not have (section 9). Four more
tests end with the process out of memory in both builds, which is two older
faults: `Promise.all`, `any` and `race` read an iterable to its end before
they look at an element, so one that never ends is never left; and an index
near 2^32 makes an array that long, since arrays have no holes.

### 8. What it costs

Release build, each measure in the binary of `main` (at `5e573f5`, the
commit before the URL work of PR #38) and in this one, turn about, three
processes each; the best of five repetitions in a process, with a collection
between them. Milliseconds.

| | `main` | Now |
| :--- | ---: | ---: |
| Read an own property, 2,000,000 times | 462 | 448 |
| Read through 20 prototypes, 300,000 | 82 | 82 |
| Call a method of an instance, 1,000,000 | 591 | 573 |
| Call a method inherited through 3 classes, 1,000,000 | 606 | 597 |
| Call a function, 2,000,000 | 1075 | 1026 |
| `f.call(o, i)`, 1,000,000 | 787 | 802 |
| `f.apply(null, args)`, 500,000 | 414 | 415 |
| `f.bind(o)` and one call of it, 200,000 | 271 | 297 |
| `new F(i)`, 500,000 | 449 | 462 |
| `new D(i)` with three `super()` calls, 200,000 | 751 | 746 |
| A class and a class that extends it, declared 20,000 times | 60 | 59 |
| Call a static method of the class, 1,000,000 | 571 | 564 |
| Call a static method through 3 parents, 500,000 | throws (section 1) | 332 |
| `d instanceof A` through 3 classes, 1,000,000 | 262 | 256 |
| `f.name`, `f.length` and `f.prototype`, 500,000 | 248 | 252 |
| `Object.defineProperty`, 200,000 | 318 | 327 |
| `Object.create(p)` and `Object.getPrototypeOf`, 300,000 | 241 | 248 |
| Throw and catch a `TypeError`, 100,000 | 469 | 491 |
| Make a closure and call it, 500,000 | 390 | 379 |
| Make 50,000 objects with a closure each, then `gc()` | 52 | 53 |

Nothing that was measured got faster or slower by more than the machine
varies from run to run, except `bind`, which now asks for its target's
prototype (about a tenth more for making a bound function; calling one
costs what it did).

The table is from a quiet machine. A repeat on a busy one was thrown away:
there, seven runs of one binary on one loop differed by up to a third in
processor time, more than any row above. With a copy of `main`'s binary run
beside `main`'s as the control, the best run of this build lay between the
two for three million calls (2.14 s; 2.07 and 2.21), below both for a
million closures, and 13% above both for `bind`.

In memory:

- A realm has 11 more cells that stay (55 where a script begins, 44 on
  `main`): the error types have cells of their own, and so has each
  interface object with a parent. One that a page goes on to use costs a
  cell more than it did (`HTMLDivElement`, never used before: 3 cells where
  it was 2).
- A built-in function that becomes a parent or a prototype gets a cell,
  which stays as long as the built-in does: one for `Map` however many
  classes extend it.
- 2,000 pairs of classes: 10,000 cells while they are held, none after, on
  `main` and now.
- A chain of 200,000 classes, each the parent of the next: made in 0.4 to
  0.5 s, 400,002 cells, 0.4 s for a collection while it is held and 0.6 s
  for the one that frees it, all as on `main`. A static member of the first
  class is read from the last in 25 to 44 ms, and the walk up with
  `Object.getPrototypeOf` is 200,001 steps in 0.2 to 0.3 s; on `main` the
  member is `undefined` and the walk is one step.

### 9. What is still open here

Nine cases of `prototypes.dat` failed when this phase ended, each recorded
for one of the first seven of these, and everything in the list was the same
on `main`. Phase 43 has since done the first item and the first half of the
last, and eight cases fail:

- *(Done in Phase 43.)* **`new.target` is the function that is running**, not the one `new` was
  applied to. `if (new.target === Shape) throw …` in a parent's constructor,
  the usual way to write an abstract class, stops `new Circle()`; and with
  `Reflect.construct(F, [], G)` the object has `G.prototype` and `new.target`
  inside `F` is `F`. The DOM needs the same value:
  `Reflect.construct(HTMLElement, [], Derived)` is `Illegal constructor`,
  so a custom element compiled by Babel cannot be made (the five lines of
  `compiled-custom-elements.js`).
- **A built-in does not make the subclass it was called on.**
  `class List extends Array {}`: `List.from([1])` and `new List().map(f)` are
  plain arrays, `class P extends Promise {}`: `P.resolve(1)` is a plain
  promise. There is no `Symbol.species`.
- **`instanceof` does not ask `Symbol.hasInstance`**, and answers `false`
  where it should throw for a right side with no `prototype` at all (an
  arrow function, a method, `Math.max`). It cannot throw there yet: a bound
  function has none either and should be asked for its target's
  (`new F() instanceof F.bind(null)` is `false`), and six of the host's
  constructors have none (`Response`, `Headers`, `Worker`, `WebSocket`,
  `IntersectionObserver`, `ResizeObserver`), where `r instanceof Response`
  must not start to throw.
- **What a function's own properties are.** `Object.getOwnPropertyNames(f)`
  lacks `length`, `name` and a `prototype` nothing has read; `f.name = 'x'`
  takes effect. An async function and a generator function have
  `Function.prototype` itself as their prototype, where the language has an
  object of its own for each kind between the two. `typeof` of that
  prototype is therefore `function` now and was `object` before, and three
  test262 tests that ask for `object` passed on `main` and fail here.
- **Which built-ins are constructors.** Nothing tells `Math.max` from `Map`:
  `new Math.max()` makes an object, and `class X extends f.bind(null)` is
  accepted for any `f`.
- **A property of a prototype that cannot be written does not stop an
  assignment** to the object that inherits it.
- **A function bound to an object that holds it is not collected**:
  `o.b = f.bind(o)` 2,000 times leaves 2,000 cells after `gc()`. The
  collector cannot see into what a native function captured. It is part of
  "no leak audit has been done" in Known gaps, 4.
- *(The first half done in Phase 43.)* **An object that a parent's
  constructor returns does not become `this`** in the child: after `super()`
  the child still has the object `new` made. And `super.x` in a method of a
  class that extends null is `undefined`, not a `TypeError`. (Found by
  test262; no case here yet.)

Three things were found on the way that are not about prototypes. All are
on `main`, and all are rows of the task table now:

- **A page that ends while a worker is starting can end the process.** Four
  `new Worker('data:text/javascript,postMessage(1)')` and nothing else:
  7 of 60 runs of the release build of `main` end in `SIGSEGV`. The
  sanitizer build reports a read of freed memory on the worker's thread, in
  a table that process exit had destroyed.
- **A script can read and write the engine's own slots.** They are
  properties whose names begin with `%`: `el['%classList'] = 5` makes
  `el.classList` a number, and `method['%home']` is the object `super` is
  looked up from.
- **A string built half a surrogate pair at a time costs the square of its
  length**: `s += '\uD83D'; s += '\uDE00'` 20,000 times takes 4.3 s, and
  5 ms a pair at a time. One case of `long-strings.dat` does that and takes
  6 s of the 10 a case is allowed; on a busy machine it times out.

---
