# 289. No binary16 rounding in Math

## What was wrong

`Math.f16round` did not exist; the five pinned test262 tests for it failed.

## What changed

Phase 284 adds direct binary64-to-binary16 rounding with ties to even, preserving
signed zero and special values, and exposes the standard method properties.

## Evidence

`python3 tools/conformance/verify_math_f16round.py` reports **263,955
comparisons, 0 failures** against Node 24 and Python's binary16 packer.
`python3 tools/conformance/run_test262.py built-ins/Math/f16round` reports
**0/5 before, 5/5 after**. Setup and coverage are in
[Phase 284](../phases/284-a-number-rounded-to-binary16.md).

## Still open

Float16Array, DataView binary16 methods and other missing Math functionality
are separate work; the differential check is not exhaustive over binary64.
