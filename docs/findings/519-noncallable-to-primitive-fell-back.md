# Finding 519: noncallable Symbol.toPrimitive fell back silently

## What was wrong

The shared `js_to_primitive` helper treated every noncallable
Symbol.toPrimitive as if it were absent. An object with a numeric,
boolean, string, Symbol or object hook could therefore execute ordinary
conversion methods instead of throwing TypeError. This was the remaining
GetMethod issue explicitly recorded in Phase 518.

Pinned test262's
`test/built-ins/isFinite/toprimitive-not-callable-throws.js` quotes the
relevant algorithm: GetMethod returns undefined for null/undefined, but
throws if the retrieved value is otherwise not callable.

## What changed

Only `src/js/JsOps.cpp` changes in production. After reading the hook
once, ToPrimitive rejects a non-nullish noncallable value. The existing
callable dispatch and ordinary fallback remain unchanged, including
receiver, arguments, hints and exception propagation.

## Verification

`tests/suites/519-noncallable-to-primitive.cpp` has **223 checks** across
eleven conversion callers and all three hints, including inherited hooks,
null/undefined controls, getter/call exceptions, object returns, property
mutation and collection in a getter. Node v22.22.3 with `--expose-gc`
returns **`ok 223`** for the same script. C++ asserts the explicit final
result so failed script evaluation cannot accidentally pass the suite.

The baseline fails **157** focused checks; fixed plain and explicit -O0
ASan/UBSan builds pass all **223**. Same-base pinned test262 comparisons
improve **2,596 → 2,600 / 2,725** conversion cases and **15,236 → 15,240 /
15,686** default helper-regression cases: **eight distinct gains, zero
losses**. All eight gained cases also pass ASan/UBSan. Plain units pass
**560/560**, plus **114/114** cross-origin checks.

See [Phase 519](../phases/519-noncallable-to-primitive.md) for reproducible
commands and broader validation, including limits: the broader explicit
-O0 sanitizer run has one unrelated empty-string-repeat timeout, local
deep validation was interrupted without a final result, and full default
sanitizer/deep CI must pass in the landing session before merge.

## What is still open

This is the shared ToPrimitive GetMethod check only. ToPropertyKey's
Symbol preservation, Proxy/class-callability behavior, other GetMethod
sites and built-in-specific fast paths are not changed. It is not a
repository-wide GetMethod refactor or a full ECMAScript conformance claim.
