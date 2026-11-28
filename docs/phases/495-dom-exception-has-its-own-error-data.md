# Phase 495 — `DOMException` has its own `[[DOMExceptionData]]`

**Finding:** [`docs/findings/495-dom-exception-has-its-own-error-data.md`](../findings/495-dom-exception-has-its-own-error-data.md)

`Error.isError` added in Phase 483 answered `true` for `new DOMException`. The two shared `ObjectClass::Error` — the engine's stand-in for [[ErrorData]] — so the test was written to assert that wrong answer and the finding noted it as a gap.

A `DOMException` is not an `Error`. Its `[[DOMExceptionData]]` is distinct, `Object.prototype.toString` is `[object DOMException]` not `[object Error]`, and `Error.isError` must be `false` for it.

## What was wrong

`fill_dom_exception` set `klass = ObjectClass::Error`, and every `klass == Error` branch (stack accessor, clone, toString tag, `isError`) treated the two as one.

## What changed

- `include/litebird/js/Heap.hpp` — `ObjectClass::DOMException` added after `Error`.
- `src/js/DomBindings.cpp` — `fill_dom_exception` now sets `DOMException`.
- `src/js/JsOps.cpp` — `Error.prototype.stack` getter and `printed_tag` now accept both `Error` and `DOMException` (DOMException still has a stack).
- `src/js/BuiltinsObject.cpp` — `printed_tag` returns `DOMException` for the new klass, structured clone copies it, and the comment on `isError` updated. `isError` itself already checked `== Error` only, so it now correctly returns `false` for a DOMException.

Existing suite `483-error-is-error.cpp` updated: its DOMException assertion now expects `false`.

## What it measured

- `tests/suites/495-dom-exception-has-its-own-error-data.cpp` — 8 assertions: `isError` false for two DOMException names, `toString` tags `Error` vs `DOMException`, stack present for both, `structuredClone` keeps DOMException tag/name/message, cross-realm `isError` false for foreign DOMException and true for foreign TypeError.

- `make test` 526/526 (new suite + 483 fix), 0 fail.

- `make wpt` 9570/9824 unchanged, `make xml-conformance` 1718/1718.

## Still open

- `Error.isError` on a Proxy wrapping an Error is still `false` (needs unwrapping).
