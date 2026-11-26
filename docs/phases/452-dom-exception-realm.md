# Phase 452 — `make_dom_exception` asks the receiver first

**Finding:** [`docs/findings/452-dom-exception-realm.md`](../findings/452-dom-exception-realm.md)

## 1. What was wrong

Phase 62 settled that a `DOMException` belongs to the **receiver's** realm — the
node the method was called on — not the caller's. It measured the trade:
making the exception in the caller's realm fixes `domparsing/innerhtml-05.xhtml`
(0→1) and breaks `dom/nodes/Document-createElement.html` (743→503, 240
unlisted). The row was written and the test pinned
(`dom_exceptions_belong_to_the_realm_of_the_object_they_are_about`, 6
checks).

The row's text says `make_dom_exception` \"asks the node's realm first, then
the method's\". The code did not. It returned `realm.make(\"DOMException\")`
where `realm` is the `Realm&` the binding captured when its interface was
installed — the method's realm — and threw it through `throw_dom`. For a node
method borrowed with `.call()` / `.apply()` across realms, or a frame helper
that does `frameNode.innerHTML = value` via a function of the frame's, the
native function's realm and the node's realm differ and the exception was the
method's.

In practice the two realms agree: `wrap` and every node prototype's method
live in the node's document's realm, so `d.createElement('1foo')` from the
page still enters the native with `realm == frameRealm`. The case the text
promised never fired, and the candidate patch that made it the caller's was
reverted — but the promised fallback was never landed either.

## 2. What changed

`src/js/DomBindings.cpp:make_dom_exception` now implements the row:

```cpp
Realm* owner = nullptr;
if (const JsValue& self = JsValue::current_this(); self.is_object_like())
    if (JsObject* cell = self.object_val.get(); cell && cell->dom_node)
        owner = realm_if_any(*cell->dom_node);
if (!owner || !owner->alive) owner = &realm;
JsValue error = owner->make("DOMException");
```

The method's `Realm&` is the fallback for a receiver that is not a node
(`Storage`, `Location`, `XMLHttpRequest`) or has no realm / is dead. The
receiver is `JsValue::current_this()` as set by `JsOps`' native call frame;
the node's realm is `realm_if_any`, the document's browsing realm if live
else the owner's script realm.

## 3. How it was verified

```
$ cmake --build build -j4
$ ./build/test_suite --filter dom_exceptions_belong_to_the_realm_of_the_object_they_are_about
[pass] dom_exceptions_belong_to_the_realm_of_the_object_they_are_about (6/6)
$ ./build/test_suite
All 487 engine test suites passed successfully!
```

Plus the new suite `452-dom-exception-realm.cpp` (7 checks) that exercises
the borrowed-method case `foreignMethod.call(localNode)` — without the change
the exception is the foreign realm's, with it the receiver's.

`dom/nodes/Document-createElement.html` still 743/743, `innerhtml-05.xhtml`
still 0/1 listed as `test-checks-parent-global` — the documented trade, not a
number to chase.

## 4. What is still open

* `domparsing/innerhtml-05.xhtml` stays listed — no implementation passes
  both it and the 240, as Phase 62 measured.
* A borrowed non-node method (`localStorage`, `location`) throwing a
  `DOMException` via `.call()` cross-realm still reports the method's realm;
  the receiver is not a node and the fallback is intentional.
