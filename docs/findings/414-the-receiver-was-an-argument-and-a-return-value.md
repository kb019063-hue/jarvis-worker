# Finding 414 — the receiver was an argument, and a return value

Phase 414. Status: **fixed.**

## 1. What was wrong

Three defects, found together by diffing Node's answers against this engine's
on 21 `Reflect` expressions.

**`Reflect.get` dropped its third argument.** §28.1.6 is
`target.[[Get]](key, receiver)`. The receiver is what a getter sees as `this`,
and it need not be the target:

```
Reflect.get({get x(){return this.y;}}, "x", {y: 9})   // 9
```

This engine answered `undefined`, because the method called
`get_property` on the target and never read `args[2]`.

**`Reflect.set` dropped its fourth argument, and its answer.** §28.1.13 is
`target.[[Set]](key, value, receiver)` and step 2 returns the boolean:

```
Reflect.set(target, "x", 5, receiver)   // the setter's `this` is `receiver`
Reflect.set(Object.freeze({}), "x", 1)  // false — not an exception, not true
```

The method wrote to the target with `set_property` and returned `true`
unconditionally.

**`structuredClone(Symbol())` threw the wrong class.** HTML 2.7.4 step 6:
"If value is a Symbol, then throw a `DataCloneError` DOMException". It is a
step of its own, before any object is looked at — so it fires for a nested
symbol and a `Map` key too. This engine threw a `TypeError`, and a comment
beside the branch said that was the standard's split.

## 2. What changed

`Reflect.get` → `js_get_from_chain(cell, key, receiver)`, the existing lookup
that runs a getter with a receiver (`super.name`'s).

`Reflect.set` → the ordinary path unchanged when there is no receiver or the
receiver is the target; otherwise a thread-local `SetterReceiverScope` that
`set_property`'s accessor calls read through `setter_receiver(on)`, which
answers `on` when no scope is open. Both paths return the boolean.

`structuredClone` → `throw_data_clone(value.to_string())`. Not `js_to_string`:
`ToString(symbol)` throws, which is correct, so the message has to come from
`Symbol.prototype.toString`.

## 3. The mistake worth remembering

The Symbol branch came with a comment explaining that a `TypeError` was the
standard's answer, *and* the phase-252 suite asserted it, with a second
comment saying the wording was "the engine's own rather than Node's".

Two records of the same wrong belief, and they made each other look
authoritative — the test passed, so the comment read as measured, and the
comment explained why the test deviated, so the deviation read as chosen.
Neither cited the specification. The standard's step 6 is one sentence and
says the opposite. **A passing test that documents its own deviation is a
red flag, not a licence**: it had been passing for 162 phases.

## 4. What remains wrong

- **A new property, set through a receiver that does not have it.**
  OrdinarySetWithOwnDescriptor step 5 defines it on the *receiver*
  (`CreateDataProperty(receiver, …)`). The setter side is now right; the
  create side is not, and nothing asserts it.
- **`Proxy` is absent**, and it is where `Reflect.set`'s boolean actually
  earns its keep — a `set` trap answering `false` is what makes
  `Reflect.set` useful over an assignment. Both were found in the same
  differential run; this one needs `Proxy` first.
- **`Symbol.dispose`/`asyncDispose`** are absent and not added: Node's are
  registered symbols with the wrong description, so Node is not an oracle,
  and with no `using` in the parser nothing would consult them.
