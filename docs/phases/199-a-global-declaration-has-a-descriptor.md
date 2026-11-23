# Phase 199 — a global declaration has a descriptor

*Finding 203.* The global object and the global environment are two things
that have to answer as one. §9.1.1.4 makes the global declarations — `Object`,
`Error`, `eval`, `JSON`, `undefined` — non-configurable data properties of the
global object, but this engine keeps them as bindings in the global
environment and lets the object reach them through its `lookup_fallback`, the
hook that exists so `window.Object` finds what `Object` finds.

Two of the questions a script can ask go through that hook and one did not:

```
Object.prototype.hasOwnProperty.call(this, "Error")    true
Object.getOwnPropertyDescriptor(this, "Error")         undefined
```

`has_own_property` consults the property map *and* the fallback. `descriptor_for`
consulted only the property map, so every global declaration was simultaneously
an own property and not one. That is the shape `propertyHelper.js` tests with —
`assert(hasOwnProperty(obj, name), name + " should be an own property")`, then
`obj[name]`'s descriptor — and it is why 35 test262 cases across
`built-ins/Object/getOwnPropertyDescriptor`, `built-ins/Error/prop-desc.js`,
`built-ins/Function/prop-desc.js`, the six `built-ins/NativeErrors/*/prop-desc.js`
and `built-ins/Object/prop-desc.js` all failed with *Cannot read properties of
undefined*: the helper read `.enumerable` off the descriptor the engine had
declined to give it.

## What was wrong, counted

A sweep of the global object's own names, comparing the two answers:

```
39 mismatched
Object Function Array String Number Boolean Symbol Error EvalError RangeError
ReferenceError SyntaxError TypeError URIError parseInt parseFloat isNaN isFinite
eval JSON Math Reflect Promise Map Set WeakMap WeakSet ArrayBuffer DataView Date
RegExp Int8Array Uint8Array globalThis undefined NaN Infinity decodeURI encodeURI
```

Every constructor in the library, the whole error family, the four global
functions, and the three non-writable globals. Not one of them had a
descriptor.

## The fix

`descriptor_for` asks the global scope when the object it was handed is the
global object. The descriptor is derived from the binding, not invented:

| binding | `writable` | `enumerable` | `configurable` |
|---|---|---|---|
| a library global (`Object`, `eval`, `Math`) | `true` | `false` | `true` |
| a script's `var` or function declaration | `true` | `true` | `false` |
| `undefined`, `NaN`, `Infinity` | `false` | `false` | `false` |
| `let`, `const`, `class` | — not a property of the global object at all |

The `is_lexical` flag is what keeps the last row right, and it is the half
that could have gone wrong: a `let` in the global scope is a binding the object
must *not* describe. The non-writable globals are the only ones §9.1.1.4 makes
non-configurable, which is why `is_readonly` decides two fields rather than
one.

## Where the block goes

The first version put it at the end of `descriptor_for`, beside the other
fall-throughs, and changed nothing at all. The `Window` exotic claims every
global name:

```
if (cell && cell->exotic && cell->exotic->has(cell, key)) {
    if (cell->exotic->computed_property(cell, key, enumerable, writable)) { ... }
    return JsValue::make_undefined();
}
```

`has` answers yes for `Object`, `computed_property` has nothing to compute, and
the function returns `undefined` before reaching the new block. Asking the
global scope has to come **before** the exotic, and the comment in the code
says so, because that is not visible from either end of the function: the
`has` that claims the name is in `DomWindowBindings.cpp` and the `return` that
discards it is a hundred lines below the block.

## What moved

| | Before | After |
|---|---|---|
| test262, the 21 default directories | 14,969 / 15,686 (95.4%) | **14,980 / 15,686 (95.5%)** |
| `make js-conformance` | 1,145 / 1,158 | **1,146 / 1,158** |
| `make dom-conformance` | 678 / 678 | 678 / 678 |
| unit suites | 344 | **345** |

11 test262 cases gained and none lost; the js-conformance case that came with
it is `eval.dat`, which reads a global's descriptor. `make test262` is not
slowed: the block is one pointer comparison for any object that is not the
global object, and the scope walk is a single `find_own`.

## What is still open in this family

`Object.getOwnPropertyDescriptor(Object.prototype, "__proto__")` is still
`undefined` — 12 cases in `built-ins/Object/prototype/__proto__/`, and 7 more
in `language/expressions/object` and destructuring that read the same accessor.
That is a *different* mechanism: `__proto__` is not in any property table at
all, `JsValue::get_property` synthesises it, so it has no descriptor to report
and no accessor for `defineProperty` to replace. Giving it one changes how
assignment to `__proto__` resolves, which is why it is not done here.

The rest of `built-ins/Object/defineProperty` and `defineProperties`
(190 cases) is one larger hole: **an array index is backed by
`array_elements` and by no property entry**, so the attributes
`defineProperty` is asked to set on an index are dropped. `verifyProperty`
then reports *"0 descriptor should not be writable"* and its relatives on
almost every one of them. The fix is to give an index a real property entry
when a descriptor is applied to it, which touches `JsValue.cpp` — a file an
open pull request still names.
