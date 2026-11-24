# 369. A sum that rounded every step

## What was wrong

`Math.sumPrecise` was absent, leaving scripts to use ordinary addition, which
loses small terms and can overflow before cancellation.

## What changed

Phase 369 adds exact binary64 summation of Number values from an iterable, with
one ties-to-even rounding, special-value handling and iterator cleanup.

## Evidence

`python3 tools/conformance/verify_math_sum_precise.py` reports **10,013 numeric
and 8 semantic comparisons, 0 failures** against Python exact rationals and
Node 26 (with the V8 feature flag). The pinned test262 directory improves from
**0/10 to 9/10**; its remaining test contains an unsupported BigInt literal.
Setup and scope are in [Phase 369](../phases/369-an-exact-sum-rounded-once.md).

## Still open

BigInt syntax and the iteration limit at 2^53 inputs remain untested by this
suite. The comparison is not exhaustive over all binary64 lists.
