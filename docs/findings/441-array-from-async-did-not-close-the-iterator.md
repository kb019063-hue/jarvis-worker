# Finding 441 — Array.fromAsync left its iterator open

**Severity:** observable spec violation. A mapper can stop an `Array.fromAsync` walk while the source still owns resources or relies on `return` for cleanup; the engine used to reject the result without calling it. The walk also re-read the `next` property at every step instead of keeping the Iterator Record's method.

**Found by:** the open Array.fromAsync item measured in Phase 415.

## Reproduction

```js
let closed = 0;
let i = 0;
const source = {
  [Symbol.asyncIterator]() {
    return {
      next() { return Promise.resolve({ value: ++i, done: false }); },
      return() { closed++; return Promise.resolve({ done: true }); }
    };
  }
};
try {
  await Array.fromAsync(source, () => { throw new Error("stop"); });
} catch (e) {
  // `closed` must be 1.
}
```

Before Phase 441, `closed` was `0`; the mapper's throw rejected `fromAsync`, but its iterator was never closed. A getter for `next` was also read once per step: a two-value source observed three reads instead of the Iterator Record's single read.

## The boundary matters

This is not “close on every error.” `Array.fromAsync` closes at its explicit IfAbruptCloseAsyncIterator sites: mapper call, mapper-result await, CreateDataPropertyOrThrow, and the safe-integer limit. A failure while acquiring or stepping the source—a rejected `next`, a non-object result, or a throwing `done`/`value` access—is a `?` completion and must leave that iterator alone. For a sync iterable, CreateAsyncFromSyncIterator supplies the asynchronous wrapper: it awaits each sync value, and a rejected yielded value closes the underlying sync iterator through the wrapper's IteratorClose behavior.

AsyncIteratorClose awaits the result of `return`. When the caller already has a throw completion, step 5 preserves it over errors from `return` and over a non-object answer; this includes a caller's `throw undefined`. For a normal completion, a rejecting close propagates and a non-object answer is a TypeError.

Node.js v22.22.3 is not a complete oracle here: V8 closes after some source-step failures that the standard's `?` steps do not close. Those cases are pinned to the algorithm in the suite rather than copied from Node.

## Fix and evidence

Phase 441 replaces the duplicated iterable loops with one iterator-record walk, caches `next` once before result construction, uses the existing async-from-sync adapter, and closes only at the specified sites. A direct C++ assertion also covers AsyncIteratorClose's normal-completion TypeError case, while the fromAsync cases verify that the caller's error retains precedence.

`tests/suites/441-array-from-async-closes-its-source.cpp` is the regression suite. It fails against the previous implementation on both the missing close and repeated `next` read. The final build, suite, test262 comparison, and corpus totals are recorded in Phase 441.
