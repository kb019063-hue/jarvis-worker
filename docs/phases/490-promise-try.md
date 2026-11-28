# Phase 490: Promise.try invokes and settles through its constructor

`Promise.try` was listed as absent in the Promise subclass row. The local Node v22.22.3 oracle also returned `undefined` for `typeof Promise.try`, so Node could not serve as a differential. The implementation follows ECMA-262 2025 §27.2.4.9.

The method first requires an object receiver, then calls the callback synchronously with `undefined` as `this` and forwards every trailing argument. It deliberately creates no promise before that call. On a normal completion, it performs `PromiseResolve(this, result)`, preserving an existing promise of the same constructor and adopting thenables. On an abrupt completion—including the TypeError from a non-callable callback—it creates a `NewPromiseCapability(this)`, rejects through that capability, and returns its promise. Therefore callback execution precedes subclass construction in both branches; a non-constructor object receiver can still cause a synchronous error after the callback ran.

The implementation reuses `Promise::resolve_from` for PromiseResolve and `Promise::new_capability` for the abrupt path. It also checks callability explicitly: this engine's low-level `JsValue::call_with_this` returns `undefined` for a non-function rather than raising the abstract operation's required TypeError.

`tests/suites/490-promise-try.cpp` covers synchronous invocation and rest arguments, promise job settlement, thrown `undefined`, non-callable callbacks, thenable adoption, same-promise identity, subclass construction order and rejection, primitive-receiver ordering, and strict callback `this`.

Validation: `make -j2 test` passed **525/525 engine suites**; the cross-origin suite passed **114/114 checks**. This feature is a specification-derived addition, not a Node v22 differential.
