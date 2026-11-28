# Phase 478 — parseInt rounds power-of-two radices once

## What was wrong

`parseInt` accumulated each digit in a binary64 value, rounding after each
multiply and add. Past 53 significant bits those accumulated rounding errors
could move the final answer by one or more ulps. For example,
`parseInt('40000000000003', 16)` returned 18014398509481984 instead of the
correctly rounded 18014398509481988.

## What changed

For radices 2, 4, 8, 16 and 32, the digit run is interpreted as a bit stream.
It keeps 53 leading bits plus guard and sticky bits, then rounds once to
nearest, ties to even; more than 1024 significant bits yields infinity. The
existing prefix handling, sign (including negative zero), digit stopping,
coercion order and decimal conversion remain unchanged. No large integer
allocation is needed.

## Evidence

`python3 tools/conformance/verify_parse_int_power_radices.py` compares both
Litebird and Node with Python's arbitrary-precision integers converted once to
binary64. It covers all five radices, exact ties, negative values, signed zero,
random bit strings, 1024-bit overflow boundaries, prefixes and stopping:
**1,320/1,506 before, 1,506/1,506 after**. The engine suite covers the
regression, ties-to-even, each radix, zero, prefix and overflow behavior.
Pinned test262 `built-ins/parseInt` and `built-ins/Number/parseInt` stayed
**54/56**, with no gains or losses:

```sh
python3 tools/conformance/run_test262.py --binary before=build/litebird-parse-power-before --binary after=build/litebird built-ins/parseInt built-ins/Number/parseInt
```

## Still open

This is not an audit of the rounding behavior of non-power-of-two radices,
other numeric conversions or every possible input string. Decimal keeps its
existing separate conversion path.
