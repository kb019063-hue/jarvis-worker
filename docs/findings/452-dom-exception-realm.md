# Finding 452 — `make_dom_exception` never asked the receiver

**Phase:** [`docs/phases/452-dom-exception-realm.md`](../phases/452-dom-exception-realm.md) — fixed.

## 1. What was wrong

The Open tasks row says `make_dom_exception` \"asks the node's realm first,
then the method's\" (`src/js/DomBindings.cpp`, `include/litebird/js/DomBindings.hpp`).
It did not. It made the exception in `realm` — the `Realm&` the binding
captures at `make_function` time — which for a node method is usually the
node's document's realm (the prototype and the node belong to the same realm)
so the documented 240-subtest trade held and the existing
`dom_exceptions_belong…` suite passed.

Borrow the method and the two realms part:

```js
const foreignCreate = frameDoc.createElement;
foreignCreate.call(localDoc.createElement('div'), '1foo')
```

The native enters with `realm == foreignRealm` (its install realm) but
`current_this` is a node whose `realm_if_any` is `localRealm`. The exception
was `foreignRealm`'s. Web IDL §3.1 / §3.14.3 makes it the receiver's (the
associated realm of the platform object), and the Phase 62 write-up describes
exactly that `if (current_this.dom_node) owner = realm_if_any(*node)` fallback
— but the landed tree never contained it.

## 2. What was found on the way

Nothing borrowed the method in the corpus, so WPT held. A focused probe
fails on the unpatched tree at `452-dom-exception-realm.cpp:47`
(foreign-realm constructor), which is the signal this row was filed for.

## 3. What changed

One function, four lines: `make_dom_exception` reads `JsValue::current_this()`,
asks its `dom_node` for `realm_if_any`, falls back to `realm`. Non-nodes keep
the fallback — `Storage`/`Location` have no `dom_node` by design.

## 4. What is still open

* `innerhtml-05.xhtml` — intentional divergence, 1 listed.
* Borrowing a non-node DOMException-throwing method cross-realm remains the
  caller's-method fallback; fixing it needs a non-node realm slot, not this
  function's `current_this` peek.
