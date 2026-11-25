# Phase 403 — a bound function has no prototype to extend

Task: the row "What a function is, in small" names this among its still-open
items — "`class X extends f.bind(null) {}` is accepted where the language
refuses it — a bound function is a constructor but has no `prototype` to
extend". Finding 403.

## 1. What was wrong

The `extends` clause read the parent's `prototype` and **quietly ignored it
when it was not an object**:

```cpp
JsValue parent_proto = parent.get_property("prototype");
if (parent_proto.is_object_like()) {
    if (JsObject* target = parent_proto.as_prototype()) proto.object_val->prototype = target;
} else if (parent_proto.type == JsType::Null) {
    proto.object_val->prototype = nullptr;
}
// else: nothing. The class is built with a prototype that inherits from
// %Object.prototype% and the `extends` had no effect at all.
```

§15.7.14 step 5 is `protoParent = ? Get(superclass, "prototype")` followed by
"if protoParent is not an Object and protoParent is not null, throw a
TypeError". The missing `else` is that throw.

A **bound** function is the case the row names because it is the one that looks
like it should work: §10.4.1.3 gives a bound function `[[Construct]]`, so it
passes the earlier `IsConstructor` check, and it has no `prototype` property at
all — `'prototype' in f.bind(null)` is `false`. A class extending one was
accepted and its instances inherited from `%Object.prototype%`, which is neither
what the parent nor the standard says.

## 2. What changed

`src/js/JsEval.cpp`, in the class evaluation: the `prototype` of the parent must
be an object or `null`, and anything else — including a property that is not
there — is a `TypeError`. The check sits after the `IsConstructor` check, which
is the order the spec has: a parent that is not a constructor is refused for
that reason and never reaches the `prototype` question.

## 3. What it was measured against

Node v22.22.3 answered twelve probes first, and **both corrections this phase
needed were to my own tests**:

1. I wrote a probe that redefined a function's `prototype` as a getter to prove
   the check is a `Get`. It is not possible: an ordinary function's `prototype`
   is non-configurable, so `Object.defineProperty` throws before `extends` runs
   — Node says `TypeError` at the `defineProperty`. The assertion was replaced
   with an accessor on a plain parent object, which does exercise the `Get`.
2. I then wrote an assertion whose expected value was the string
   `"[object Object]"` for a call that throws inside a getter. It was not
   testing anything; it was deleted rather than repaired.

The rest of the probes match Node line for line, including the two that are
easy to get wrong:

- `function k() {}; k.prototype = null; class D extends k {}` is **accepted**
  by step 5 — `null` is a legal `protoParent` — and `new D()` is a
  `ReferenceError`, because `super()` has nothing to call. Node answers the
  same pair.
- `class A extends null {}` is still accepted, and a bound function is still a
  constructor: `new (f.bind(null))()` works. The refusal is about *extending*
  it, not about what it is.

## 4. The numbers

| corpus | before | now |
|---|---|---|
| test262, all of it | 15,177 (96.8%) | **15,180 (96.8%)**, +3, 0 broken |
| `make test` | 460/460 | **461/461** |
| `make js-conformance` | 1,152/1,158 | **1,153/1,158** |
| `make wpt` | 9,570/9,824, 0 unexpected | unchanged |

`language/statements/class/subclass/superclass-bound-function.js` — the case
the row named — passes now, and `built-ins/Function/prototype/toString/bound-function.js`
keeps passing. `language` did not move, which is the number to watch here: a
`TypeError` added to class evaluation could have refused many classes that
should be accepted.

## 5. What is still open

- `built-ins/Function/prototype/toString/proxy-bound-function.js` needs `Proxy`,
  which this engine does not have.
- The rest of the row is unchanged by this phase and is not about `extends`:
  async and generator prototypes (Phase 366), and the `extends`-of-a-bound-
  function item is now closed.
