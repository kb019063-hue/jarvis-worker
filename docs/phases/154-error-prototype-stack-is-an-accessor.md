# Phase 154 — `Error.prototype.stack` was a property, not an accessor

Task: the `AGENTS.md` row **"`stack` is an own data property on an error"**,
found by counting the test262 failures by directory. Finding 159.

## 1. The cluster

`built-ins/Error/prototype/stack` is 35 tests and **35 of them failed**. That is
the shape worth picking: one property, one rule, and a scoreboard that moves
only if the rule is right. Every statement below was false:

| probe | this engine, before | test262 |
| :--- | :--- | :--- |
| `Object.getOwnPropertyDescriptor(Error.prototype, "stack")` | `undefined` | an accessor |
| `Object.getOwnPropertyDescriptor(Error.prototype, "stack").get.name` | — | `"get stack"` |
| `new Error("x").hasOwnProperty("stack")` | `true` | `false` |
| `Object.getOwnPropertyNames(new Error("x"))` | `["message","stack"]` | `["message"]` |
| `Object.getOwnPropertyDescriptor(Error.prototype,"stack").set.call({}, "s")` | — | creates an own `stack` |
| `get.call(1)` | — | `TypeError` |

## 2. There is no standard, so what is the oracle?

`stack` is not in ECMA-262. It is a de-facto property that V8 and
JavaScriptCore agree on, and test262 has written the agreement down. **Node is
not a usable oracle here**, which is unusual and worth recording: Node 22
answers `undefined` for `Object.getOwnPropertyDescriptor(Error.prototype,
"stack")`, because V8 installs `stack` per instance at construction and has
nothing on the prototype at all. V8's own arrangement is therefore not what
the suite asserts; the suite asserts the V8-*compatible* arrangement the
proposal settled on, which is an accessor pair on `Error.prototype` over an
internal slot. So the 35 files in the directory were the specification, and
they are precise: `setter-non-string-value`, `setter-this-not-object`,
`getter-data-property-shadows`, `setter-proxy-trap-rejects` and the rest name
every step.

Reading them together gives one algorithm:

- **get(receiver)** — if the receiver is not an object, `TypeError`. If it has
  the `[[ErrorData]]` internal slot, the stack text; otherwise `undefined`.
  The answer comes from the *slot on the receiver*, never from the prototype
  chain: `{__proto__: new Error()}.stack` is `undefined`, and a native error
  built through a foreign `new.target` (so that `Error.prototype` is not on
  its chain) still answers through `get.call`.
- **set(receiver, v)** — if the receiver is not an object, `TypeError`. If `v`
  is not a string, `TypeError` (this is the change that breaks
  `err.stack = null`, V8's old release-memory idiom). If the receiver *is*
  `Error.prototype`, `TypeError`: replacing the property there would take the
  accessor away from every error. Otherwise `? receiver.[[Set]]("stack", v,
  receiver)` — with a throwing Set, so a non-writable or frozen own `stack`
  throws instead of being quietly ignored, and an own accessor's setter runs.

## 3. What changed

**The slot already existed.** `JsObject::klass == ObjectClass::Error` is set
by `js_make_error`, by DOMException, and copied by the two places that clone an
error (`take_over` in `JsEval.cpp`, the structured-clone path in
`BuiltinsObject.cpp`), so `[[ErrorData]]` is the `klass` field and no storage
was added. A proxy has no `klass` of its own here either, so
`get.call(new Proxy(new Error(), {}))` answers `undefined` for the same reason
the test wants.

**`js_make_error` stops installing `stack`.** One `define_property` call
deleted. `DomBindings.cpp`'s `fill_dom_exception` likewise, and
`Error.captureStackTrace` now assigns through the ordinary write path instead
of defining a hidden data property — an error target already has the slot, and
a non-error target gets the own property the setter would have given it.

**The text is built lazily, in `js_error_stack_text`.** It reads `name` and
`message` off the receiver when something asks for `stack`, so
`new Error("x").stack` is `"Error: x\n    at <anonymous>"` and an error whose
`name` was replaced reports the replacement, as a lazy engine does. Same
format as before, byte for byte.

**`install_error_stack_accessor`** builds the pair with a new helper,
`js_make_method`, which is `js_make_native` with `not_constructor` set: the
suite asserts `isConstructor(get) === false`, and a native function is a
constructor unless told otherwise. It is called from
`JsIntrinsics::install()` after `install_error_builtins`, because it needs
`realm.error_protos[Error]`.

The setter's tail is written out rather than delegated to `set_property`,
because the two disagree about failure: `set_property` answers `false` where
the spec's Set with Throw=true throws, and it *re-invokes the inherited
setter* when the receiver starts with no own `stack` — which is this setter,
and looping is what produced `RangeError: Maximum call stack size exceeded` on
`set.call(TypeError.prototype, "s")` the first time. It also has one general
bug that is not this phase's to fix: an object with no property table of its
own (`property_cell()` null, as for a plain `{}` that has never been written to)
answers `false` from the tail of `set_property` instead of creating the
property. Recorded in `AGENTS.md`.

## 4. What the scoreboard said

Two binaries, test262 at `2e58f87d`:

```
before     14383 pass (91.7%)  [fail 1303]
now        14411 pass (91.9%)  [fail 1275]
28 pass in now and not in before; 0 pass in before and not in now
```

The directory itself went from 0/35 to 28/35. The seven that remain need
features the engine does not have rather than anything about `stack`:
`$262.createRealm` (2 tests) and `Proxy` (5 tests).

`built-ins/Error` went from 41/93 to 69/93 — the other 25 are `cause`, the
`options` argument and the error constructors' own descriptors, all of them
pre-existing gaps the `stack` change neither helped nor hurt.

Everything else unchanged: `make test` **310/310** including the suite added
here (309 before `main` was merged into the branch), `make js-conformance`
**1144/1158**, `make dom-conformance` **678/678**.

## 5. What is still open

- **`$262` does not exist**, so every test that creates a second realm fails
  whatever it is testing. Two of the `stack` cases are only that.
- **`Proxy` does not exist** (96 test262 failures, five of them here).
- **`set_property` does not create a property on an object that has no
  property table yet.** It answers `false` from the tail: `property_cell()`
  is null for a plain object that has never been written to, and
  `loose_properties()` is null for anything that is not a function, so the
  write is dropped. It became visible here because test262's
  `setter-creates-own-property` sets `stack` on a fresh `{}`.
- **A native function's order is `name,length`, not `length,name`** — the
  same row Phase 146 left behind, still one line in `js_make_native`.
