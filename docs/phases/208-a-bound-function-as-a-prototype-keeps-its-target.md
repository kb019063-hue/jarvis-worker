## Phase 208 — A bound function reached only as a prototype keeps its target

The `%Function.prototype%` row of `AGENTS.md` named three leftovers, and `prototypes.dat` — the corpus recorded from Node.js — has held six recorded failures for a year. One of them was not about `Function.prototype` at all: a *collection* case, in a file that also asks which functions a subclass sees.

`prototypes.dat`'s case is this shape:

```js
var bound = (function () {
    var target = { v: 'bound kept' };
    return Object.create(function () { return this.v; }.bind(target));
})();
gc(); churn(); gc(); churn();
Object.getPrototypeOf(bound)();        // expected "bound kept"
```

On `main` that is `"undefined"`. Every escape the target has is the prototype link of a live object, so it is exactly as alive as the object is; the engine lost it.

The collector counts what each handle's `Extra` refers to (`count_value`), so a payload that one handle holds from inside the heap is owned and is **not** a root — and then the mark pass has to keep those objects alive, because nothing else will. `mark_value` marked the payload's own cell, its function's closure, its property map and its captures' container, and of the two vectors of values it has — what the entry point captured, and a bound function's target — it marked neither's contents. The target's payload was found owned by nobody, freed, and the live bound function went on reading it.

Two lines in `Heap::mark_value` mark them, in the same place `payload_cell()` was already marked. A plain function in the same position never had the fault, because a plain function's cell is the object the handle names; only a payload keeps anything the cell does not.

`make js-conformance`: **1,146 → 1,147 of 1,158**; `prototypes.dat` **70/76 → 71/76**, nothing lost. `make -j3 test`: **350/350** suites, the new one being `a_bound_function_as_a_prototype_keeps_its_target`, which fails on the tree without the fix and passes with it, and which is written through `TestPage` so that a merge with another new suite keeps both whole.

**What is still open.** The other five recorded failures of `prototypes.dat`: `Object.getPrototypeOf(async function () {})` is `Function.prototype` and should be an object of its own kind (same for generators, and `Object.prototype.toString` says `[object Function]` for both); `Array.from` and `Promise.resolve` do not make the subclass they are called on; a native function lists `name` before `length` (`js_make_native` defines `name` first) where `length` comes first; and `class X extends (F.bind(null)) {}` is accepted where §10.2.1.2's bound function has no `prototype` to extend. `arguments.callee` is `undefined` in sloppy code. And the count/mark pair is still written twice with nothing tying them together: a third payload-owned vector would have the same fault, and no test says so.
