# Phase 500 — Math.hypot retains small contributions

## What was wrong

For three or more arguments, `Math.hypot` scaled the squares to avoid
intermediate overflow but accumulated them with ordinary addition. Many small
squares following a large one could each round away. For example, one followed
by 100 copies of `1e-8` returned exactly 1, even though their contribution is
about 5e-15. Results also varied with argument order.

## What changed

The scaled squares now use Kahan compensated summation. The existing
left-to-right conversion of all arguments, infinity/NaN precedence, positive
zero, scaling, and the two-argument `std::hypot` path are unchanged. This
reduces error; it does not promise a correctly rounded result for every input.

## Evidence

`python3 tools/conformance/verify_math_hypot_accuracy.py` grades Litebird and
Node separately against 140-digit Python Decimal sums and square roots, with a
two-ulp tolerance. It covers 279 inputs: mixed magnitude and argument order,
seeded random vectors, underflow/overflow, signed zero, NaN and infinities.
**249/279 before, 279/279 after** passed for both clients. The standalone suite
also checks conversion side effects and exceptions. Pinned test262
`built-ins/Math/hypot` remained **12/12**, with no gains or losses:

```sh
python3 tools/conformance/run_test262.py --binary before=build/litebird-hypot-before --binary after=build/litebird built-ins/Math/hypot
```

## Still open

This sampled comparison is not a proof of correct rounding or a complete audit
of the effects of scaling and multiplication on floating-point accuracy.
