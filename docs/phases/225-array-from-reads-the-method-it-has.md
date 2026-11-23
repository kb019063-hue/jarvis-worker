## Phase 225 — `Array.from` reads the iterator method it already has

`iteration.dat` carries a case whose answer Node gives and this engine did not:

```js
var a = [1, 2], reads = 0;
Object.defineProperty(a, Symbol.iterator, {
    get: function () { reads++; return Array.prototype[Symbol.iterator]; }
});
[...a];            // reads === 1
for (var x of a) {} // reads === 2
Array.from(a);      // reads === 4   — expected 3
new Set(a);         // reads === 5   — expected 4
```

One iteration, one `Get`. `[...a]` and `for…of` each read the getter once; `Array.from` read it **twice**, and every iteration after it in the same script was one read ahead. `iteration.dat` has recorded `1,2 1 2 3 4` since Phase 94 and answered `1,2 1 2 4 5`; the file's other two failures are the symbol-as-a-string row and `arguments[Symbol.iterator]`, both still open.

The cause is an ordering problem, not a missing read. `Array.from` has two paths, and which one it is on decides how the result is constructed: the iterable path constructs with no argument and grows the result a place at a time, the array-like path knows the length first and constructs with it. So it must know *whether* the source is iterable before it builds anything — it asks `js_iterator_method()` (one Get) — and then it handed the source to `js_iterate()`, which is the general entry point and asks again (a second Get). Phase 94 wrote it that way deliberately: `js_iterate` was the only loop available that followed the protocol, closed the iterator on a throw, and still took the direct path over a plain array's elements.

`src/js/JsOps.cpp` cannot be touched this phase — pull request #261 has it — so the loop is written in `src/js/BuiltinsArray.cpp`, in the file's anonymous namespace, beside the `Array.from` it serves: `iterate_with_method(iterable, method, visit)`. It keeps the direct element read for a value whose method and `next` are still this realm's (`js_iterates_as_builtin`), and otherwise follows GetIterator + IteratorStep with the method it was given: `next` read once, a step that is not an object a `TypeError`, `return` called on a throw (quietly: the loop's error is the one script sees) and checked on an early stop. `Array.from` calls it with the method it already has.

The shape is not new. `IterationCursor` in `src/js/JsEval.cpp` does the same thing for a destructuring pattern, and for the same reason: it has to decide the path before it can start. A second copy of fifteen lines is a worse outcome than a shared helper in `JsOps.hpp`, and that is the follow-up named below rather than a change to another agent's file now.

`make js-conformance`: **1,148 → 1,149 of 1,158**; `iteration.dat` **81/84 → 82/84**, nothing lost. `make -j3 test`: **363/363** suites, the new one being `array_from_reads_the_iterator_method_it_already_has`, registered under `array_from_asks_the_source_for_its_iterator`. It checks a spread, a `for…of`, `Array.from` and `new Set` reading the getter once each (`1 2 3 4`), that the iterator *the getter returned* is the one iterated (a getter that answers a different iterator every read answers once, `1`), that `next` is read once, and that the mapper still closes the iterator at the value it threw on (`closed at 3`). With `src/js/BuiltinsArray.cpp` stashed it fails on `failures.length`; with the fix it passes.

**What is still open.** `iterate_with_method` is a second copy of `iterate_with` in `src/js/JsOps.cpp`; the two belong together, exported as one function, once `JsOps.cpp` is free of #261. The direct path is entered twice (`js_iterates_as_builtin` then, inside `js_iterate`, again), which runs no script but is a lookup that could be one. The other two failures of `iteration.dat` are untouched: a symbol is a string in this engine, so `Symbol.iterator` is `"@@iterator"` and `'@@iterator' in []` is `true` where the standard says `false`; and `arguments` has no `Symbol.iterator` of its own.
