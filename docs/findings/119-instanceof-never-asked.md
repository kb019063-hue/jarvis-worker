# Finding 119 — `instanceof` never asked the object it was given

Phase 114. The architecture and the measurements are in
[`docs/phases/114-instanceof-asks-symbol-hasinstance.md`](../phases/114-instanceof-asks-symbol-hasinstance.md).

## 1. What was wrong

`js_instance_of` was the whole of `instanceof`: require the right-hand side to
be callable, read its `prototype`, walk the left-hand side's chain. That is
OrdinaryHasInstance with two steps removed, and the missing ones are the ones
the language put there for objects that are not plain constructors:

- `GetMethod(rhs, @@hasInstance)`, so that a class or an object can say what is
  an instance of it, and
- the `[[BoundTargetFunction]]` step, so that `F.bind(null)` — which has no
  `prototype` of its own — is asked as `F`.

So `Symbol.hasInstance` existed in this engine as a well-known symbol and in no
way else: `typeof Function.prototype[Symbol.hasInstance]` was `undefined`, a
class that defined one was ignored, an object that defined one threw "not
callable", and a bound function answered `false`. In `prototypes.dat` the case
that asks for it did not merely fail — it aborted the file with an uncaught
`TypeError`, taking every later case with it.

## 2. What changed

- The operator is §13.10.2: not-an-object is a `TypeError`, a bound function
  delegates the whole operator to its target, otherwise the method is read and
  called; only `undefined`/`null` fall back to the ordinary walk.
- `Function.prototype[Symbol.hasInstance]` is installed — non-writable,
  non-configurable, `[Symbol.hasInstance]`, length 1 — doing
  OrdinaryHasInstance, which is now a function (`js_ordinary_has_instance`)
  shared with the operator's fallback.
- The operator recognizes that exact function by identity and takes the walk
  directly, so the ordinary case is as fast as before; anything else — a class's
  static method, a plain object's own, a getter — is read and called.
- `JsValue::Extra` gained `bound_target`, set by `Function.prototype.bind`, so a
  bound function can be told from the other functions that have no `prototype`.
- A script function with no object `prototype` is now the `TypeError` the
  language has, which is what an arrow function, a method and `Math.max` are. A
  *native* function with none still answers `false`: the six host constructors
  that have no `prototype` are the next row, and this phase does not make them
  throw before they are fixed.

## 3. Verification

- `make test` → **272 / 272**, the new suite being
  `instanceof_asks_symbol_has_instance`, whose 19 in-page checks were each
  printed by Node.js first.
- `make js-conformance` → **1142 / 1158 (98.6%)**, up from 1141;
  `prototypes.dat` **69 / 76**, up from 68, and no longer aborted.
- `make dom-conformance` 678/678, `make conformance` 1973/1973,
  `make url-conformance` 1188/1188, `make wpt` 9567/9824 with the same 257
  listed failures — all unchanged.
- The first form of the change used the fast path for every function, and
  answered `3 instanceof B` `false` for `class B extends A {}` where `A`
  defines `static [Symbol.hasInstance]`; the fast path now asks whether the
  function's own `[[Prototype]]` is `Function.prototype`, and the suite asserts
  the subclass case.

## 4. What is still open

- `Symbol.species` and the built-in statics that should construct the subclass
  (`Array.from`, `new List().map`, `Promise.resolve`).
- Six host constructors with no `prototype` — `Response`, `Headers`, `Worker`,
  `WebSocket`, `IntersectionObserver`, `ResizeObserver` — so
  `r instanceof Response` is `false` rather than `true`, and `1 instanceof
  Math.max` is `false` rather than a `TypeError`.
- The message for a non-callable `@@hasInstance` is this engine's, not V8's
  word for word; both are `TypeError`.
- `Symbol.hasInstance` is still a string key (`"@@hasInstance"`) in this engine,
  as every well-known symbol is.
