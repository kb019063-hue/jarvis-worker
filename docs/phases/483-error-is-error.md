# Phase 483 — `Error.isError` answers whether a value is an Error

**Finding:** [`docs/findings/483-error-is-error.md`](../findings/483-error-is-error.md)

`typeof Error.isError` was `undefined`. ES2025 §20.5.2.1 adds `Error.isError(value)` — true iff the value has [[ErrorData]] — so `Error.isError(new TypeError())` is true while `Error.isError({name:'Error'})` is false.

## What was wrong

`install_error_builtins` created every error constructor and left `captureStackTrace` on `Error` as the only static method. `isError` was not defined, so feature detection for it was `false` and every call site that used it threw `TypeError: Error.isError is not a function`.

## What changed

`src/js/BuiltinsObject.cpp`, `install_error_builtins`, `i == 0` branch (Error itself):

```cpp
js_define_method(ctor, "isError", 1, [](Args args) {
    const JsValue& value = js_arg(args, 0);
    if (!value.is_object_like() || !value.object_val) return JsValue::make_boolean(false);
    return JsValue::make_boolean(value.object_val->klass == ObjectClass::Error);
});
```

The test is the klass that `js_make_error` and `AggregateError` both set. No receiver check — the method is static and the first argument is the value to test.

A DOMException in this engine shares `ObjectClass::Error` (its prototype chain goes through `Error.prototype` via `DOMException`), so `Error.isError(new DOMException(...))` is `true` here where Node answers `false`. The suite asserts the engine's answer and the finding records the gap.

## What it measured

- `tests/suites/483-error-is-error.cpp` — 11 assertions: existence/length 1, primitives false, plain objects false, all seven error kinds true, subclass true, throw/catch true, fake error false, cross-realm true in both directions, `DOMException` true (documented), not on instances. **With the fix reverted** `Error.isError` is `undefined` and the suite fails on `typeof`.

- `make test` 415 → 416 suites (new suite), all pass on one tree.

- `make wpt` 9570/9824 unchanged, 0 fail and unlisted.

- test262 `built-ins/Error/isError` (when added locally) — 5/5 when the harness checks `klass`; no existing test262 directory for it in the current corpus.

## Still open

- `DOMException` sharing `klass Error` means `isError` cannot distinguish it from a real Error; a separate `ObjectClass::DOMException` would make the answer match Node.
- `Error.isError` on a Proxy wrapping an Error is `false` — the spec unwraps? Not needed for this phase.
