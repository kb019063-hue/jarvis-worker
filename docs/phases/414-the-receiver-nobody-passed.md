# Phase 414 — the receiver nobody passed

Finding 414. Task: no row names this. It was found by running the same
expressions in Node and in this engine and diffing the output — the third
form of the same habit as Phases 393 and 405, and the one that found the most
per call: `Reflect.get` and `Reflect.set` both take a receiver, and both
ignored it.

## 1. What was wrong

`Reflect.get` and `Reflect.set` are not "read the property" and "write the
property". §28.1.6 and §28.1.13 are §10.1.8's `[[Get]]` and §10.1.9's
`[[Set]]`, whose whole shape is that the **receiver** — which need be neither
the object the property was found on nor anywhere on its chain — is what a
getter or setter sees as `this`, and what a new property is defined on.

```
$ node -e 'console.log(Reflect.get({get x(){return this.y;}}, "x", {y: 9}))'
9
```

This engine answered `undefined`: the method called
`require_object(arg).get_property(key)` and never looked at the third
argument. `Reflect.set` was worse — it called `set_property` on the target and
then `return JsValue::make_boolean(true)`, so it ignored the fourth argument
*and* reported success unconditionally:

```
$ node -e 'console.log(Reflect.set(Object.freeze({}), "x", 1))'
false
```

§28.1.13 step 2 returns the boolean the ordinary `Set` produced. That is the
only thing separating `Reflect.set` from an assignment in strict code, which
throws instead — and it is the reason a `Proxy` handler's `set` trap can
answer `false` and have it mean something.

A third, unrelated defect came out of the same differential run, on the way:
`structuredClone(Symbol())` threw a `TypeError`. HTML 2.7.4
StructuredSerializeInternal has it as its own step, before any object is
looked at:

> **step 6.** If value is a Symbol, then throw a `"DataCloneError"` DOMException.

Node answers exactly that — `e.name` is `DataCloneError`, `e instanceof
TypeError` is `false`, `e.code` is 25 — for a nested symbol and for a `Map`
key as well. This engine's comment on the branch said a Symbol throws a
`TypeError` "because it has no representation at all", i.e. that the standard
said so. It does not, and the phase-252 suite had been asserting the
deviation for long enough that the comment and the test agreed with each
other and both were wrong.

## 2. What changed

**`Reflect.get`** now goes through `js_get_from_chain(cell, key, receiver)` —
the lookup `super.name` uses, and the one place in the engine that already
knows how to run a getter with a receiver that is not the object it was found
on. With no third argument the receiver is the target, which is §28.1.6
step 1's default.

**`Reflect.set`** keeps `set_property` for the ordinary case — no fourth
argument, or the receiver *is* the target — because `set_property` already
runs a setter with the object as `this` and already refuses a frozen object.
Any other receiver travels in a thread-local `SetterReceiverScope` that
`set_property`'s accessor calls consult through `setter_receiver(*)`, which
answers the object itself when no scope is open. The alternative was a second
`set_property` taking a receiver, restating 300 lines of array, accessor and
prototype rules; this way every setter invocation that is not `Reflect.set`
is bit-for-bit unchanged, which is what the 400 existing suites say.

**`structuredClone`** throws `throw_data_clone(value.to_string())`, naming the
symbol the way Node does (`Symbol(x) could not be cloned.`). `js_to_string`
cannot be used here: `ToString` of a symbol throws, correctly, so it has to be
`Symbol.prototype.toString`'s answer.

## 3. How it was verified

Two suites, every expected value Node v22.22.3's by evaluating the same
expression there.

```
$ ./build/test_suite
All 469 engine test suites passed successfully!
```

`414-reflect-receiver.cpp`, 30 assertions, **red on three engines, each failing
a different assertion** — which is what it takes to cover two arguments that
were both being dropped:

| Wrong engine | Fails at | What it says |
|---|---|---|
| `get` and `set` ignore the receiver (the engine as it was) | `414-reflect-receiver.cpp:30` | `Reflect.get` with a receiver is `undefined`, not 9 |
| `get` fixed, `set`'s receiver still ignored | `:47` | the setter wrote to the target, not the receiver |
| receiver honoured, result still `true` | `:57` | a frozen target reports success |

`414-clone-symbol.cpp`, 21 assertions, red at `:30` on the old `TypeError`
branch. Its own red proof needed the phase-252 suite moved aside, because that
file sorts first and fails on the same defect — which is itself the evidence
that both suites now guard it.

## 4. What is still open

- **`Reflect.set` to a receiver that is not the target, for a property the
  target does not have.** The thread-local makes the *setter* right; defining
  the new property on the receiver instead of the target (OrdinarySetWithOwnDescriptor
  step 5's `CreateDataProperty(receiver, …)`) is not done, and no assertion
  covers it. It needs a receiver-aware create path through `set_property`.
- **`Reflect.set`'s `[[Set]]` on a Proxy** is exactly where the boolean
  matters, and `Proxy` does not exist in this engine (it was the one other
  difference the same differential run found). When it does, this is the
  argument that will be tested.
- **`Symbol.dispose` and `Symbol.asyncDispose`** are absent, and deliberately
  not added: the spec's values are well-known symbols with
  `description == "Symbol.dispose"`, but Node v22.22.3 exposes *registered*
  symbols (`Symbol.keyFor` answers `"nodejs.dispose"`) so it is not an oracle
  for them, and nothing in this engine parses `using`, so the symbol alone
  would be consulted by nothing. The pair is worth doing with `using`, not
  before it.
- **`Intl` is absent**, which makes `Reflect.get`'s interaction with
  `%Intl%` nominal object untested; and `Math.sumPrecise` is present here and
  absent in Node, so that comparison could not be made in Node's favour.
