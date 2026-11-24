# Finding 290 — AggregateError did not exist, and Promise.any faked it

**Resolved by Phase 285** (`docs/phases/285-an-error-that-collects.md`).

## 1. What was found

`typeof AggregateError` was `undefined`; Node v22.22.3 answers `function`.
test262's `built-ins/AggregateError` was 0 of 25, and nothing in this
repository had asked for the constructor, so nothing recorded it.

`Promise.any` was the place it showed. When every input rejected it made a
plain `Error` and gave it a hidden `name` and `errors`:

```js
Promise.any([Promise.reject('a')]).catch(e => {
  e.name                          // 'AggregateError'
  e instanceof AggregateError     // threw: no such constructor
  Object.getPrototypeOf(e)        // Error.prototype
  e.errors                        // ['a'] here, but [undefined] for an empty list
});
```

The last line is a second defect: the combinator's value slots start as
`undefined`, so the rejection half could not tell a reason that is itself
`undefined` from an index that never rejected, and an empty list came back
`[undefined]` where Node has `[]`.

## 2. What was done

The constructor, per §20.5.7.1, beside the other seven error types, with its
prototype made on first use; and `Promise.any` given its own counting
implementation that rejects with a real `AggregateError` carrying exactly the
reasons, in order.

## 3. Evidence

`make test` **414/414**, with `tests/suites/285-aggregate-error.cpp` holding
every answer to Node's. test262 `built-ins/AggregateError` 0 → 21 of 25; the
four left need `Proxy`, `$262`, or a global that can be deleted. On `main`'s
three files the suite fails at its first line. Every other check is unchanged.

## 4. What is still open

A built-in global is not an own property of the global object:
`Object.getOwnPropertyNames(globalThis)` omits all of them and
`delete globalThis.Error` answers `true` and does nothing. That is its own
row.
