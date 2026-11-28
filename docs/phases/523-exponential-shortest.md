# Phase 523 — Shortest exponential digits keep their exponent

## What was wrong

With no precision argument, `Number.prototype.toExponential` took its digits
from the shortest decimal representation but its exponent from a separate
17-digit C-library representation. A binary64 number can be just below a
power of ten while its shortest decimal representation is that power: the
two exponents then disagree. `(1e23).toExponential()` returned `1e+22`,
`(1e-6).toExponential()` returned `1e-7`, and `(1e-323).toExponential()`
returned `1e-324`, which does not even round-trip to the original value.

## What changed

The exponent is derived from the same shortest representation as the digits,
accounting for the decimal point, leading zeros and any scientific exponent.
Zero and the sign remain special-cased as before; explicit-precision formatting
is unchanged.

## Evidence

`python3 tools/conformance/verify_exponential_shortest.py` compares 3,412
outputs with Node: all binary64 powers of ten from 1e-323 through 1e308 and
their adjacent floats, signed and special values, and seeded random bit
patterns. **3,110/3,412 before, 3,412/3,412 after** agreed. The standalone
suite tests affected powers, subnormals, zero, normal mantissas and the
explicit-precision path. Pinned test262 `built-ins/Number/prototype/toExponential`
remained **14/15**, with no gains or losses:

```sh
python3 tools/conformance/run_test262.py --binary before=build/litebird-exp-before --binary after=build/litebird built-ins/Number/prototype/toExponential
```

## Still open

This sampled comparison is not a proof that the underlying shortest-decimal
conversion agrees with every ECMAScript implementation for every binary64
value, nor an audit of other Number formatting methods.
