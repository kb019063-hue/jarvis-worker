# Finding 403 — `extends` read the parent's prototype and shrugged

**Severity:** a class could extend a value with no `prototype`, and the
`extends` had no effect at all — the class's instances inherited from
`%Object.prototype%`. `class X extends f.bind(null) {}` was accepted where every
browser refuses it. A missing `else` in the class evaluation.

**Found by:** the row "What a function is, in small", which names it exactly:
"a bound function is a constructor but has no `prototype` to extend".

## What it was

```cpp
JsValue parent_proto = parent.get_property("prototype");
if (parent_proto.is_object_like()) {
    ...                                   // use it
} else if (parent_proto.type == JsType::Null) {
    proto.object_val->prototype = nullptr; // extends null
}
// anything else: silently dropped
```

§15.7.14 step 5 has three outcomes, not two: an object, `null`, or **a
`TypeError`**. The third was missing.

A bound function is the shape that exposes it. §10.4.1.3 gives it
`[[Construct]]`, so it passes `IsConstructor` — it really is a constructor —
and it has no `prototype` property of its own:

```js
'prototype' in f.bind(null)                    // false
class X extends f.bind(null) {}                // was accepted;  Node: TypeError
```

## Why the ordering matters

The `prototype` check belongs *after* the `IsConstructor` check, and that is how
it is written. A parent that is not a constructor is refused for that reason
("is not a constructor or null") and never reaches the `prototype` question —
so `class X extends (() => {}) {}` keeps its existing message, and a plain
object with a `prototype` getter is refused by the constructor check rather
than by step 5. Getting this order wrong would have changed the message on
cases that were already correct.

## Both corrections were to my tests, not to the engine

1. I tried to prove the check is a `Get` by redefining a function's `prototype`
   as an accessor. It cannot be done — an ordinary function's `prototype` is
   non-configurable, and Node throws at the `defineProperty` before `extends`
   is reached. The assertion was rewritten against an accessor on a plain
   object.
2. I then wrote an assertion expecting the *string* `"[object Object]"` from a
   call that throws inside a getter. It asserted nothing meaningful; it was
   deleted rather than patched.

That is the sixth time in these phases that a failing assertion was my mistake
and not the engine's, which is why the suite starts from Node's answers.

## Evidence

test262 15,177 → **15,180** (+3, 0 broken);
`language/statements/class/subclass/superclass-bound-function.js` passes;
`make test` **461/461**; `js-conformance` 1,152 → **1,153** of 1,158; `wpt`
unchanged. Twelve probes compared line for line with Node v22.22.3, including
`k.prototype = null` (accepted by step 5, `new` is a `ReferenceError`) and
`extends null` (still accepted).

## Still open

`proxy-bound-function.js` needs `Proxy`, which this engine does not have. The
row's other items — async and generator prototypes — were Phase 366.
