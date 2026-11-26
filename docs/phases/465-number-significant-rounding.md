# Phase 465 — Significant-digit formatting rounds ties up

## What was wrong

`Number.prototype.toExponential` and `toPrecision` used `printf` to round
their specified precision. On this host `printf` rounds exact ties to even,
while ECMAScript selects the larger decimal integer at a tie. For example,
`(1.25).toPrecision(2)` gave `1.2`, rather than `1.3`.

## What changed

For explicitly supplied precision, both formatters now obtain a scientific
expansion with 1100 digits after the decimal point and round its digits
explicitly, propagating carry to the exponent. A binary64 number has a
denominator no larger than 2^1074, so this is enough to expose its entire
finite decimal expansion, even for subnormals. Zero, negative signs, exponent
formatting and the undefined-precision paths remain separate and unchanged.

## Evidence

`python3 tools/conformance/verify_number_significant_rounding.py` compares
4,428 results with Node, covering exact ties, values adjacent to ties,
exponent carries, large/small and random binary64 values, special values and
precisions from 0 to 100: **4,418/4,428 before, 4,428/4,428 after**. The
standalone engine suite covers ties, nearby non-ties, carries and controls.
Pinned test262 for both methods improved **27/32 → 28/32**, one gained and
none lost:

```sh
python3 tools/conformance/run_test262.py --binary before=build/litebird-number-ties-before --binary after=build/litebird built-ins/Number/prototype/toExponential built-ins/Number/prototype/toPrecision
```

## Still open

This does not audit the shortest exponential path or other numeric formatting
methods. Rendering a long exact expansion on every explicit-precision call
has a cost; no performance improvement is claimed. It relies on the C
library's scientific conversion and decimal separator under the process locale.
