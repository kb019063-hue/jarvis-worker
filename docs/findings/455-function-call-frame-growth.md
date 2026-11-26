# Finding 455 — small function calls grew their binding vector repeatedly

Status: **measured and optimized for bounded small frames.** The total
`invoke_function` setup is still open; see
[`docs/phases/455-function-call-frame-reserve.md`](../phases/455-function-call-frame-reserve.md).

## The finding

Phase 204's profile attributed about 2.4 microseconds per call to
`invoke_function`. Every new frame adds bindings for parameters and other
function state to a `std::vector`; for the small frames in the `call`
benchmark, its geometric growth can reallocate and move bindings more than
once. This is distinct from Phase 425's global-`var` property path.

## What changed

`invoke_function` now reserves an estimate for simple-parameter functions
without parameter expressions, using known parameter, `var`, function-declaration
and applicable function-state counts. Only estimates between 2 and 16 are
reserved. `define` and the existing parameter/var initialization remain
unchanged. The regression suite covers parameters, duplicate parameters,
hoisted functions, `var` redeclaration, `arguments`, closures, defaults and
arrows.

## Measurement and verification

The pre-change binary was built at claim commit `005ba21` and copied to
`/tmp/litebird-main-before-455`. Two batches of 31 interleaved `call` runs
measured **650.3 → 609.7 ms (1.07×)** and **644.1 → 604.8 ms (1.06×)**.
Node measured **28.8 ms** and **25.9 ms**, respectively (21.2× and 23.4×).

```sh
python3 tools/bench/bench_js_core.py build/litebird \
  --baseline /tmp/litebird-main-before-455 --only call --reps 31
```

Run the command twice for the two batches above. `make test` passed **499/499** engine suites, including the new regression;
the focused regression also passed **1/1** under ASan/UBSan.

## Still open

The reservation does not remove the shared environment allocation, per-binding
`define` calls, `prepare` lambda or `EnvScope`; the profile's overall
call-frame setup remains. The `JsValue::set_property` exotic callback, stack
floor and `Binding` storage are separate Phase 204 items. No Phase 425 global
path was changed.
