# Finding 493: Date components used C++ argument order

## What was wrong

`time_from_components`, shared by the multi-argument Date constructor and
Date.UTC, converted six component arguments inside the argument list of
`make_time`. C++ does not guarantee left-to-right function-argument evaluation.
In the GCC build, the observable order was year, milliseconds, seconds,
minutes, hours, date, month, instead of year through milliseconds. Later
conversion hooks could run before a month conversion threw, and mutations
by earlier hooks reached later components too late.

## What changed

Each ToNumber operation now has its own declaration, in the order required
by ECMA-262's Date constructor and Date.UTC. Calendar arithmetic receives
only numbers, after all supplied components have been converted. Abrupt
completions stop subsequent conversion; non-finite numeric results do not
skip it. Omitted defaults, two-digit year mapping, clipping and timezone
conversion are unchanged. Only `src/js/BuiltinsDate.cpp` changes in the engine.

## Verification

At base `4ea7bfb`, the 116-check standalone suite disagrees with Node
v22.22.3 on 108 checks. With the fix, Node and Litebird both return `ok 116`.
The same probe also passes under AddressSanitizer/UBSan in an explicit
`-O0 -g0` build (not the default CI optimization configuration).

The same-base, same-tool test262 Date comparison at pinned commit
`2e58f87d8350ad51224617a041c6e041d52f8444` improves **551 → 555 / 610**,
**four fixed and zero lost**. They are exactly `coercion-order.js` and
`coercion-errors.js` under both `built-ins/Date` and `built-ins/Date/UTC`.
See [Phase 493](../phases/493-date-components-convert-in-order.md) for
commands, broader results and validation limits.

## What is still open

Other Date parsing, timezone/DST, invalid-Date setter coercion, missing
Date.prototype[Symbol.toPrimitive] and `$262` realm behavior remain separate.
This finding is closed for constructor/Date.UTC component ordering only.
