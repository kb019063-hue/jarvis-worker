# Finding 200: a subclass promise had no state, and a rejection had no reject

## What was found

`class P extends Promise {}` made instances that were `instanceof P` and whose
`then` said *called on incompatible receiver*: the engine looked for a
promise's state on the value, and the object a subclass constructor gets back
from `super(...)` is a **cell** — `take_over` copies what a cell holds, and the
value's payload does not travel with it.

Behind that were three older faults, each of which only became reachable once
a capability's own functions were called from inside a built-in. All three are
independent of `Promise`.

## 1. `current_this()` is a reference into the call context

`JsValue::current_this()` returns `const JsValue&` — an alias for
`t_call_context.this_val`. `CallContextScope`'s constructor saves the context
by move and then assigns `t_call_context.this_val`. So a native that holds the
receiver by reference and starts a call with it has the receiver **overwritten
under it** by the new call's `this`.

`Promise.resolve` did exactly that:

```cpp
return Promise::resolve_from(JsValue::current_this(), js_arg(args, 0));
```

`resolve_from` → `new_capability` → `js_construct(constructor, …)`, and
`constructor` is that reference. From inside the constructor, `%fn` — which is
the callee — was an **object**, so `eval_super_call` refused with *'super'
keyword unexpected here*. The same site broke `reject` and `withResolvers`.

**The fix** is to copy the receiver before handing it to anything that can
call:

```cpp
const JsValue constructor = JsValue::current_this();
```

150 call sites read `current_this()`. Most copy it (`JsValue me = …`) and the
ones that hold `const JsValue&` only read it. The three in `Promise.cpp` did
not.

**What this means for the next agent:** an aliasing bug of this shape is
invisible until a native passes its receiver into something that calls. If a
`const JsValue&` from `current_this()` outlives a call, it is a bug.

## 2. `settle_through` settled a rejection through `resolve`

PromiseReactionJob steps 8-9 pick `capability.[[Reject]]` for a rejection and
`capability.[[Resolve]]` for a value. `settle_through` called
`handler.resolve` for both, and `PromiseHandler` had no `reject` field at all.
A rejection that passed through a `then` with no handler for it arrived at the
next `then` **as a value**, and a `catch` further down never ran.

This was invisible while every capability was the engine's own, because
`handler.resolve` was undefined and the code fell through to `settle(...)`,
which does honour the flag. It appears the moment a reject function exists.

**The fix:** `PromiseHandler::reject`, rooted in the trace and in the
microtask's root set, and `settle_through` picks by the flag.

## 3. A capability's derived promise shared the source's state

`capability_for(data, C)` made the new promise with `create_promise_instance(data)`
— the **same** `PromiseData` as the receiver. Settling the new promise settled
the source too, so `p.then(f).then(g)` handed `g` what `f` had been given
rather than what it returned. The new promise must have a state of its own,
with its own resolving pair, so that a handler returning a thenable is adopted
(`Promise::resolve`'s adopt path) instead of being used as a plain value.

## 4. `PromiseResolve` wrapped a promise that was already the right kind

`Promise.resolve(x)` made a new promise around `x` even when `x` was already a
%Promise% promise of the same constructor. §27.2.4.7.1 steps 2-3 return it
unchanged, and the combinators depend on that: they ask `this.resolve` for each
item and watch the result's `then`. Wrapping it moved a throwing `then` getter
out of `js_iterate`, so `IteratorClose` never ran and `Promise.any` looped
forever over a non-advancing iterator.

## Why it matters

Every one of these is a wrong answer in ordinary script with no subclass
involved: `p.then(f).then(g)` gave `g` the wrong value, `catch` never saw a
passed-through rejection, `Promise.resolve(p)` was a different promise from
`p`, and `Promise.any` hung. They were found by the engine's own suites, not by
the new ones — `make test` was red four times in a row before the last of them
cleared.

## What the fix does

- `include/litebird/js/Promise.hpp`: `PromiseCapability`, `PromiseHandler::reject`,
  the six new statics.
- `src/js/Promise.cpp`: the capability pair, `PromiseResolve`, and every method
  that makes a promise going through them.
- `src/js/Promise.cpp`: `Promise::state_of(value)` reads the payload first and
  the cell second, and every reader in the engine goes through it. (It cannot
  live in `JsValue::promise_value()`: `src/js/JsValue.cpp` sees `PromiseData`
  forward-declared only, so it cannot touch `cell->internal`; and `#193` holds
  that file, so the later pull request moves — AGENTS.md.)
  The collector's trace of a promise cell does not count a handler's reactions
  as roots when more than three owners hold the state (a value payload, the
  cell, and the one lambda that settles it is the script-only count; a native
  owner — a resolver in a timer, a suspended async function — adds one and the
  reactions stay roots).
- `src/js/BuiltinsObject.cpp`: one line, because a promise named from its cell
  was reaching a `promise_value()` check its own `ObjectClass::Promise` case had
  already answered.

## What is still open

- The `use_count() > 3` baseline is a **conservative heuristic**, not a
  proof. It agrees with the unstressed run on 678/678 DOM cases and 28/28
  script cases across five seeds, but it can only be wrong in the direction
  that keeps more alive than it needs to. A real ownership graph for
  `PromiseData` (explicit roots, one owner per pending job) would replace it.
- `Promise.try`, `Promise.allKeyed` and `Promise.allSettledKeyed` are absent.
- `await` and async functions do not consult a subclass's species.
