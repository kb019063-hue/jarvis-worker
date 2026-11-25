# Phase 412 — `Array.fromAsync`

Task: the row "A script can read and write the engine's own slots" names
`Array.fromAsync` among the things still absent. It was absent: `typeof
Array.fromAsync` was `"undefined"` against Node's `"function"`. Finding 412.

## 1. What the method is

§23.1.3.1. Every other `Array` maker is synchronous; this one **awaits**, and
that is the whole of it:

- a value from the source is awaited before it is stored, so a `Promise` in the
  source becomes its settled value, not the promise;
- the mapper's *result* is awaited as well, so a mapper may be `async`;
- an async iterable is asked for `@@asyncIterator` **before** `@@iterator`
  (step 4), and only a source with neither is read as an array-like;
- every failure inside the work — a bad mapper, a throwing mapper, a rejected
  value, a non-iterable `null` — **rejects** the promise rather than throwing
  out of the call. The one exception is a `this` that is not a constructor,
  which step 2 throws.

## 2. How a built-in awaits

This was the only real design question. `await` has two paths in this engine
(`JsInterpreter::eval_await`): inside an async function the frame is parked and
a microtask resumes it, and **anywhere else the wait is a blocking one** — run
the event loop until the promise is no longer pending. A built-in runs to
completion and is not on the interpreter, so it takes the second path. The
helper is eight lines and is the same loop `eval_await` runs:

```cpp
auto promise = Promise::to_promise(value);
promise->handled = true;
while (promise->state == PromiseState::Pending) {
    if (loop.spin_once() == 0) {
        if (!loop.has_pending_tasks()) js_throw_type_error("… can never settle …");
        loop.wait_for_work(false);
    }
}
if (promise->state == PromiseState::Rejected) js_throw(promise->result);
return promise->result;
```

A rejection is re-thrown as a `JsThrow`, and the outer `try` turns it into
`Promise::make_rejected` — which is §23.1.3.1's reject-capability, spelled the
way this engine spells it.

## 3. What it was measured against

Fourteen probes compared line for line with Node v22.22.3, all identical on the
first working build. The four worth naming:

| probe | answer |
|---|---|
| `Array.fromAsync({length:2, 0:Promise.resolve('p'), 1:'q'})` | `p,q` — an array-like's places are awaited too |
| `Array.fromAsync(asyncIterable)` | `10,20,30` — `@@asyncIterator` is preferred to `@@iterator` |
| `Array.fromAsync.call(C, [1,2])` with a constructor `C` | an instance of `C`, constructed with no argument (the iterable path) |
| `Array.fromAsync.call({}, [1,2])` | a plain array, not a `TypeError` |

## 4. The numbers

| corpus | before | now |
|---|---|---|
| `typeof Array.fromAsync` | `"undefined"` | `"function"` |
| `make test` | 464/464 | **465/465** |
| test262, all of it | 15,180 (96.8%) | 15,180 — **the pinned suite predates the method** |
| `make js-conformance` | 1,153/1,158 | 1,153/1,158 |
| `make wpt` | 9,570/9,824, 0 unexpected | 9,570/9,824, 0 unexpected |

**test262 does not move, and that is expected rather than a disappointment:**
the pinned commit (`2e58f87d8350`) has no `Array.fromAsync` tests — the method
reached the standard after it. The assertion of correctness here is the
Node-comparison and the new suite, not a conformance delta. Writing that down
matters, because a phase that reports "0 gained" looks like a failure unless it
says why.

## 5. What is still open

- `Promise.try` is **not** in Node v22.22.3 either (`typeof Promise.try` is
  `"undefined"` there), so the row's mention of it was measured before it was
  written and is not a gap that can be compared against the oracle. It was left
  alone for that reason. `Promise.withResolvers`, `Promise.any`,
  `Object.groupBy` and `Map.groupBy` already exist; `Set.prototype.union` and
  its relatives do not, and are a separate phase.
