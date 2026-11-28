# Finding 489: Date.toJSON bypassed the receiver

## What was wrong

The native `Date.prototype.toJSON` implementation read the Date slot and
formatted it itself. A user override of `toISOString` was never called,
and borrowing the method for an ordinary object threw TypeError. That
contradicts ECMA-262 §21.4.4.37's generic conversion-and-invocation algorithm.
It also ignored numeric conversion hooks on both valid and invalid Dates.

## What changed

`src/js/BuiltinsDate.cpp` now boxes the receiver, converts with the number
hint, returns null for a non-finite Number conversion result, and invokes
the receiver's `toISOString` with no arguments. Property access happens
only after conversion; its exceptions and the method's result propagate.
The JSON serializer and other Date methods are unchanged.

## Verification

`make -j2 test build/litebird`: **524/524 suites** and **114/114 cross-origin
checks** pass. The 39 checks in
`tests/suites/489-date-tojson-is-generic.cpp` match Node v22.22.3; the old
implementation fails 24 of them. See
[Phase 489](../phases/489-date-tojson-is-generic.md) for exact reproduction
commands and the same-tree before/after test262 comparison: **587 → 595
of 677** Date/JSON.stringify tests, **8 fixed and 0 regressions**; all
**13/13** tests of `Date.prototype.toJSON` now pass, up from 5/13.

## What is still open

Other Date failures and JSON.stringify's 23 recorded failures remain.
Shared conversion-helper behavior, realm support, calendar parsing and
local-time formatting are not changed. This finding is closed for the
Date.toJSON dispatch defect, not for the entire Date API.
