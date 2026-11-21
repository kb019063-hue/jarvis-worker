# Finding 169 — a parameter that reads like noise

## What was seen

`Object.assign` passed 17 of its 38 test262 cases. The failures did not look
like one bug: some were about the return value's type, some about a `TypeError`
that was not thrown, one about the order two getters ran in, one about a symbol
key that was not copied.

## Why it was four bugs sharing a signature

The standard's signature carries three things this code had dropped, and each
is a single word in the pseudocode:

```
Object.assign ( target, ...sources )
  to = ? ToObject(target)                    <- dropped: the primitive was returned
  ...
  ? Set(to, nextKey, propValue, true)        <- dropped: the `true`
  nextKeys = ? from.[[OwnPropertyKeys]]()    <- wrong: own *string* keys only
```

The middle one is the one worth remembering. `JsValue::set_property` returns
`bool` — `false` meaning "`[[Set]]` refused" — and **that is correct**, because
sloppy-mode assignment drops the write silently and strict-mode assignment
throws. Every other caller of it already does:

```cpp
if (!target.set_property(ref.name, value) && env->is_strict()) {
    js_throw_type_error("Cannot set property '" + ref.name + "' of this object");
}
```

`Object.assign` is *always* strict in this respect: it is not a script
assignment, it is an operation with a `throw` argument, and that argument is
`true`. So the one word `true` in `Set(to, nextKey, propValue, true)` is the
whole difference between "the value was dropped" and "a `TypeError` was raised",
and the `bool` that carried it was being discarded at the call site.

The general shape: **a return value that encodes refusal is only useful to a
caller that can tell the two possible intents apart.** Here the intent was in
the signature of the standard operation, not in the code.

## What made it visible

Printing the same eight cases from the engine and from Node:

```
                               engine (before)      Node 22
Object.assign("test", {a:1})   typeof "string"      typeof "object", .a = 1
Object.assign(12, "aaa")       names []             names 0,1,2
Object.assign(frozen, {a:2})   no throw             TypeError
Object.assign(sealed{}, {a:2}) no throw             TypeError
frozen accessor + assign       setter never ran     setter ran
Object.assign({}, {[s]:1})     0 symbols            1 symbol
```

The first line is the tell that the whole operation was running on the wrong
value: `typeof` said `"string"` where the standard's first line is `ToObject`.

## The rule

When a builtin is "almost right", check its signature line by line against the
standard's step 1. `ToObject`, a trailing `true`, and `[[OwnPropertyKeys]]` are
three words, and they were three separate clusters of failing cases that all
happened to live in the same file.
