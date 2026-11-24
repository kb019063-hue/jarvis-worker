# Phase 284 — a number rounded to binary16

## What was wrong

`Math.f16round` was absent: `typeof Math.f16round` was `undefined`, so scripts
could not request IEEE 754 binary16 rounding. Node.js 24 answers `function`.
The test262 `built-ins/Math/f16round` directory was **0/5** on the old binary.

## What changed

`Math.f16round` converts its argument once and rounds the resulting Number
directly from binary64 to the nearest binary16 value, ties to even, returned as
a Number. It preserves negative zero, NaN and infinities; handles subnormals
and the 65520 overflow midpoint; and never first rounds to binary32, which can
round a value on the wrong side of a halfway point. It is a non-constructible,
non-enumerable, writable and configurable method named `f16round`, length 1.

## Evidence

```sh
make -j2 all
npm install --prefix build/node-reference node@24
python3 tools/conformance/verify_math_f16round.py
python3 tools/conformance/run_test262.py built-ins/Math/f16round
make test
```

Node v24.22.3 and Python's `struct` binary16 format agree with Litebird on
**263,955 comparisons, 0 failures**. These include every positive finite
binary16 value and every midpoint to the next value (including the overflow
boundary), the adjacent binary64 numbers on both sides, both signs, 10,000
seeded binary64 bit patterns, infinities and NaN. The test262 directory is
**0/5 before, 5/5 after**, with zero losses. Across `built-ins/Math`, test262 went **311/327 → 316/327**
(+5, −0) using `python3 tools/conformance/run_test262.py --binary
before=build/litebird-f16-before --binary after=build/litebird built-ins/Math`.
The dedicated suite covers
coercion order, signed zero, ties, overflow, descriptor and constructibility;
`make test` passed **419 engine suites and 114 cross-origin checks**.

The oracle tests representable values and rounding boundaries exhaustively,
not every binary64 input. Node and Python are independently implemented client
references; the result comparison treats NaNs as NaNs rather than comparing
payload bits.

## Still open

`Float16Array` and DataView's binary16 accessors are separate features and were
not added. Other Math methods, including `Math.sumPrecise`, are outside this
phase. No claim is made about every host floating-point environment.
