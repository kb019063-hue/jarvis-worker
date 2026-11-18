# Phase 62 — The realm of a `DOMException`, settled by measurement

(Written on 17 November 2026 as a section of `ARCHITECTURE.md` in #63, whose session went quiet before it could be merged. It was moved here when #63 was landed on 18 November, because that document no longer takes write-ups. The text is unchanged and the headings are one level up; the first paragraph was its row of the phase table, and the numbers are those of the day it was measured. The finding is [`docs/findings/68-the-realm-of-a-domexception.md`](../findings/68-the-realm-of-a-domexception.md).)

**The realm of a `DOMException`, settled by measurement:** the subtest of `tests/wpt/expected-failures.txt` listed as `test-checks-parent-global` was probed before anything changed, and it cannot be fixed without breaking 240 others. `dom/nodes/Document-createElement.html` names the *frame's* `DOMException` as the constructor a failing `createElement` on the frame's document must throw, and `domparsing/innerhtml-05.xhtml` asks for the caller's from the same shape of call. The candidate change was written, measured (**7,998 → 7,759**, 240 unlisted), and reverted; the rule the engine keeps (a node's realm is its document's, which is what browsers do) is now pinned by a unit suite instead of by a comment. | ✅ Completed (202/202 unit suites; web-platform-tests unchanged at 7,998/8,009, 11 listed, 0 unlisted; `AUDIT.md`, finding 68)

## 1. The question, and why it is asked twice

`tests/wpt/expected-failures.txt` listed three subtests as the engine's own
failures. Two of them (`strict-write`, 2 subtests) belong to the property
assignment path in `JsEval.cpp` and `JsExotic::set`, which a change in flight
owns; the third was `domparsing/innerhtml-05.xhtml`, listed as
`test-checks-parent-global`. The claim this branch opened with said the fix was
one line in `make_dom_exception` (`src/js/DomBindings.cpp`): make the
exception in the realm the call came from, which `make_function`
(`include/litebird/js/DomBindings.hpp`) already records in
`caller_intrinsics_slot()`.

## 2. What measurement said

The one-line change was written and run against the whole suite:

| | Before | With the caller's realm |
| :--- | ---: | ---: |
| web-platform-tests | **7,998 / 8,009** (11 listed, 0 unlisted) | 7,759 / 8,009 (10 listed, **240 unlisted**) |
| `domparsing/innerhtml-05.xhtml` | 0/1 | **1/1** |
| `dom/nodes/Document-createElement.html` | **743/743** | 503/743 |
| `dom/nodes/Element-createElement.html` | unchanged | 240 fail |

The 240 are one file's, three documents × 80 invalid names:
`Document-createElement.html` calls
`assert_throws_dom("InvalidCharacterError", win.DOMException, …)` for a
document of a frame, and `assert_throws_dom` compares `e.constructor` with the
constructor it was given. With the caller's realm the exception was the page's,
so 240 subtests that pass today became unexpected failures. `innerhtml-05.xhtml`
is the same shape of call (a page reaching into `window[0]`'s document) and
wants the other answer.

## 3. Which rule is the engine's

Web IDL (§3.14.3) creates a thrown exception "in the current realm", and for a
platform object the associated realm is its relevant realm (§3.1) — the realm
of the interface object, i.e. the document's. The Web IDL note's `MathUtils`
example is the borrowed-method case (`.call()` of one realm's method with
another realm's receiver); a node method whose receiver is a node of another
frame's document is the receiver's realm, and that is what the engine does and
what `make_dom_exception` must keep doing:

```cpp
Realm* owner = nullptr;
if (const JsValue& self = JsValue::current_this(); self.is_object_like()) {
    if (JsObject* cell = self.object_val; cell && cell->dom_node) {
        owner = realm_if_any(*cell->dom_node);      // the node's document's realm
    }
}
if (!owner || !owner->alive) owner = &realm;         // the method's own realm
```

That is what is in the tree today, unchanged by this phase: a node's realm is
asked first, and the method's realm is the fallback for a receiver that is not
a node (`Storage`, `Location`, an `XMLHttpRequest`). Confirmed by the suite in
`tests/test_suite.cpp`, `dom_exceptions_belong_to_the_realm_of_the_object_they_are_about`,
which puts a page and an XML frame in one process and checks six things: the
frame's `createElement` throws the frame's `DOMException` (name, `code`,
prototype and `instanceof`), a non-well-formed XML fragment through the frame's
`innerHTML` does too, handing the node to a function of the frame's changes
nothing, the page's own node throws the page's exception, and `new
DOMException(...)` in either realm is that realm's.

## 4. What that leaves listed, and why

`test-checks-parent-global` stays in `expected-failures.txt` with its reason
rewritten in `tests/wpt/README.md`: it is the test's, not the engine's, and no
implementation passes both it and the 240. The other two engine rows wait for
changes that own their files — `strict-write` for `JsExotic::set` to report a
refused write so that `JsEval.cpp` can throw in strict mode, and
`nodelist-iterator` for `NodeList.prototype[Symbol.iterator]` to be
`Array.prototype[Symbol.iterator]`.

## 5. What it moved

| Scoreboard | Before | After |
| :--- | ---: | ---: |
| Unit suites | 201 | **202** |
| web-platform-tests | 7,998/8,009 | 7,998/8,009 (11 listed, 0 unlisted) |
| `make wpt` floor | 7,998 | 7,998 |

The build repair this branch opened with (`main` at `57a94be` did not compile:
Phase 49's `<selectedcontent>` clone called `copy_attribute_namespaces()`, which
Phase 51 deleted) is finding 58 on `main`, merged as Phase 52. Nothing in this
phase depends on it.
