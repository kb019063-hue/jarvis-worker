# Phase 69 — the custom-element construction stack

Finding 75. The companion finding is
[`docs/findings/75-an-upgrade-ran-the-constructor-on-the-element.md`](../findings/75-an-upgrade-ran-the-constructor-on-the-element.md).

## 1. What was wrong

An element that is already in the page is upgraded when its class is defined.
The HTML standard upgrades it by constructing the definition's constructor the
ordinary way — `Construct(C, «»)`, with a fresh object as `this` — and by giving
the element out from the definition's **construction stack** when the
constructor reaches its interface (`super(...)`, or
`Reflect.construct(HTMLElement, [], new.target)`); the upgrade counts only if
what the constructor returns *is* the element.

Litebird instead put the class's prototype on the element's object first and
passed that object as `this`. Everything a constructor could ask about itself
came back wrong:

- a constructor written as a plain function had `this === self`, and writing on
  `this` wrote on the element;
- `function Plain() { this.touched = true; }` — a constructor that never
  reaches `HTMLElement` — was accepted, and its upgrade counted as done;
- after a constructor that threw early the element kept the class's prototype,
  so a class that extends `Event` and was given to `customElements.define`
  produced an element with no `getAttribute`;
- a class could not tell whether it was being upgraded or `new`-ed.

Beside it, `Element::is_defined()` answered `true` for a customized built-in
whose upgrade had failed, and a `document.createElement()` whose constructor
threw did not come back as the HTMLUnknownElement the DOM standard says it is
(the state it left, `Failed`, was read by the wrapper's prototype choice).

## 2. What changed

### The stack lives in the definition

`Definition` (`src/js/DomObserverBindings.cpp`) gained the standard's
construction stack: a vector of `{element, taken}`, innermost last.

`run_upgrade` now:

1. sets the element's state to `CustomElementState::FailedUpgrade`;
2. queues the reactions the standard queues after the constructor returns
   (observed attributes, then `connected` if the element is connected), marked
   so they can be dropped;
3. pushes `{element, false}`;
4. makes the object `new` would make — fresh, with the constructor's prototype
   at that moment — and calls `definition->constructor.construct({}, &instance)`
   inside `js_capture_throw`;
5. pops the entry, noting whether the constructor took it;
6. accepts the construction only if what it made is the element: the returned
   value when that is the element's object, or, when the constructor returned
   nothing, the element if the interface handed it out (`super()` makes what it
   returns `this`, so a class with no `return` constructs the element);
7. otherwise marks that element's queued reactions done, reports the thrown
   value or `TypeError: The custom element constructor returned a different
   object.` (the standard's own step) and leaves the state failed.

`construct_custom_element` gained the branch that reads the stack: it takes the
top entry, refuses a second request in the same construction with
`TypeError: This instance is already constructed`, marks the entry, gives **the
element's own object** — the wrapper a script may have held since before the
definition existed — the class's prototype, and hands it to `super(...)` through
the new `JsValue::hand_back_constructed_object`. That is what makes `this` the
element from the moment `super()` returns, without the object `new` made ever
being the element.

The prototype is set while the constructor runs and is not rolled back when the
upgrade fails, which is what browsers do: the standard sets it as part of giving
the element out, and only the state and the reactions are undone.

### `Failed` and `FailedUpgrade` are two states

`document.createElement()` with a definition whose constructor throws returns a
new element implementing `HTMLUnknownElement` (DOM standard, "create an
element", synchronous custom elements) — the *wrapper* gets that prototype, the
element itself does not change interface. That was implemented as "state
`Failed` means HTMLUnknownElement wrapper", which the upgrade path also set, and
which is why a failed upgrade lost `HTMLElement.prototype` where a browser keeps
it. The states are separate now: `Failed` is createElement's, `FailedUpgrade` is
the upgrade's, and both are terminal (a definition is never offered to the
element again) and neither is `:defined`.

### `is_defined()`

`Element::is_defined()` (`src/dom/FormState.cpp`): a foreign element is defined;
a `Custom` element is defined; a customized built-in (an element with an `is`
value) is not; anything else is defined unless its tag is a valid custom element
name. Before, everything that was not `Custom` fell through to the name check,
so a customized built-in — whose tag is `button`, not a custom name — was `:defined`
whether or not its upgrade had worked.

### The call-context machinery went away

Phase 43 could not touch the DOM bindings, so it kept a construction-in-place
list beside the call context: `JsValue::construct` noted any construction whose
`this` was already a platform object, and `current_derived_this()` looked the
object up by `new.target`. Nothing registers in that list any more — the upgrade
no longer passes an element as `this`, and the paths that do pass a `this`
(`js_construct`'s fresh object, `super(...)`'s) never pass a DOM object. A
build that aborted on registration ran the 215 unit suites, the DOM corpus and
wpt without one, so the list, its `InPlaceConstruction`/`InPlaceLists`/
`InPlaceScope`/`is_interface_of`, its mutex, its atomic and the per-request walk
are deleted. `current_derived_this()` keeps what is still needed: the object the
class's constructor was given, or — under `construct_for()` — one made with
`new.target.prototype`.

## 3. How it was verified

The five questions that measured the fault were
`tests/dom-conformance/open/compiled-custom-elements.js` — an open probe, 1 of 5
lines the same as jsdom:

```bash
python3 tools/conformance/compare_with_jsdom.py \
    tests/dom-conformance/open/compiled-custom-elements.html \
    tests/dom-conformance/open/compiled-custom-elements.js
# before: 5 lines, 1 the same
# after:  5 lines, 5 the same
```

They are nine cases in `tests/dom-conformance/upgrades.dat` now, recorded from
jsdom 31.0.0 (the file the probe replaced is gone):

```bash
make dom-conformance
# Everyday probe (fixed 68 cases): 68/68 (100.0%)
# DOM conformance, full corpus: 678/678 (100.0%)  [26 files, 0 crashes, 0 timeouts]

python3 tools/conformance/run_dom_conformance.py --reference --quiet
# 678 / 678: every expectation is still what jsdom prints
```

The nine cases ask: `this` unbound before `super()` and `new.target`;
`super(...)` returning the element; fields initialised on the element; what is
written on `this` before it asks not reaching the element; the object a script
held before the definition staying the element's object with the class's
prototype; a constructor returning another element failing the upgrade; a
constructor throwing after `super()`; one throwing before it reaches the
interface; `super()` twice; a failed customized built-in not being `:defined`;
and the order of the reactions a `define()` upgrade queues.

`tests/test_suite.cpp` has `js_an_upgrade_constructs_the_definition_the_ordinary_way`
for the same ground from the engine's side (it is where the values a browser
gives are asserted, since the corpus is jsdom's answers):

```bash
make test
# All 215 engine test suites passed successfully!
```

Nothing else moved:

```bash
make js-conformance    # 1124 / 1150 — the same as before the change
make wpt               # 9550 / 9744 — the same
make url-conformance   # 1188 / 1188
make xml-conformance   # 1718 / 1718
make conformance       # 1792 / 1792
```

## 4. What is still open

- A private name is still a property whose name begins with `#`
  (`obj['#p']` reads and writes it) — a row of the task table in `AGENTS.md`.
- The upgrade of an element that moves between documents was described in that
  row as wrong (an element adopted out of a frame's document not being upgraded
  by the page's definition, where one from `createHTMLDocument` or `DOMParser`
  is). Measured against jsdom 31 with
  `python3 tools/conformance/compare_with_jsdom.py`, five lines covering a
  frame document adopted into the page, `createHTMLDocument` and `DOMParser`
  agree: all three are upgraded by the page's definition, and the element is the
  page's. It was not reproducible, so nothing was changed for it and no case was
  written; the row is gone with the rest.
- `eval_super_call` keeps its branch for a `super(...)` call whose `this` is
  itself a platform object (`class X extends Map` given to
  `customElements.define`). With the new construction model that branch is no
  longer reached by an upgrade — what it describes is now handled by
  `take_over()` into the object `new` made — but it is cheap, and no measurement
  says nothing else reaches it.
- `document.createElement()` of a customized built-in whose constructor throws
  hands back an `HTMLUnknownElement` prototype (DOM standard); the element is
  otherwise the one the constructor built, and the wrapper is not replaced. No
  case asks whether a browser's replacement element keeps the attributes it had
  — it has none — so this was left as it is.
