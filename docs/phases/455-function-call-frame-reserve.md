# Phase 455 — reserve the small function frame before filling it

Task: take the `invoke_function` environment-setup item from Phase 204,
finding 208. This remains separate from Phase 425's global-`var` path.
Finding 455. Agent: Qelari.

## 1. What the call builds

Phase 204's profile put `invoke_function` at about 2.4 microseconds per call:
a new `JsEnvironment`, followed by bindings for `this`, parameters, `var`
names and hoisted functions, with optional `arguments`, `new.target` and
`super` state. `JsEnvironment::define` appends new bindings to a vector, whose
geometric growth can allocate and move bindings more than once for a small
frame. The `call` benchmark exercises one million calls to `add(a, b)` inside
a function; the global-`var` case remains Phase 425's work.

## 2. Reserve only a bounded, known frame

`JsEnvironment` now exposes `reserve_bindings`. Before defining anything,
`invoke_function` estimates the bindings for simple parameters without
parameter expressions: parameters, function-scoped `var` names, hoisted
functions, and applicable function bindings (`this`, `arguments`,
`new.target`/callee state, and `super`'s home object). It reserves only
estimates from 2 through 16. Estimates outside that range and functions with
parameter expressions keep the existing path. For arrows the estimate covers
only their own parameters, `var` names and hoisted functions; lexical
`this`/`arguments`/`new.target` continue resolving through the enclosing
function, as before. Parameter expressions are excluded because their
parameter and body variable environments are prepared separately.

This changes allocation capacity only: `define` still handles duplicate
parameters and `var` redeclarations, and the vector remains free to grow if
needed. `tests/suites/455-function-call-environment-reserve.cpp` covers
parameters, duplicate parameters, hoisted functions, `var` redeclaration,
`arguments`, closures, a default parameter and arrows.

## 3. Measurement

The pre-change binary was built at claim commit `005ba21` and copied to
`/tmp/litebird-main-before-455`. Two separate batches of 31 interleaved runs
of `call` gave:

| batch | before | after | speedup | Node reference | ratio to Node |
| :--- | ---: | ---: | ---: | ---: | ---: |
| 1 | 650.3 ms | 609.7 ms | **1.07×** | 28.8 ms | 21.2× |
| 2 | 644.1 ms | 604.8 ms | **1.06×** | 25.9 ms | 23.4× |

Across both batches the medians are about **1.07× faster** than the pre-change
binary.

```sh
python3 tools/bench/bench_js_core.py build/litebird \
  --baseline /tmp/litebird-main-before-455 --only call --reps 31
```

Run the command twice for the two batches above. The full seven-case `make bench-js-core` also ran. Its cases moved in both
directions on this two-vCPU host, so the measured claim is limited to the
repeated `call` comparison; the remaining frame setup is not claimed to be
eliminated.

## 4. Verified

- `python3 tools/ci/check_suites.py` — suite shape passed.
- `make -j2` — build completed; `make test` — **499/499** engine suites and
  **114/114** cross-origin Window/Location and MessageChannel checks.
- `make conformance` — **1973/1973**; `make js-conformance` — **1153/1158**,
  everyday **90/90**, 0 crashes and 0 timeouts.
- `make dom-conformance` — **678/678**; `make url-conformance` — **1188/1188**.
- `make xml-conformance` — **1718/1718**, with 261 canonical forms compared,
  2585 documents run, 0 crashes and 0 timeouts.
- `make wpt` — **9570/9824**; all 254 failures are listed, 0 unlisted.
- `make diff-trees` — all four batches of 1500 randomized trees matched the
  corrected parse5 reference; `make deep` — **1332 runs** across 465 pages at
  depth 200,000, 0 failed.
- `make diff-xml` — 3958/4000 documents agreed with Expat and libxml2; 0
  `DIFFER`. XML round trips — **3000/3000**.
- `make fuzz` — parser 400, pipeline 400, XML 600, media 2400, color 5008
  and Brotli 2000 cases/inputs; 0 findings or crashes.
- `call` benchmark — two 31-run batches, **1.06–1.07×** over the pre-change
  binary, as recorded above.
- Focused ASan/UBSan run of the new suite — **1/1 passed**. The full `make
  asan` attempt exceeded the 1800-second command limit without a captured
  result. `make asan-unit ASAN_UNIT_STRETCHES=balanced` completed five of six
  stretches; deep stretch 2 was SIGKILLed (`-9`) alongside stretch 3 (reported
  peaks of 2384 MB and 1628 MB on a 3939-MB host). A retry of stretch 2 alone
  also terminated in `deep_nesting_never_exhausts_the_native_stack`, without
  a sanitizer report. The new suite ran in the successful first stretch and
  passed separately; full sanitizer coverage remains incomplete under the
  sandbox's resource limits.

## 5. What remains

The reserve trims vector growth for small frames; it does not remove the
`shared_ptr` environment, per-binding `define` calls, `prepare` lambda or
`EnvScope` from `invoke_function`. The roughly 2.4-microsecond profile item
is the whole call-frame setup, not a cost this bounded reservation eliminates.
The other Phase 204 items (`JsValue::set_property`'s exotic `std::function`,
the unproven stack-floor cache, and `Binding`'s name storage) remain separate.
Phase 425's global-`var` optimization was not changed.
