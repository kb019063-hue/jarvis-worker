# Phase 285 — an error that collects

Task row: `AggregateError` did not exist, and `Promise.any` faked it.
Finding 290.

## 1. What was wrong

§20.5.7 has eight error types. This engine had seven: `typeof AggregateError`
was `undefined`, and every one of the 25 tests in test262's
`built-ins/AggregateError` failed — 0 passed.

The bug that hid behind it is in `Promise.any`. When every input rejected, it
made a plain `Error`, put a hidden `name: "AggregateError"` on it, and hung
`errors` off it the same way. The value read like an `AggregateError` and was
not one:

```js
try { await Promise.any([Promise.reject('a')]); } catch (e) {
  e instanceof AggregateError   // threw: there was no constructor to ask
  e.errors                      // [undefined] for an empty list: Node has []
  Object.getPrototypeOf(e)      // Error.prototype, not AggregateError.prototype
}
```

Its `errors` was wrong for a second reason as well. The combinator kept one
value slot per input, started at `undefined`, and handed the whole list to the
rejection half — so it could not tell an index that never rejected from a
reason that *is* `undefined`, and a rejected `undefined` was dropped while
every other index came back as a hole.

## 2. What changed

`AggregateError` is installed beside the other seven in `install_error_builtins`
(`src/js/BuiltinsObject.cpp`), per §20.5.7.1: callable and constructible,
`length` 2, `[[Prototype]]` of the constructor `Error`, a prototype with `name`,
`message` and `constructor` and no `errors` of its own, `errors` an own
non-enumerable writable data property holding `IterableToList(errors)`, `cause`
from the options bag as `Error` does it, and the message defined only when one
was given — step 4 defines it for a message that is not `undefined`, so
`new AggregateError([])` has none of its own. The prototype is made on first
use, because the realm's own constructor in `JsOps.cpp` makes the seven and
that file belonged to another open pull request; `JsIntrinsics::
aggregate_error_prototype()` holds it and roots it.

`Promise.any` is now its own function rather than a `combine()` call
(`src/js/Promise.cpp`), because it needs to count what has rejected and to keep
an `undefined` reason apart from a promise that has not settled. It rejects with
a real `AggregateError` made through the realm's constructor, with the reasons
in the order the promises were given and nothing else in the list. A custom
`then` that settles a promise while the list is still being built is handled:
the list is complete only when the iterator has ended, which is what
`CountRemainingElements` is for.

## 3. How it was verified

`tests/suites/285-aggregate-error.cpp`, a file of its own in the form
`tests/suites/README.md` asks for, holds every answer below to Node v22.22.3:
the constructor's shape and descriptors, `errors` and its descriptor, the
message that is and is not there, a bad message thrown before the list is
iterated, a subclass's prototype, and six `Promise.any` cases run on a timer —
`instanceof`, the reason order, an `undefined` reason, an empty list, the
fulfilment that wins, and the identity of a reason object.

```bash
make test      # All 425 engine test suites passed successfully!
```

| Check | Result |
| :--- | :--- |
| `make test262` on `built-ins/AggregateError` | 0/25 → **21/25** |
| `make test262` on `built-ins/Promise` and `built-ins/AggregateError` | 572/757 (75.6%) |
| `make js-conformance` | 1150/1158 (99.3%), 0 crashes |
| `make conformance` / `make dom-conformance` | 1973/1973 · 678/678 |
| `make wpt` | 9569/9824, the same 255 listed |
| `make url-conformance` / `make xml-conformance` | 1188/1188 · 1718/1718 |
| `make diff-trees` / `make diff-xml` | 1500/1500 trees equal, no crash |
| `make render-conformance` | 65/66 and 40/41, only the recorded failures |
| `make security-fetch` / `-worker` / `-https` / `-websocket` | 3/3 · 17/17 · 27/27 · 61/61 |
| `make verify-gc-stress` | 11830 |

Negative control: with the three source files as `main` has them and this suite
in place, it fails at its first assertion — `AggregateError` is not defined.

The six `Promise.any` cases settle on timers, so the suite runs
`EventLoop::default_loop().run_until_idle` before it reads the answers, and the
last question is asked from a timer rather than after a fixed wait. The first
draft waited on a 100 ms timer without running the loop at all: its answers
never ran, and the timer it left behind made `event_loop_and_timers` fail in
two of three runs. Nothing depends on real time now — the delays only fix the
order the reasons arrive in.

## 4. What is still open

- **The four tests left in `built-ins/AggregateError` are not this task's.**
  Two need `Proxy` (absent), one needs `$262` (a test262 host object), and
  `prop-desc.js` needs a global that can be deleted — see the row below.
- **A built-in global is reachable but is not an own property of the global
  object.** `Object.getOwnPropertyDescriptor(globalThis, 'Error')` answers,
  `'Error' in globalThis` is `true` and `hasOwnProperty` is `true`, yet
  `Object.getOwnPropertyNames(globalThis)` lists only `requestAnimationFrame`,
  `cancelAnimationFrame`, `queueMicrotask` and `crypto`, and
  `delete globalThis.Error` answers `true` and leaves `Error` there. Every
  global the library installs has this shape, so it is a new row of the table
  and not a change in passing.
