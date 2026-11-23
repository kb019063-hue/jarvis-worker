# Finding 205 — the row about `this` was never true

**Severity:** documentation, not code. A row told every agent that a large,
dangerous part of the engine was broken. Nothing was. Acting on it would have
changed the call path of every function in the engine to reach behaviour the
engine already had.

**Found by:** taking the row at its word and probing it before writing any code.
The row's own check — `(function () { return typeof this; }).call(42)` under
`"use strict"` and without — was the first thing run.

## What the row said

> **A method's `this` is boxed before the call, so `typeof this` is never a
> primitive.** `(function () { return typeof this; }).call(42)` answers
> `"object"`, and `Boolean.prototype.toString = function () { return typeof
> this; }` makes `(true).toString()` `"object"` where every browser and Node
> answer `"boolean"`. §10.2.1.2 OrdinaryCallBindThis boxes only for **sloppy**
> functions and leaves a strict function's `this` as it was passed; this engine
> boxes whenever the receiver is a primitive, in strict code too.

Three claims. All three are false.

## What the engine does

```
(function () { return typeof this; }).call(42)                      "object"
(function () { "use strict"; return typeof this; }).call(42)        "number"
(function () { "use strict"; return typeof this; }).call(true)      "boolean"
(function () { "use strict"; return typeof this; }).call(null)      "object"
```

Node v22.22.3 answers the same four. The engine boxes for sloppy functions and
leaves a strict function's `this` alone — exactly §10.2.1.2, which the row said
was not implemented.

Fifteen call forms were then compared one for one against Node, because a
call-path bug can hide behind the one form a probe happens to use:

```
plain, .call, .apply, Reflect.apply, bind, object method, class method,
object shorthand method, accessor getter, strict and sloppy of each,
on a primitive and on null/undefined                  15/15 identical
```

## Why the row's own example did not show it

`Boolean.prototype.toString = function () { return typeof this; }` makes
`(true).toString()` answer `"object"` — **in Node too**. The replacement
function is *sloppy*, so boxing is the correct behaviour for it. The row
compared that against "every browser and Node answer `"boolean"`" without
noticing the strictness of the function it had just written. Run the same line
inside strict code and both engines answer `"boolean"`:

```
sloppy replacement function: Node "object"   Litebird "object"
strict replacement function: Node "boolean"  Litebird "boolean"
```

## What the two test262 cases were really showing

`Array/prototype/toLocaleString/primitive_this_value.js` has
`flags: [onlyStrict]`, so the replacement `Boolean.prototype.toString` it
installs is strict, and it expects `"boolean"`. Litebird answered
`"true,false"` — the numbers themselves. That is not a `this`-binding failure:
`Array.prototype.toLocaleString` was not asking its elements for their own
`toLocaleString` at all. It had been reduced to `join_array(receiver, ",")`, so
the overridden method was never invoked:

```
Boolean.prototype.toLocaleString = function () { return "CALLED"; };
[true, false].toLocaleString()     Litebird "true,false"   Node "CALLED,CALLED"
```

`true.toLocaleString()` called the same method correctly — the fault was in
`Array.prototype.toLocaleString`, not in any call path.

That defect already has a row and a pull request of its own (Marten's `#238`,
Phase 192 / finding 196, which replaces the method with
`element.get_property("toLocaleString")` and `call_with_this(element)`). It is
not this finding's to claim, and the row here needs to stop pointing at the call
path and point at that one instead.

## The change

One table row of `AGENTS.md`, struck through, saying what was measured, and a
check column that asks a future agent to *probe first*: a change here must
exhibit a call form where a strict function's `this` is boxed, or a sloppy one
where it is not. No such form exists.

## What is not claimed

- **The whole call path is not proved correct** by fifteen forms. What is proved
  is that the specific defect the row describes — boxing a strict function's
  `this` — is absent, and that the row's stated check says so.
- **The two test262 cases still fail**, and will keep failing until `#238` lands.
  This finding does not fix them and did not measure them as fixed.
- **No `Boolean.prototype.toString` edge** beyond `this` was examined: not
  `Symbol.toPrimitive`, not `Date`'s, not the `valueOf` fallback.
