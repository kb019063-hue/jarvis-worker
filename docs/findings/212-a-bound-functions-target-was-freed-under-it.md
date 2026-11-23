## 212. A bound function reached only as a prototype lost its target

`JsValue` keeps what a function's entry point needs — and a bound function's target, bound `this` and bound arguments — in the handle's `Extra`, not in an object the collector walks. `count_value` counts them, so a payload that one handle holds *inside* the heap is not a root; the mark pass is then the only thing that keeps the objects they refer to alive. It marked a payload's `cell`, `function`, `captured` and `properties`, and **not** `captured`'s values or `bound_target`. What it skipped stayed alive only if something else reached it.

So this answered `"undefined"` after a collection, where `"bound kept"` is right, and does so on `main`:

```js
var bound = (function () {
    var target = { v: 'bound kept' };
    return Object.create(function () { return this.v; }.bind(target));
})();
gc(); churn(); gc(); churn();
Object.getPrototypeOf(bound)();        // "undefined"
```

`prototypes.dat` recorded the answer a year ago — "collect: a function that is reachable only as a prototype stays alive, with what it closed over", expected `… bound kept` — and the file has been at 70/76 since. A plain function in the same place was always right, because a plain function has no payload: its cell is the same object the handle names.

`mark_value` now marks a payload's captures and its bound target, in the same place it already marked `payload_cell()`. `make js-conformance`: **1,146 → 1,147 of 1,158**, `prototypes.dat` 70/76 → 71/76, nothing lost. `tests/test_suite.cpp` gains `a_bound_function_as_a_prototype_keeps_its_target`, which fails on the tree without the two lines (`Assertion '… == "kept"' failed`) and passes with them.

**What is still open in that area.** The other five failures of `prototypes.dat` are untouched and are their own rows: an async or generator function's prototype is `Function.prototype` instead of a per-kind object, `Array.from`/`Promise.resolve` do not make the subclass they are called on, a native function lists `name` before `length` where the standard's order is `length` first, and `class X extends f.bind(null) {}` is accepted where the language throws. `arguments.callee` answers `undefined` in sloppy code. Nothing here says the same mistake is not made by another payload-owned reference: the rule is that anything the counting pass counts must be marked, and the two are written in two functions with nothing tying them together — `gc_reclaims_a_bound_functions_target` and its new neighbour only cover the two known shapes.
