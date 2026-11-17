# Phase 43 Architecture: `new.target`, and What a Constructor Makes

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 43 Architecture: `new.target`, and What a Constructor Makes` section.
> The text below is the section as it stood, unchanged.


### 1. What was wrong

`new.target` is the function `new` was applied to. In a constructor that a
subclass reached with `super(...)` it is the subclass; with
`Reflect.construct(F, args, G)` it is `G` inside `F`. On `main` it was the
function that was running. `invoke_function` bound it on entry, to the callee
whenever the call was a construction, and nothing could have told it more:
`JsValue::construct(args, this)` took the arguments and the object, and
`super(...)` and `Reflect.construct` both ended there.

Three more things about what a constructor makes were wrong beside it, each
of them a few lines of `eval_super_call` or `js_construct`:

- `super(...)` ran the parent's constructor on the object `new` had made and
  dropped what came back. A parent that returns an object of its own (a
  function that returns `Object.create(new.target.prototype)`, a class that
  hands out one instance, a compiled class) was lost: the child went on with
  the other object, and that one was what `new` gave. The value of
  `super(...)` itself was `undefined`.
- `this` could be bound twice, or never: a second `super(...)`, a derived
  constructor that returns without one, and one that returns a number were
  all accepted.
- `Reflect.construct(Native, args, NewTarget)` ran the built-in with no
  object and gave what it built the prototype of `NewTarget` afterwards.
  That is right for an array or an error. A DOM interface builds on the
  object `super()` hands it and refuses to run without one, so
  `Reflect.construct(HTMLElement, [], Derived)` was `Illegal constructor`.

| A script | `main` | Now, and in Node |
| :--- | :--- | :--- |
| `class Shape { constructor() { if (new.target === Shape) throw new TypeError('abstract'); } }`, `class Circle extends Shape {}`: `new Circle()` | `TypeError: abstract` | a `Circle` |
| `new.target.name` in `A`, and in `B extends A` after its `super()`: `new C()` for `class C extends B {}`, then `new B()`, then `new A()` | `A,B,A,B,A` | `C,C,B,B,A` |
| `function F() { this.nt = new.target; }`: `Reflect.construct(F, [], G).nt === G` | `false` | `true` |
| `class A { constructor() { this.f = () => new.target; } }`, `class B extends A {}`: `new B().f() === B` | `false` | `true` |
| `class MyErr extends Error { constructor(m) { super(m); this.who = new.target.name; } }`, `class Sub extends MyErr {}`: `new Sub('m').who` | `MyErr` | `Sub` |
| `function Parent() { return custom; }`, `class Child extends Parent { constructor() { super(); this.added = 2; } }`: `new Child() === custom`; `custom.added` | `false`; `undefined` | `true`; `2` |
| `class Child extends Parent { f = 7; }` over the same parent: `custom.f` | `undefined` | `7` |
| `var r = super()`: `r === this` | `false` | `true` |
| `constructor() { super(); super(); }` | accepted | `ReferenceError`, after the parent has run a second time |
| `class B extends A { constructor() {} }`: `new B()` | an object | `ReferenceError` |
| `constructor() { super(); return 1; }`, and the same with `null` | an object | `TypeError` |
| `Reflect.construct(F.bind(null), [], G)`: `new.target` in `F` | `F` | `G` |
| The same when `F` returns an object of its own: that object's prototype afterwards | `G.prototype` | what it was |
| `Reflect.construct(Function, ['return 3'], G)`: the prototype of the function made | `Function.prototype` | `G.prototype` |
| `Reflect.construct(F)`, with no list of arguments | accepted | `TypeError` |
| `function Old() { var self = Reflect.construct(HTMLElement, [], new.target); self.made = true; return self; }`, given a prototype and to `customElements.define`: `new Old()`, `document.createElement('x-old')` | `TypeError: Illegal constructor`; an element that is not an `Old` | an `Old`, both (the last two rows: as in jsdom) |
| An `<x-old>` that stands in the page when that `define` runs: `el.made` | `undefined`, after an uncaught `Illegal constructor` | `true` |

Who met it. The abstract class of the first row is the usual way to write
one. An error class that calls itself after `new.target` (`this.name =
new.target.name`) named every subclass after the parent. A parent that
returns an object is how a singleton and a factory are written, and it is
what every class compiled for older browsers does: Babel turns `super(...)`
into `Reflect.construct(Super, arguments, NewTarget)` and goes on with what
that returns, so a modern class that extends a compiled one lost its
instance. And a custom element compiled that way could neither be made nor
take over an element of the page. Phase 34, which made compiled classes run
as far as their helpers go, listed both as open (its section 9), with a
failing case and five lines of
`tests/dom-conformance/open/compiled-custom-elements.js`.

### 2. `new.target` goes in with the call

A call in this engine has a context beside its arguments (`CallContext` in
`src/js/JsValue.cpp`, one per thread): who `this` is, whether it is a
construction, which function is called. It has one field more now,
`new_target`, and null there means the function that is called, which is
every construction but two kinds:

```cpp
JsValue construct(const std::vector<JsValue>& args, const JsValue* this_val = nullptr,
                  const JsValue* new_target = nullptr) const;
JsValue construct_as_super(const std::vector<JsValue>& args, const JsValue& derived_this,
                           const JsValue* new_target = nullptr, JsValue* in_place = nullptr) const;
```

`super(...)` passes on the `new.target` of the constructor it stands in, and
`Reflect.construct` passes its third argument. A script function reads it
with the rest of the context (`take_call_context`), a native function with
`JsValue::current_new_target()`, which answers null unless another function
than the native itself was named.

Inside a script function `new.target` is a binding of the function's scope,
`%new.target`, as before: made when the function is a class constructor or
its body, or an arrow function in it, says `new.target` (the parser marks the
nearest function that is not an arrow), so an arrow function finds it along
the scope chain. What changed is the value it is given on entry: undefined
in a call, the named function when there is one, the callee otherwise.
`eval_super_call` reads the binding where `super(...)` stands and hands a
copy up.

**The way in is an argument.** The callable that `make_function` gives a
script function reads the context and calls `invoke_function`, its only
caller, with one argument more: a pointer to the named function, null when
`new.target` is the callee or the call is no construction. It points at what
the caller of `construct()` holds, which stays for as long as the call runs,
so nothing is copied on the way in. (The first version of this passed it
through a thread-local slot and a copy, because the header that declares
`invoke_function` belonged to another open pull request; section 7 has what
that cost.)

The bytecode VM is not involved: the compiler leaves a function that says
`new.target` or `super` to the interpreter.

### 3. `super(...)` binds `this`, once

`eval_super_call` constructs the parent with the object `new` made and the
`new.target` of this constructor. Then:

- **What the parent gave back is `this`.** A script parent, or a bound
  function, that returns an object other than the one it was given: that
  object is `this` from here on, as it is, with the prototype it has. The
  binding named `this` that the `super(...)` expression sees is given the
  new value, which is the constructor's also when the expression stands in
  an arrow function. The class's fields are then defined on that object, and
  the value of the expression is `this`.
- **A built-in parent** builds its own object and `this` takes over its
  state, as since Phase 34; a DOM interface is handed `this` and makes it
  the thing it constructs. Both are told `new.target` now. (An interface can
  come back with another object than the one it was handed; that is
  section 5.)
- **What the constructor returns.** When a derived constructor's body
  returned nothing and `super(...)` bound `this` to another object than the
  constructor was entered with, `invoke_function` returns that object.
  Whoever called `construct()` (`new`, `Reflect.construct`, a subclass's
  `super(...)`) already preferred an object that comes back to the one it
  had passed in, so the object a parent three classes up returned travels
  down through each `super(...)` to the `new` expression.

`this` is bound when the parent has returned, and only once. The scope of a
derived constructor has a field for it (`JsEnvironment::this_binding`:
unbound, bound, or bound to another object than the constructor was entered
with), and `super(...)` finds that scope by walking out through the blocks
and arrow functions it stands in. `eval_super_call` sets it after the parent
has run and throws a
`ReferenceError` if it was set already: the language runs the parent the
second time and refuses the result, and a script can count the runs. The
second run is given a spare object, so that what the first one made is left
as it was (`class M extends Map { constructor() { super([[1, 2]]); try {
super(); } catch (e) {} } }` still has its entry). `invoke_function` looks at
the flag when the body returned nothing, and throws a `ReferenceError` if
`super(...)` never finished; a body that returned something that is neither
an object nor undefined is a `TypeError`. A class that extends null gets
both without a line of its own: its default constructor calls a parent that
is not a constructor, and one written out either returns an object or never
binds `this`.

What this does not do: reading `this` before `super(...)` has run is still
not an error (section 8).

### 4. `Reflect.construct`, and `new` on a bound function

`js_construct(ctor, args, new_target)` is behind `new`, `Reflect.construct`
and a bound function. Until now its third argument decided one thing, whose
`prototype` the object gets. Now:

| `ctor` is | What happens |
| :--- | :--- |
| a script function or class | The object is made with `new_target.prototype`, read once before the function runs, and the function is constructed on it with that `new.target`. What it returns, if an object, is the result |
| a built-in, and no other `new_target` | As before: it builds its own object |
| a bound function | It is constructed with the `new_target`; its native reads `current_new_target()` and calls `js_construct(target, bound and given arguments, that)`. What comes back is the result as it stands, with no prototype given to it: the target made the object, or returned one of its own. The function carries a mark for this (`Extra::forwards_construct`, set by `bind`), which `super(...)` in a class that extends a bound function reads too |
| any other built-in, with a `new_target` | `JsValue::construct_for` runs it. If it asks for the object it is to build on (`current_derived_this()`, which only the DOM's interface constructors do), one is made then, with the prototype `new_target` has at that moment, and what the built-in makes of it is the result. If it never asks, it builds its own and that is given the prototype afterwards |

The order in the last row is the language's: a built-in looks at its
arguments first and at `NewTarget.prototype` when it makes its object.
`Reflect.construct(Promise, [], NewTarget)` has to complain that there is no
executor without having read the property, and one test262 test asks exactly
that with a getter that throws. (The first version of this change made the
object before running the built-in, and that test, which `main` passes, was
how it was found.) On every path the property is read once; where it is not
an object, a script function's object gets `Object.prototype` and a
built-in's keeps the prototype the built-in gave it.

`Reflect.construct` itself now refuses a list of arguments that is not an
object, after it has found both functions to be constructors.

### 5. The DOM: a custom element compiled for older browsers

No file of the DOM bindings changed: they belonged to another open pull
request (#49, which merged while this one was being verified; what was left
for that reason is at the end of this section). Two things in the engine
make such an element work.

**Made new.** The constructor of a DOM interface already asked for the
object a subclass's `super()` hands it and made the element, or the event,
on that object. `Reflect.construct(HTMLElement, [], Old)` now offers it one
the same way (section 4), with `Old.prototype`, and
`construct_custom_element` finds the definition by that prototype as it does
for `new MyElement()`:

```js
function Old() { var self = Reflect.construct(HTMLElement, [], new.target); self.made = true; return self; }
Old.prototype = Object.create(HTMLElement.prototype, { constructor: { value: Old, writable: true, configurable: true } });
Object.setPrototypeOf(Old, HTMLElement);
customElements.define('x-old', Old);
new Old().localName                                 // "x-old"; on main: TypeError: Illegal constructor
document.createElement('x-old') instanceof Old      // true; on main: false, after an uncaught "Illegal constructor"
```

**Upgraded.** Most custom elements are not made by a script. They stand in
the page's markup, come out of `innerHTML`, are cloned, or exist before
`customElements.define` runs, and each of those is an element that exists
and is *upgraded*: `run_upgrade` runs the definition's constructor with the
element's own object as `this`. A class written as a class reaches
`HTMLElement` through `super()` with that object, and the interface sees
that it is an element already. A constructor compiled for older browsers
does not pass `this` on. It calls `Reflect.construct(HTMLElement, [],
new.target)` and goes on with what comes back; or, when its parent is a
class (a library ships classes, the application is compiled), it calls
`Reflect.construct(Parent, arguments, NewTarget)`, the class is constructed
on a new object, and it is the class's `super()` that reaches `HTMLElement`,
with that new object. Either way the only thing that ties the call to the
upgrade it is part of is `new.target`.

So `JsValue::construct()` notes a construction whose `this` is a platform
object already, a *construction in place*, for as long as it runs, under the
function `new` is applied to (a list in `src/js/JsValue.cpp`;
`super(...)` constructing a script parent on the same object adds nothing to
it). The list belongs to the agent, not to the thread, unlike the call
context: the body of a generator, and of an async function that awaits, runs
on a thread of its own in step with whoever resumed it, and a constructor
that reaches its interface from there is part of the same upgrade. (The
first version had a list for each thread, and such a constructor was
refused.) One thread of an agent runs at a time, so a list has no lock; the
table of lists has one, because a worker is an agent on another thread, and
a counter of running constructions lets every other construction skip it.
When the constructor of an interface asks for the object it is to build
on (`current_derived_this()`), the list is looked at first. If the call's
`new.target` is being constructed in place, on an element of the interface
that asks, the answer is that element, whatever the call came with, and it
is given once: a second request while the same construction runs is a
`TypeError`. `construct_as_super` tells `eval_super_call` when the interface
built on another object than the one `super(...)` offered, and that object
becomes `this`, the way a script parent's returned object does (section 3).

Only the element's own interface is given it: `HTMLElement` for an element
with a name of its own, the interface of its tag for a customized built-in
(`is_interface_of()`, which asks the element's realm for that constructor).
Any other interface builds on an object of its own, also when `super(...)`
came with the element. So `Reflect.construct(Event, [], new.target)` in the
constructor of a custom element makes an event, and a class that extends
`Event`, or `HTMLElement` where its definition is for a `button`, and is
given to `customElements.define` all the same, no longer has an element
turned into something else under it: its upgrade fails, as the standard has
it.

A parent that is no interface at all is the same question without the list:
`class M extends Map {}`, given to `customElements.define`. A built-in
parent builds its own object and `eval_super_call` lets `this` take over its
state (section 3). With an element as `this`, that made the element's object
a map (an error, an array), and for an interface that builds its own node
whatever it is offered (`Text`, `DocumentFragment`) the wrapper of another
node: `el.nodeType` was 3. `eval_super_call` takes nothing over into a
platform object any more. What the parent built is `this` from there on,
with the prototype `new.target` has, the way `Reflect.construct(Map, [],
NewTarget)` makes it; the constructor returns that, and the upgrade fails
because it is not the element.

This is the HTML standard's *construction stack*, which each definition has
there: the element being upgraded is pushed on it, the `HTMLElement`
constructor finds the definition by `new.target`, returns the element on top
and leaves a marker in its place. The consequences are the standard's too,
and jsdom's (all of these are cases of `compiled.dat`):

| | What happens |
| :--- | :--- |
| An element of the page, one from `innerHTML`, a clone, one made before `define` or in a tree given to `customElements.upgrade` | Upgraded by the compiled constructor, and then told its observed attributes and that it is connected, in that order |
| What Babel emits (`_wrapNativeSuper`, `_createSuper`), two classes deep | The same: each level constructs the next with the same `new.target` |
| A subclass compiled for ES5 of a class that extends `HTMLElement` | The compiled constructor constructs the class on a new object; the class's `super()` is given the element, which is `this` in the class from there on, with the class's fields on it |
| A class on top of a compiled constructor | `super()` runs the function on the element with the class as `new.target`; the function gets the element back, and the class's fields go on it |
| A customized built-in (`Reflect.construct(HTMLButtonElement, [], new.target)`, `<button is="fancy-b">`) | The same. One that asks `HTMLElement` instead is refused |
| The constructor asks twice during an upgrade | The second request is a `TypeError`. Under `new`, where nothing is being upgraded, each request makes an element |
| A second instance of the class made while it is being upgraded | Before the constructor reached its interface: the inner instance is the element being upgraded, and the constructor's own request is the one refused. After: the inner one is refused. For a class and for a compiled constructor alike |
| The constructor throws after it got its element | The upgrade fails; later constructions get elements of their own |
| A class whose parent is `Map`, `Error`, `Array`, `Text` or `DocumentFragment`, given to `customElements.define` | `this` in its constructor is a map (an error, an array, a text node) of its own; the element's object is not made one, and its upgrade fails. (It keeps the class's prototype, like every element whose upgrade failed: what is left, below) |
| A frame (Phase 42 gave each its own interfaces and registry; unit suite only, jsdom does not load the frame) | The interface that is given an element is the one of the element's realm: a compiled constructor on the frame's `HTMLElement` upgrades an element of the frame, and the page can define the same name with another. One that asks the page's `HTMLElement` for an element of the frame gets none (`Illegal constructor`): the standard looks the definition up in the registry of the interface that is called |
| The constructor reaches its interface from the body of a generator or of an async function | The same element, though that body runs on another thread. A generator that was started during an upgrade and is resumed after it makes an element of its own |

**What is left** is in how `run_upgrade` calls the constructor, which is in
`src/js/DomObserverBindings.cpp`. The standard constructs it the ordinary
way: `this` is a new object, the element gets the class's prototype when
`HTMLElement` hands it out, and the upgrade fails unless the constructor
returns the element. Here the element is given the prototype first and is
`this`. A class cannot tell, and neither can a compiled constructor that
does what it is for. Three things can
(`tests/dom-conformance/open/compiled-custom-elements.js`, 5 lines, 4
differ): in a constructor written as a function
`this === self` is `true`; one that never reaches `HTMLElement` (`function
Plain() { this.touched = true; }`) has written on the element and its
upgrade counts as done, where the standard refuses it; and after a
constructor that throws before it reaches `HTMLElement` the element keeps
the class's prototype. The fourth line is the selector's: a customized
built-in whose upgrade failed still matches `:defined`. The last three
print the same on `main`; the first could not be asked there, because the
constructor it is asked of did not get its element. A row of the task
table.

### 6. How this is verified

In the sandbox, and on two trees, because `main` moved while this was
measured. **The rows that compare with `main`** are from this branch with
`main` at `95d7fb2` merged in (commit `357b861`), against a release build of
that `main`: "the binary of `main`". Since then this branch has changed no
source file of the engine; what came is `main`'s. **The rows that only
check** are from the tree as it is merged, four pull requests of `main`
later (`a4952ed`: text on a canvas, `<select>` in the HTML parser, the
attribute model, CSS colours). None of the four touches how a function is
called or constructed. The attribute model moved the web-platform-tests from
7,968 to 7,998 and the parser work moved html5lib to all 1,792, so those two
are not the numbers this change was developed against.

| Check | Tree | Result |
| :--- | :--- | :--- |
| JavaScript corpus (`make js-conformance`) | merged | 974 / 989 in 25 files. `construct.dat` is new: 49 cases, 47 pass, and the two that do not are in section 8. `prototypes.dat` is at 68 / 76: the case on `new.target` in a parent's constructor, recorded to fail in Phase 34, passes |
| The same corpus in the binary of `main`, and in Node (`--reference "node -e"`) | `357b861` | `main`: 939 of the 989, 13 of the 49 of `construct.dat`, 67 of the 76 of `prototypes.dat`. Node: 989 / 989 |
| Two probes outside the corpus, in Node, in the binary of `main` and here: 20 rows on what `new.target` is, 24 on when `this` is bound (`super(...)` in an arrow function, twice, after the parent threw, in a class declared inside a constructor) | `357b861` | The same output as Node on all 44. The binary of `main` differs on 19 of the 20 and 14 of the 24 |
| test262, the 21 directories on functions, classes and objects (`make test262`) | `357b861` | 15,686 tests: 12,316 pass, 12,275 in the binary of `main`. 41 that failed pass; none that passed fails |
| test262, all of `built-ins`, `language` and `annexB` (the same runner with those three names) | `357b861` | 47,967 tests: 27,872 pass, 27,829 in the binary of `main`. 43 that failed pass; none that passed fails |
| DOM corpus (`make dom-conformance`) | merged | 669 / 669 in 25 files; `compiled.dat` is new, 25 cases, each recorded from jsdom 31 |
| The same corpus in the binary of `main`, and in jsdom (`--reference`) | `357b861` | `main`: 646 of the 669, 2 of the 25 of `compiled.dat`. jsdom: 669 / 669 |
| Unit tests (`make test`) | merged | 191 / 191; six are new: `new.target` (32 assertions), `super(...)` and `this` (16), the collector on both (3), compiled custom elements constructed (10) and upgraded (24, with a frame, a generator, and parents that are no elements), and a page of them collected (6) |
| Pages that are not in the corpus (`tools/conformance/compare_with_jsdom.py`): lines as jsdom prints them, here and in the binary of `main` | `357b861` | Classes and compiled constructors mixed on one page: 9 of 9, and 1. A constructor that reaches its interface from a generator and from an async function: 6 of 6, and 0; 5 of 5, and 3. An element adopted from another document: 8 of 8, and 3. Constructors written as functions: 7 of 7, and 0. A constructor that makes a second instance of its class: 3 of 4, and 2. Classes that are no elements, given to `customElements.define`: 5 of 13, and 3; the lines that differ there, the one on the page before it, and four of the five of `tests/dom-conformance/open/compiled-custom-elements.js` (1 of 5, and 0) are what `run_upgrade` does (section 5, "What is left") |
| web-platform-tests, html5lib, URL, XML (`make wpt`, `make conformance`, `make url-conformance`, `make xml-conformance`) | merged | 7998 / 8009 subtests in 103 files, 11 listed and none failing that is not; 1792 / 1792; 1188 / 1188; 1718 / 1718: what `main` has |
| `make diff-trees`, `make diff-xml`, `make diff-strings`, `make verify-promise-iterators` | merged | 0 of 6,000 trees differ from parse5's; 0 of 4,000 documents and 0 of 3,000 round trips differ from Expat's; 95 of 50,580 string results differ from Node's, as on `main`; 34 of 34 cases |
| `make deep`, one page at a time (two at a time, as the target runs them, need more than the 3.9 GB of the sandbox) | merged | `deep pages: 1332 runs over 465 pages at depth 200000 on a 8 MB stack, 0 failed; slowest 21.5 s (table [dumps])` |
| `make asan` (ASan + UBSan; `make fuzz` runs in that build) | `b740984`, the last commit that changed the engine; five pull requests of `main` have come since | Exit 0 and no report from either sanitizer: 183 / 183 suites, JavaScript 973 / 989 (one fewer than the normal build, as on `main`: `d(2000)` recursing needs more stack than a sanitizer build's frames leave), DOM 669 / 669, URL 1188 / 1188, XML 1718 / 1718, WPT 7968 / 8009 with the 41 that were listed then, 1,000 generated XML documents and 1,000 round trips, 400 + 400 + 600 fuzz runs and 1,800 of media with 0 findings, 882 deep runs over 315 pages. On the merged tree CI's sanitizer job is what runs it |
| CI | | The runs are listed in the pull request. Its sanitizer job failed twice on this branch with nothing to read, passed once, and failed a third time when it could say why: AUDIT.md, finding 49, has what it said and what was changed |

**What test262 found.** The corpus here is recorded from Node and was
written with the change; test262 was written by neither. It is run in the
binary of `main` and in this one, for the difference and not for the score
(58.1% of the suite passes).

The first whole run, when only `new.target` was done, had one test that
passed on `main` and failed here:
`built-ins/Promise/get-prototype-abrupt-executor-not-callable.js`. That
version made the object for a built-in before it ran the built-in, and so
read `NewTarget.prototype` before `Promise` had looked at its executor; the
test has a getter there that throws, and asks which error comes first. It is
why `construct_for` makes the object when the built-in asks for it (section
4), and it is a case of `construct.dat` now.

In the run of the table 43 tests that fail on `main` pass and none that
passes fails. 32 of them are in `language/statements/class`: the
`super-must-be-called` test of every built-in a class can extend, and what
a derived constructor may return. The others are in
`language/expressions/super` (4), `language/expressions/new.target` and
`built-ins/Function/internals/Construct` (2 each), and one each under
`arrow-function`, `class/elements` and `optional-chaining`. Six tests end
differently in the two builds without passing in either: they ask for an
array or a list near 2^32 entries long and are stopped by memory or by the
clock, whichever comes first that time.

**What jsdom is not right about here.** Two answers of jsdom 31 differ from
Web IDL, and the unit suite asserts the standard's instead (both are rows of
the table in `tests/dom-conformance/README.md` now): for
`Reflect.construct(Event, ['z'], NewTarget)` jsdom reads
`NewTarget.prototype` twice, and when that is not an object it makes
something that is not an event (`instanceof Event` is `false` and `type` is
undefined), where the standard falls back to `Event.prototype`.

### 7. What it costs

Release builds of `357b861` and of `main` at `95d7fb2`. Each measure in two
copies of the binary of `main`, to see how far one binary is from itself,
and in this one, turn about, five processes each; in a process the best of
five loops of 15,000 to 60,000 operations, with a collection after each loop
(without one a loop of this kind fills the machine: Known gaps, 4).
Nanoseconds for one operation. The ratio is to the faster of the two copies.
The last column is what the first version of this change measured, as the
same ratio.

| | `main` | `main` again | Now | Now / `main` | First version / `main` |
| :--- | ---: | ---: | ---: | ---: | ---: |
| Call a function | 701 | 693 | 701 | 1.01 | 1.03 |
| Call a method | 627 | 609 | 634 | 1.04 | 1.05 |
| Call an arrow function | 502 | 508 | 508 | 1.01 | 1.03 |
| `array.map` with a callback, for each element | 391 | 380 | 414 | 1.09 | 1.05 |
| An object literal | 300 | 315 | 309 | 1.03 | 0.99 |
| `new` on a function | 904 | 871 | 898 | 1.03 | 1.09 |
| `new` on a class | 1054 | 1042 | 1085 | 1.04 | 1.07 |
| `new` on a class with a parent | 2193 | 2143 | 2238 | 1.04 | 1.14 |
| `new` on a class three levels deep | 3187 | 3159 | 3242 | 1.03 | 1.20 |
| `new` through default constructors, four levels | 4368 | 4375 | 4462 | 1.02 | 1.24 |
| `new` on a class with a parent and two fields | 2362 | 2403 | 2438 | 1.03 | 1.12 |
| `new` on a subclass of `Map` | 1384 | 1400 | 1428 | 1.03 | 1.33 |
| `new` on a subclass of `Error` | 2074 | 2139 | 2145 | 1.03 | 1.14 |
| `new Map()` | 350 | 354 | 341 | 0.97 | 0.99 |
| A function that reads `new.target` | 989 | 944 | 1003 | 1.06 | 1.05 |
| A class on a function that returns another object | 2068 | 2047 | 2169 | 1.06 | 1.25 |
| `new` on a bound function | 1033 | 1013 | 1022 | 1.01 | 1.06 |
| `Reflect.construct(F, [1])` | 1088 | 1081 | 1104 | 1.02 | 1.10 |
| `Reflect.construct(F, [1], G)` | 1124 | 1125 | 1172 | 1.04 | 1.07 |
| `Reflect.construct(Array, [], G)` | 626 | 642 | 675 | 1.08 | 1.08 |
| `createElement` of a custom element (a class) | 1807 | 1833 | 1858 | 1.03 | 1.28 |
| `new` on a custom element class | 1426 | 1378 | 1491 | 1.08 | 1.31 |
| `new` on a custom element class two levels deep | 2328 | 2298 | 2427 | 1.06 | 1.39 |
| `innerHTML` with 200 custom elements, parsed and upgraded: for each | 3696 | 3614 | 3912 | 1.08 | 1.18 |
| `innerHTML` with 200 `div` elements: for each | 1343 | 1358 | 1317 | 0.98 | 1.02 |
| `createElement` of a `div` | 684 | 702 | 703 | 1.03 | 1.03 |
| `new` on a subclass of `Event` | 1919 | 1918 | 1999 | 1.04 | 1.13 |
| `new Event()` | 732 | 761 | 704 | 0.96 | 1.02 |

**What the first version cost, and why.** A third of a microsecond for every
`super(...)`: `new` on a class three levels deep was 20% slower, on a
subclass of `Map` 33%, on a custom element class two levels deep 39%. Two
things did it, and both were there because the header that declares
`invoke_function` and the scope belonged to another open pull request when
they were written. `new.target` went into a function through a thread-local
slot, which every call wrote and cleared, with a copy of the function for
every construction. And whether `this` was bound was a binding with a name,
declared each time a derived constructor was entered and looked up by that
name three times before the constructor returned, in a scope that keeps its
bindings in a vector of names. That pull request has merged; `new.target` is
an argument that points at what the caller holds, the flag is a field of the
scope, and a derived constructor's `this` is read back from the scope only
when `super(...)` gave it another object.

**What it costs now.** In the run of the table the 28 measures stand between
0.96 and 1.09 of `main`, and the two copies of `main` are up to 5% apart
from each other. A run of the same code an hour before, in which the two
copies were within 3%, had them between 1.00 and 1.10: `new` on functions
and classes at 1.04 to 1.06, a subclass of `Map` at 1.09, a class on a
function that returns another object at 1.10, `Reflect.construct` at 1.06 to
1.08, the custom-element measures at 1.08 to 1.09, plain calls at 1.02 to
1.04. Three of the measures run none of the changed code (an object literal,
`new Map()`, `new Event()`); in the table they stand between 0.96 and 1.03,
and that much is the machine and where the linker put things. So no row says
much by itself. What the two runs have in common is that `new` on a custom
element class, `innerHTML` with custom elements and
`Reflect.construct(Array, [], G)` are at 1.06 to 1.09 in each, and that
nothing is above 1.10: carrying `new.target` and binding `this` once costs a
construction about a tenth at most, and this machine cannot say how much
less.

(Corrected in Phase 63. As merged, this paragraph also spoke of a third run
with a subclass of `Error` at 1.11, of four runs of the three measures, and
said nothing was above 1.11. Those sentences were written from memory: the
two runs described above are the ones there is a record of, and their
largest ratios are 1.09 and 1.10.)

A construction in place (an upgrade) takes a lock to note itself and one
each time an interface asks which object it is to build on. While none is
running no construction takes a lock: the count of those that are running
is read first.

### 8. What is still open here

Found on the way, the same on `main`, and in the task table now, as rows of
their own or added to the row they belong to. The first two have a failing
case each in `construct.dat`.

- **`this` can be used before `super(...)` has run.** *(Fixed in Phase 59 /
  finding 65; kept here for context from Phase 43.)* In a derived constructor
  `this.x = 1; super();` writes to the object `new` made, where the language has
  a `ReferenceError`; an arrow function that reads `this` and `super.m` behave
  the same. The engine has no binding that exists and cannot be read yet: `let`
  and `const` have none either (`typeof z` before `let z` is `undefined` here
  and a `ReferenceError` in Node, and a `let` in a block does not hide an outer
  variable above its own line).
- **`eval` inside a function is global code.** It sees no local variable, no
  parameter, no `this`, no `new.target` and no `super`: `function f() { var
  local = 5; return eval('local + 1'); }` is a `ReferenceError`, and
  `eval('super()')` in a constructor is a `SyntaxError`.
- **A private name is a property whose name begins with `#`.** `o.#p` for an
  object that has no such field is `undefined`, and assigning to it adds
  one, where both are a `TypeError`; `obj['#p']` reads and writes the
  private field from outside the class; `#p in o` is a syntax error. (Met
  while writing the case for fields on a returned object.)
- **`new Map(iterable)` and `new Set(iterable)` do not call the `set` and
  `add` of the object they fill**, so a subclass that overrides one is not
  asked. (Added to the row on built-ins and subclasses.)
- **An upgrade runs the constructor on the element itself** (section 5):
  `this` in a constructor written as a function is the element, one that
  never reaches `HTMLElement` is accepted, and the element has the class's
  prototype before the constructor has run.
- **A script's own property whose name begins with `#`, `%` or `@@`, or has
  the shape `__x__`, is left out of `Object.keys`, `JSON.stringify`,
  `for…in`, `Object.assign` and spread**, because that is how the engine
  hides its own slots and private names: `var m = {}; m['#/route'] =
  'home'; JSON.stringify(m)` is `{}`. (Met while looking at how a private
  name is stored; added to the row on the engine's slots.)

What Phase 34 listed and this did not touch: a built-in does not make the
subclass it was called on and there is no `Symbol.species`, `instanceof`
does not ask `Symbol.hasInstance`, nothing tells a built-in that is a
constructor from one that is not (so `class X extends f.bind(null)` is
accepted, which is why a class can extend a bound function here at all), a
function's own property names, and a bound function that is never collected.
Eight cases of `prototypes.dat` fail on those.

---
