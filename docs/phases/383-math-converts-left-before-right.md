# Phase 383 — Math converts left before right

## What was wrong

`Math.atan2(y, x)` and `Math.pow(base, exponent)` each converted both
arguments inside a C++ call. C++ does not guarantee left-to-right evaluation
of function arguments. On this GCC build it called the second `valueOf` first:
side effects were reversed, a second-argument throw hid a first-argument throw,
and the second conversion could miss a mutation made by the first. ECMAScript
requires the first `ToNumber` to complete before the second begins.

## What changed

Both methods now convert their first and second arguments in separate,
sequenced statements, then call the existing numeric operation. If the first
conversion throws, the second is never attempted. If the second throws, the
first one's effects have already happened. This changes neither the numerical
argument orientation nor how NaN and signed zeros are computed.

## Evidence

```sh
make -j2 all
python3 tools/conformance/verify_math_conversion_order.py
python3 tools/conformance/run_test262.py built-ins/Math
make test
```

The independent Node.js v22.22.3 oracle reports **8/22 before, 22/22 after**.
It tests order, first/second throws, an argument mutating the other argument,
NaN, symbol errors and missing arguments for both methods, along with control
cases for `imul`, `min`, `max`, `hypot`, and numerical sign/orientation. The
standalone suite exercises both methods and verifies the same consequences.
`make test` passed **442 engine suites and 114 cross-origin checks**.

The pinned test262 `built-ins/Math` directory remained **325/327**, with no
losses or gains between the old and new binaries: it did not expose these
particular order defects. The oracle and unit suite are therefore not presented
as a test262 improvement.

## Still open

This is an audit of `atan2` and `pow` and the named control cases, not of every
built-in that converts multiple arguments or of every floating-point edge case.
A different compiler might already choose the expected order on the old code;
the old source nevertheless did not guarantee it.
