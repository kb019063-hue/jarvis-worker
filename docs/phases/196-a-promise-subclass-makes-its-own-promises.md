# Phase 196: a `Promise` subclass makes its own promises

## What was built

`NewPromiseCapability` (§27.2.1.5), `SpeciesConstructor` and a promise's state
being reachable from its object, so that a class that extends `Promise` gets
its own kind of promise back from every method the standard makes one in.

`src/js/Promise.cpp`, `include/litebird/js/Promise.hpp`:

- **`PromiseCapability`** — a `{ promise, resolve, reject }` triple and the two
  constructors of one: `new_capability(C)`, which runs `GetCapabilitiesExecutor`
  for a `C` that is not %Promise% and makes the engine's own state for one that
  is; and `capability_for(data, C)`, which is the same question asked by `then`.
- **`GetCapabilitiesExecutor`** (§27.2.1.5.1): anonymous, `length` 2, no
  `[[Construct]]`, and it throws if it is given a pair when it already has one.
- **`PromiseResolve(C, x)`** (§27.2.4.7.1): a value that is already a promise of
  `C` is the answer itself, not a new promise around it. Every combinator asks
  `this.resolve` for each item, and this is what keeps the `then` of that item
  inside `js_iterate`, so a `then` getter that throws still closes the iterator.
- **`then`, `catch`, `finally`, `resolve`, `reject`, `all`, `allSettled`, `any`,
  `race` and `withResolvers`** all go through the capability. `then` asks
  `SpeciesConstructor(receiver, %Promise%)`; `finally` still goes through
  `then`, so a subclass's species is honoured there too.
- **`PromiseData` is reachable from its cell** as well as from the value
  (`JsObject::internal` when `klass == ObjectClass::Promise`), because
  `super(...)` in a derived constructor hands the native parent the object
  `new` made, and a cell copied by `take_over` keeps what the cell holds and
  not what the value held. `JsValue::promise_value()` reads the payload first
  and the cell second; `JsValue::make_object(cell)` re-attaches the payload.
- **`Symbol.species`** is on the constructor as a getter that answers `this`.

## The two bugs this uncovered

Both were in the engine already and only showed once a capability's own
functions were called from inside a built-in:

1. **`current_this()` is a reference into the call context.** A native that does
   `Promise::func(JsValue::current_this(), …)` and lets that call start another
   call has the receiver overwritten under it: `CallContextScope` saves the
   context by move and assigns `t_call_context.this_val`, which is the very
   object the reference points into. The constructor a static method was called
   on became that method's new `this` — an object — and `%fn` inside the class
   constructor was then not a function, so `super(...)` said *'super' keyword
   unexpected here*. Copy the receiver (`const JsValue constructor = …`) before
   handing it to anything that can call.

2. **`settle_through` ignored its `rejected` flag.** A rejection that passed
   through a `then` with no handler for it was settled through the capability's
   *resolve*, so it arrived at the next `then` as a value: `catch` was never
   reached. It now picks `[[Reject]]` for a rejection and `[[Resolve]]` for a
   value, as PromiseReactionJob steps 8-9 have it, and `PromiseHandler` carries
   the reject half it never had.

## Verification

- `make test`: **343/343** suites pass. The engine's own suites found every
  one of the regressions along the way (see below).
- `make js-conformance`: **1147/1158** (99.1%).
- `make test262`: **14969/15686 (95.4%)** over the whole pinned suite.
  `built-ins/Promise` alone: **537/732**, from 470/732 on `main` — **+67**.
  Per directory, `main` → now:

  | directory | `main` | now |
  |---|---|---|
  | `prototype/then` | 65/75 | 73/75 |
  | `prototype/catch` | 10/14 | 14/14 |
  | `prototype/finally` | 14/29 | 18/29 |
  | `all` | 74/98 | 81/98 |
  | `allSettled` | 81/104 | 87/104 |
  | `any` | 69/94 | 74/94 |
  | `race` | 75/94 | 83/94 |
  | `resolve` | 19/30 | 28/30 |
  | `reject` | 6/15 | 14/15 |
  | `withResolvers` | 2/6 | 5/6 |
  | `Symbol.species` | — | 5/5 |

  The files at the root of the directory that now pass and did not on `main`:
  `executor-function-name.js`, `executor-function-length.js`,
  `executor-function-not-a-constructor.js`, `executor-function-prototype.js`,
  `executor-function-extensible.js`, `reject-function-*`,
  `resolve-function-extensible.js`, `resolve-function-length.js`,
  `resolve-function-prototype.js`, `resolve-self.js`,
  `exception-after-resolve-in-executor.js`,
  `get-prototype-abrupt-executor-not-callable.js`, `undefined-newtarget.js`,
  `is-a-constructor.js`, `constructor.js`, `length.js`, `name.js`, and ten of
  the `resolve-via-*`/`reject-via-*` queue cases.
- `make verify-gc-stress`: 28/28 cases × 3 seeds and 678/678 DOM cases × 2
  seeds agreed with the unstressed run; 0 differed.
- Probes run against Node.js and compared line by line: subclass construction,
  `then` chaining through two steps, thenable adoption, rejection
  pass-through, `catch`, `finally`, `P.resolve`/`P.reject`/`P.all`/`P.allSettled`/
  `P.any`/`P.race`/`P.withResolvers` all answering `instanceof P`, and
  `Reflect.construct(Promise, [], NewTarget)` reading `NewTarget.prototype`
  exactly once and complaining about the executor first.

## The suites that caught the work

`js_async_promises_and_json` (chain step two got the first step's value, then a
`resp.json()` handler was not adopted), `promise_combinators_resolve_before_advancing_and_close_on_abrupt_completion`
(the iterator stopped being closed), `fetch_and_workers_use_the_document_origin`
(a rejection was fulfilled), `js_new_target_is_the_function_new_was_applied_to`
(the `new.target.prototype` getter was read twice, and read at all when the
executor was not callable), `js_a_function_is_the_prototype_of_a_function` (a
`set_native_function` change was reverted).

## What is still open

- `Promise.try` (15 cases) and the two `Keyed` combinators (`allKeyed`,
  `allSettledKeyed`, 45 and 44 cases) are whole features this engine does not
  have; they are not this row.
- `await`, async functions and the engine's own internal promises still make
  %Promise% and do not consult a subclass's species.
