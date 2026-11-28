# Finding 483 — `Error.isError` was not a function

**Severity:** missing API. ES2025 §20.5.2.1 `Error.isError` is the way a library asks “did this value come from `throw new Error`” without `instanceof`, which fails cross-realm. `Error.isError(new TypeError())` should be `true`, `Error.isError({})` `false`, `Error.isError` on a window's error from an iframe `true` in both directions.

**Found by:** reading the ES2025 list of what `Error` has — `captureStackTrace` was the only static method, `isError` was not there, and `typeof Error.isError` answered `undefined` where Node v22.22.3 (with `--harmony-error-iserror`) and the test262 proposal do.

**Repro:**

```js
typeof Error.isError // "undefined"  (should be "function")
Error.isError(new Error("x")) // TypeError: not a function
```

**What changed:** one `js_define_method(ctor, "isError", 1, ...)` on `Error` itself, returning `klass == ObjectClass::Error`. The seven error kinds and `AggregateError` all set that klass; `DOMException` shares it, so it answers `true` where the spec would answer `false` — recorded as still open.

**Still open:** a dedicated `ObjectClass::DOMException` so `Error.isError(new DOMException(...))` is `false`, and Proxy unwrapping.
