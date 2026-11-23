# Finding 203 — the global object owned what it could not describe

An own property and a describable property are the same thing in
ECMA-262, and this engine had them as two answers. The global object reaches
the global declarations through `lookup_fallback` — the hook that lets
`window.Object` find what `Object` finds — and `has_own_property` asks the
hook while `descriptor_for` did not. So for 39 names:

```
> Object.prototype.hasOwnProperty.call(this, "Error")
true
> Object.getOwnPropertyDescriptor(this, "Error")
undefined
```

## Why it mattered more than one property

`propertyHelper.js` is the harness helper 400-odd test262 cases use to check a
built-in's attributes, and its first two steps are those exact two questions:

```js
assert(__hasOwnProperty(obj, name), label + " should be an own property");
var originalDesc = __getOwnPropertyDescriptor(obj, name);
...
if (__hasOwnProperty(desc, 'enumerable') && desc.enumerable !== undefined) {
  if (desc.enumerable !== originalDesc.enumerable || ...
```

The first passes and the second gives `undefined`, so the third throws
`TypeError: Cannot read properties of undefined (reading 'enumerable')` — a
message that names neither the property nor the defect. Thirty-five test262
cases died that way, each of which looks like its own bug and is not: they are
one missing branch, in a function that already had the answer available.

The lesson is the one the row in `AGENTS.md` now records: when a question has
two answers in this engine — the property map and the fallback hook — a third
place that asks must be checked for both, or it will describe half the world.

## The near miss

The fix's first version was correct and changed nothing. It sat at the end of
`descriptor_for` with the other fall-throughs, after the exotic block, and the
`Window` exotic answers `has()` **true** for every global name and then has
nothing to compute for it:

```cpp
if (cell && cell->exotic && cell->exotic->has(cell, key)) {
    if (cell->exotic->computed_property(cell, key, enumerable, writable)) { ... }
    return JsValue::make_undefined();   // <- reached for "Object"
}
```

A debug print at the top of the function and one at the tail was what showed
it: the tail print was never reached. Worth remembering as a shape — a host
object that claims names it does not describe will swallow anything appended
after it.

## What was not taken

The same survey found two more families in the same directories, and both were
left:

* `__proto__` has no descriptor because it is not in a property table at all —
  `get_property` synthesises it. 12 test262 cases. Giving it a real accessor
  changes how assignment to `__proto__` resolves, which is a wider change
  than this phase.
* The 190 remaining `defineProperty`/`defineProperties` cases *look* like one
  hole — an array index with no property entry, so the attributes set on it
  are dropped — and that diagnosis is **unverified**: the source contradicts
  it (`PropertyMap` carries per-key flags and index accessors are stored with
  one), so it was inferred from the failure messages and should not be
  repeated as fact. That fix wants
  `JsValue.cpp`, which another open pull request still names.

Both are written up in the phase document so the next agent starts from the
diagnosis rather than from the 190 failures.
