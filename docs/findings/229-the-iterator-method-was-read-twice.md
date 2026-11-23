## Finding 229 — the iterator method was read twice, once per question

`Array.from` asks a source for `@@iterator` twice: once to find out whether to build the result with a length (the array-like path) or without one (the iterable path), and once inside the loop that then walks it. Each read is a `Get`, so a getter for `Symbol.iterator` ran twice for one `Array.from` where a spread and a `for…of` run it once, and the standard (`GetMethod`, ECMA-262 7.3.11) has one read per iteration.

It is invisible for the ordinary array, whose method is a plain data property on `Array.prototype` that the direct element path never even looks at. It is visible the moment the property is an accessor, which is what `iteration.dat` does:

```
a getter for the iterator on an array is read once for each iteration
  expected: 1,2 1 2 3 4
  actual:   1,2 1 2 4 5
```

The overcount is `Array.from`'s one extra read, and because the case takes the count again after each of four iterations, the error persists to the end of the script: `new Set(a)` reads once and reports 5 where it should report 4.

**Fixed** by iterating with the method already in hand (`iterate_with_method` in `src/js/BuiltinsArray.cpp`). The loop is the standard's — GetIterator from the method, `next` read once, a non-object step a `TypeError`, `return` called on a throw and checked when the loop stops early — and the direct element read is kept for a value whose method and `next` are still the realm's.

**Still open:**

- the loop is a copy of `iterate_with` in `src/js/JsOps.cpp`. `JsOps.hpp` should export `js_iterate_with(iterable, method, visit)` and both callers use it. #261 holds that file; this phase could not.
- `js_iterates_as_builtin()` is consulted twice on the direct path (once in `iterate_with_method`, once inside `js_iterate`). It runs no script; it is a redundant lookup.
- the same double question is worth looking for elsewhere: any caller that has to know *whether* a value is iterable before it starts is a candidate. `JsEval.cpp`'s `IterationCursor` gets it right by asking once; `Set`'s constructor and the spread of a call were not examined this phase.
