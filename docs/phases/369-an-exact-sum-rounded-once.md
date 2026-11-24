# Phase 369 — an exact sum rounded once

## What was wrong

`Math.sumPrecise` was missing. Ordinary addition rounds after each term, so
`[1e16, 1, -1e16]` sums to zero with a naive loop rather than one. The pinned
test262 `built-ins/Math/sumPrecise` directory was **0/10** before this change.

## What changed

`Math.sumPrecise(iterable)` visits values in iterator order, requires Number
values without coercion, and adds finite values as exact integers in units of
2^-1074. Separate positive and negative fixed-size accumulators hold all
finite binary64 values even if an intermediate sum exceeds the floating-point
range; after subtraction the result is rounded once, nearest ties-to-even.
The size covers the specified maximum of fewer than 2^53 input values. Empty
input and only negative zeros give negative zero; cancellation and any
positive zero give positive zero. NaN and signed infinities follow the
standard, but do not stop iteration or hide a later invalid value. An abrupt
value closes a custom iterator; an abrupt `next()` is propagated without a
close. The new method is non-constructible, writable, non-enumerable and
configurable, with name `sumPrecise` and length 1.

## Evidence

```sh
make -j2 all
npm install --prefix build/node-reference node@26
python3 tools/conformance/verify_math_sum_precise.py
python3 tools/conformance/run_test262.py built-ins/Math/sumPrecise
make test
```

Node v26.13.1 needs `--js-sum-precise`, which the verifier supplies. It and
Python's exact `Fraction` independently agree on **10,013 numeric cases,
0 failures**, including cancellation, subnormals, overflow, NaN, infinities
and 10,000 seeded random binary64 lists. Eight additional method/iterator
comparisons agree with Node. This is sampling, not an exhaustive comparison
of all lists or an exercised 2^53-element limit.

The pinned test262 directory went **0/10 → 9/10**, with no losses. Its one
remaining case cannot parse because the engine lacks BigInt literals; other
non-Number values and the required iterator close are independently tested.
Across `built-ins/Math`, the score was **316/327 → 325/327**, +9/−0.
`make test` passed **432 engine suites and 114 cross-origin checks**.

## Still open

BigInt literals remain unsupported, including the literal in the tenth
case. Iterator behavior inherits the general `js_iterate` implementation and
its documented fast paths; the huge input limit is not tested by exhaustion.
This phase does not add Float16Array or DataView binary16 accessors.
