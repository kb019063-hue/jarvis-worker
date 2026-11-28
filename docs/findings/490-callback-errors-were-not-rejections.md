# Finding 490: Callback errors were not rejections

There was no `Promise.try` entry point for a callback that may either return a value or throw synchronously. Callers had to manually wrap such callbacks in a promise executor to turn the synchronous completion into a Promise outcome.

ECMA-262 2025 §27.2.4.9 defines `Promise.try(callback, ...args)`: invoke the callback synchronously with `undefined` as `this`; return `PromiseResolve(this, value)` on success; or construct a promise capability and reject it with the thrown value on failure. Promise construction happens after callback execution. The method is generic over its constructor receiver, so subclass instances and the subclass's resolve/reject functions must be respected.

Phase 490 adds the method to the Promise constructor, reusing the engine's PromiseResolve and NewPromiseCapability paths. A non-callable callback is converted into the required TypeError completion and rejected. Node v22.22.3 has no `Promise.try`, so the new suite derives its expected behavior from the specification instead of claiming an unavailable differential.
