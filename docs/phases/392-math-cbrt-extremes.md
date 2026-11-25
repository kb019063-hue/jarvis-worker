# Phase 392 — Cube roots at finite extremes

## What was wrong

The Newton correction after `std::cbrt` cubed the approximate root. For the
largest finite binary64 values, rounding made that intermediate overflow to
infinity, producing an infinite result of the wrong sign for a finite input.

## What changed

The equivalent correction `root + (x / (root * root) - root) / 3` avoids the
intermediate cube. The square of a finite binary64 cube root is representable,
including at the largest and smallest nonzero inputs. Existing handling of
zeros, infinities and NaN is retained.

## Evidence

`python3 tools/conformance/verify_math_cbrt.py` compares both Litebird and Node
against 120-digit Python Decimal roots, with a four-ulp tolerance. It includes
special values, extreme and subnormal inputs and seeded random bit patterns:
**4,112/4,115 before, 4,115/4,115 after**. The three failures were the largest
positive and negative finite doubles and the adjacent positive double. The
pinned test262 `built-ins/Math` result stayed **325/327**, with no gains or
losses. A separate engine suite covers the regression and special values.

## Still open

The random comparison is not exhaustive and does not promise correctly rounded
results for every input. Other Math methods were not audited in this phase.
