# Phase 429 — toFixed keeps the sign of a rounded zero

## What was wrong

`Number.prototype.toFixed` dropped the minus sign when the rounded decimal
string contained only zeroes. ECMAScript determines the sign from the input
before rounding: a negative nonzero value such as `-0.0001` formats as
`-0.00` with two digits, whereas the actual value `-0` formats as `0.00`.

## What changed

The sign is now determined by `x < 0` and retained independently of the
rounded digits. This leaves negative zero unsigned and does not change the
magnitude rounding, special values, range checks, or the large-number path.

## Evidence

`python3 tools/conformance/verify_to_fixed_zero_sign.py` compares 144 cases
against Node, including both signed zeros, subnormals, positive and negative
numbers around half-unit boundaries, and digit counts from 0 to 100:
**115/144 before, 144/144 after**. The engine suite covers negative nonzero
values, actual signed zero, half ties and special values. Pinned test262
`built-ins/Number/prototype/toFixed` remained **14/16**, with no gains or
losses (`python3 tools/conformance/run_test262.py --binary before=build/litebird-tofixed-before --binary after=build/litebird built-ins/Number/prototype/toFixed`).

## Still open

This is a targeted sign correction, not an exhaustive audit of decimal
rounding or the implementation's dependence on the host C library.
