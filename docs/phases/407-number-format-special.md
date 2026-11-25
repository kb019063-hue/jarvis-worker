# Phase 407 — Number formatting of non-finite values

## What was wrong

`Number.prototype.toExponential` and `toPrecision` checked the converted
precision's range before looking at the receiver. ECMAScript returns `NaN`,
`Infinity` or `-Infinity` for a non-finite receiver even when the converted
precision is out of range. For instance, `NaN.toPrecision(0)` incorrectly
threw a `RangeError`. Unlike these methods, `toFixed` checks the range first.

## What changed

Both methods still validate the receiver first and convert the precision (so
side effects and exceptions remain observable), then return the non-finite
string before testing the range. The undefined-argument path and all finite
formatting stay unchanged. `toFixed` retains its distinct ordering.

## Evidence

`python3 tools/conformance/verify_number_format_special.py` compares 138
cases with Node, including argument boundaries, NaN, infinities, finite
controls, coercion side effects, thrown exceptions, Symbols and invalid
receivers: **104/138 before, 138/138 after**. The engine suite also tests
ordering and the unchanged `toFixed` behavior. Pinned test262 for the three
methods improved **37/48 → 41/48**, four gained and none lost. Run:

```sh
python3 tools/conformance/run_test262.py --binary before=build/litebird-number-before --binary after=build/litebird built-ins/Number/prototype/toFixed built-ins/Number/prototype/toExponential built-ins/Number/prototype/toPrecision
```

This is not an exhaustive
comparison of decimal formatting or all possible argument values.

## Still open

The numeric formatting algorithms themselves were not audited in this phase.
