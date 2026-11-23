# Finding 208 — the row's number was a guess, and `i++` walked the chain twice

Status: **measured, and the counter's cost resolved.** Open: the call frame and
global `var` writes. Phase 204,
`docs/phases/204-the-tree-walker-pays-for-names-it-already-found.md`.

## The finding

Two things, and the first is why the second could be judged.

**1. `ROADMAP.md` row 7 said the tree-walker is "50–100× slower than V8 on
loops and calls". Nobody had measured it.** Measured (`make bench-js-core`,
medians of interleaved runs, Node v22.22.3 on the same box): **3× on recursion
and array indexing, 9× on a plain loop, 18× on a property loop, 27.5× on a
call.** The row was right about calls and wrong about loops by an order of
magnitude, and a phase that takes "make loops faster" from it would be aiming
at the wrong quarter of a second.

**2. `i++` cost two walks of the scope chain, two `std::string` copies and two
`JsValue` copies.** `eval_update` built a `Reference` — `ref.name` a copy of
the identifier's name, `ref.base` a copy of a `JsValue` — read the binding
through it, converted it, then called `write_reference`, which walked the
chain again to find what it had just found. Every `for` loop in every script
pays this once per iteration.

## What changed

`eval_update` checks the target first: for an identifier, one `lookup` finds
the binding and the write lands on it. `const`, a read-only binding and a TDZ
binding keep the exception the general path raises, in its words; a name that
is no binding at all falls through to `resolve_reference` unchanged.

`loop` **1.11–1.13×**, `property` **1.07–1.09×**, `array` **1.01–1.10×**,
`call`/`method`/`recursion` unchanged.

## What was built, measured and reverted

- A per-scope cache of the last four bindings looked up, read and written by
  the four name paths. Correct (only a binding of the starting scope is
  remembered, and one scope never holds two bindings of a name), and worth
  **nothing**: 1.04× / 1.00× / 1.13× at the cost of 176 bytes of every scope.
  The reverted patch was larger than the one that shipped. In this engine the
  scope chain is not where a loop's time goes; the `Reference`'s copies and
  the second walk were.
- A global version stamp to invalidate that cache: bumped 3,000,078 times in a
  1M-iteration loop, because `invoke_function` defines three or four bindings
  per call. It never hit.
- Caching the thread's stack floor (`js_check_stack` recomputes it per
  expression, ~2% of the profile): no measurable gain, reverted. The
  sanitizer-build failure that made it suspicious was reproduced on an
  unpatched `3a0b600`, so it was collateral, not cause.

## Still open

- **A global `var` is written twice per assignment**: the binding, then the
  global object's property (through `WindowExotic::set` and `assign_hook`). A
  top-level loop is 748 ms where the same loop inside a function is 355 ms.
  The largest measured item in the phase; next to take.
- **`invoke_function`** builds a `shared_ptr` environment, a `define` per
  parameter and per `var`, a lambda and an `EnvScope` per call — ~2.4 µs, the
  `call` row's 22–27×.
- `JsValue::set_property`'s exotic branch compares origins and calls through a
  `std::function` for every write to the global object.
- `Binding` is 80 bytes, 32 of them an uninterned `std::string` name.
