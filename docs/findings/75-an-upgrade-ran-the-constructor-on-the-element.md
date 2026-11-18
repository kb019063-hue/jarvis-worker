# Finding 75 — An upgrade ran the constructor on the element itself

Phase 69. The implementation notes are in
[`docs/phases/69-custom-element-construction-stack.md`](../phases/69-custom-element-construction-stack.md).

## 1. What was wrong

`customElements.define()` upgrades the elements of the page that already carry
the name, and `customElements.upgrade()` does the same for a detached tree. The
HTML standard does it by constructing the definition's constructor the ordinary
way — a fresh object as `this` — and giving the element out from the
definition's construction stack when the constructor reaches its interface
(`super(...)` or `Reflect.construct(HTMLElement, [], new.target)`); the upgrade
is a failure unless the constructor comes back with the element.

Litebird put the class's prototype on the element's object and passed that as
`this`. A script could tell:

- in a constructor written as a function, `this === self` was `true`;
- `function Plain() { this.touched = true; }` never reaches `HTMLElement`, and
  the element was accepted anyway — its upgrade counted as done;
- after a constructor that threw on its first line the element kept the class's
  prototype, so a class that extends `Event` and was given to
  `customElements.define` left an element that has no `getAttribute`;
- `Object.getPrototypeOf(element)` inside the constructor was the class's, where
  the standard has the object `new` made.

Two neighbours of the same question were wrong as well. `Element::is_defined()`
asked only whether the element's state was `custom` and otherwise fell through
to a name check, so a **customized built-in** — a `<button is="x-b">` — matched
`:defined` after its upgrade had failed. And the state a failed
`document.createElement()` left (`Failed`) was also the state a failed upgrade
left, which made the wrapper of a failed upgrade an `HTMLUnknownElement` where a
browser keeps `HTMLElement`.

## 2. What changed

The stack is the definition's now (`src/js/DomObserverBindings.cpp`):
`run_upgrade` pushes the element, constructs the constructor on an object of its
own, and compares what comes back with the element; `construct_custom_element`
takes the entry, marks it (a second `super()` is `TypeError: This instance is
already constructed`), gives the element's own object the class's prototype and
hands it to `super(...)` through `JsValue::hand_back_constructed_object`, so
that object becomes `this`. A constructor that throws, never reaches the
interface, or returns something else leaves the element not `:defined`, told
nothing, with the prototype the interface gave it still on it — which is what a
browser does, and what the measurement showed.

`CustomElementState` gained `FailedUpgrade` beside `Failed`; only the latter is
createElement's, and only it picks the `HTMLUnknownElement` wrapper.
`Element::is_defined()` answers `false` for an element that has an `is` value
and is not `custom`.

The construction-in-place list that Phase 43 kept beside the call context
(`JsValue::construct` noting a construction whose `this` was a platform object,
`current_derived_this()` looking it up by `new.target`) is deleted, with its
mutex and atomic: a build that aborted on registration ran the unit suites, the
DOM corpus and wpt without one.

## 3. How it was verified

```bash
python3 tools/conformance/compare_with_jsdom.py \
    tests/dom-conformance/open/compiled-custom-elements.html \
    tests/dom-conformance/open/compiled-custom-elements.js
# before: "5 lines, 1 the same"      after: "5 lines, 5 the same"

make dom-conformance
# DOM conformance, full corpus: 678/678 (100.0%)  [26 files, 0 crashes, 0 timeouts]

python3 tools/conformance/run_dom_conformance.py --reference --quiet
# 678 / 678 — the expectations are still jsdom's answers

make test
# All 218 engine test suites passed successfully!
```

The probe's five lines are nine recorded cases in
`tests/dom-conformance/upgrades.dat`; `js_an_upgrade_constructs_the_definition_the_ordinary_way`
in `tests/test_suite.cpp` asserts what a browser does, where jsdom and a browser
might disagree.

## 4. What is still open

- Private names are still properties whose name begins with `#`: `obj['#p']`
  reads and writes a private field from outside its class. Unrelated to this
  change, and a row of its own in `AGENTS.md`.
- The claim, in the same open-task row, that an element moving from a frame's
  document into the page is not upgraded by the page's definition turned out
  not to be reproducible: five lines measured with `compare_with_jsdom.py`
  (`from a frame document`, `from createHTMLDocument`, `from DOMParser`, and an
  element parsed in the frame and then adopted) agree with jsdom — all are
  upgraded, all belong to the page afterwards. Nothing was changed for it.
- `eval_super_call`'s branch for a `super(...)` whose `this` is a platform
  object (`class X extends Map` given to `customElements.define`) is no longer
  reached by an upgrade — the "element is not made a map" case is now handled by
  `take_over()` into the object `new` made — but it was left in place: no
  measurement says nothing else reaches it.
- `Reflect.construct(HTMLElement, [], new.target)` in a constructor of a class
  whose interface is not an element's (`class AsText extends Text`) still builds
  on an object of its own and has its state taken over; with the new model the
  object the constructor runs on is already one of its own, so this is unchanged
  behaviour, not a new case. No browser was available to compare against, and
  jsdom agrees with the corpus.
