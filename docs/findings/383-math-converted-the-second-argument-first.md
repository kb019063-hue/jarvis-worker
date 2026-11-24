# 383. Math converted the second argument first

## What was wrong

On the GCC build, `Math.atan2` and `Math.pow` converted the second argument
before the first, because the conversions were unsequenced C++ arguments.
Observable script effects and which exception won were wrong.

## What changed

Phase 383 sequences the two conversions explicitly before calling the numeric
operation, so a throw or mutation in the first argument is observed in order.

## Evidence

`python3 tools/conformance/verify_math_conversion_order.py` compares against
Node.js: **8/22 before, 22/22 after**. Test262 `built-ins/Math` stayed
**325/327**, zero gained or lost. See
[Phase 383](../phases/383-math-converts-left-before-right.md).

## Still open

Other built-in conversion sites and the methods' complete numeric edge cases
were not exhaustively audited.
