# Finding 366 — three kinds of function, one prototype between them

**Severity:** no way for a script to tell an async function or a generator
function from an ordinary one except by calling it, and the standard's own tag
was unreachable. `Object.getPrototypeOf(async function () {})` was
`Function.prototype`, `Object.prototype.toString.call(async function () {})`
was `[object Function]`, and `Object.getOwnPropertyNames(Math.max)` was
`["name","length"]` where every browser says `["length","name"]`.

**Found by:** the still-open half of the Phase 278 row in `AGENTS.md`, which
names both faults in one sentence and points at `prototypes.dat`'s async and
generator case.

## What it was

Three faults that only show together.

```cpp
// JsValue.cpp, property_cell -- every script function, whatever its shape
m_extra->cell->prototype = JsIntrinsics::get().function_proto;
```

```cpp
// BuiltinsObject.cpp, object_tag -- the tag lookup below was unreachable
case JsType::Function:
case JsType::NativeFunction: return "Function";

if (value.is_object_like()) {
    JsValue tag = value.get_property("@@toStringTag");   // never ran for a function
```

```cpp
// JsOps.cpp, js_make_native -- the standard defines these the other way round
v.define_property("name", ...);
v.define_property("length", ...);
```

The first is a missing object: §20.2.3.2 and §27.3.3.2 put a prototype between
each kind and `%Function.prototype%`, and there was nowhere for one to be. The
second made the missing object invisible even if it had existed. The third is
a plain order-of-definition bug, which test262 checks in three files.

## Why it matters more than it looks

`typeof` answers `"function"` for all three kinds. Before this phase the only
difference a script could observe was behaviour — call it and see whether you
got a promise, an iterator, or a value — and the tag that is *designed* to
answer the question returned the same string for all of them. The three
`property-order.js` cases are the standard saying the order of two properties
is part of the language, not an implementation detail.

## What it is now

```js
Object.getPrototypeOf(async function () {}) === Function.prototype   // false
Object.getPrototypeOf(Object.getPrototypeOf(function* () {})) === Function.prototype  // true
Object.prototype.toString.call(async function () {})   // [object AsyncFunction]
Object.prototype.toString.call(function* () {})        // [object GeneratorFunction]
Object.prototype.toString.call(function () {})         // [object Function]  (unchanged)
Object.getOwnPropertyNames(Math.max)                   // ["length","name"]
```

Each kind prototype is an *object*, not a function — `typeof
Object.getPrototypeOf(async function () {})` is `"object"`, which is what Node
answers. Its `constructor` is reachable and named, but not a global binding,
exactly as in Node.

## The corrections

Three assertions of mine were wrong and the engine was right, which is this
project's most repeated lesson:

1. I first built the kind prototypes as *functions*. Node answers `typeof
   "object"` for the object and `"function"` for `p.call` — a callable exotic
   object, not a language function.
2. I expected `AsyncFunction` to be a global. `typeof AsyncFunction` is
   `"undefined"` in Node; only the prototype's `constructor` reaches it.
3. I asserted a script function's own names were exactly `length,name`. Node
   lists `length,name,arguments,caller,prototype`; only the first two are the
   order §10.2.4/§10.2.5 promise. The assertion was narrowed to the prefix.

## Evidence

`make test` 426 → **427/427**. test262 **+4, 0 broken**: the three
`property-order.js` files and `symbol-tag-override-instances.js`.
`js-conformance` 1,150 → **1,152/1,158**. `language` unchanged, which matters:
the arguments-object phase broke 75 there by moving a prototype without giving
the object what the standard gave it.
