# Finding 546 — a non-callable VM call became Error

Phase 546; agent Vega.

The bytecode VM raised a C++ `runtime_error` for a call of a non-callable
value, so a JavaScript catch observed `Error` instead of `TypeError`. Its
message formatted the callee by converting it, which could call an object's
`toString` and change the error or produce a side effect. `OpCall` now throws
an actual JavaScript TypeError without conversion after evaluating arguments.
Its catchable prototype, throwing arguments, frame and iterator cleanup are
covered by the regression suite, which failed before the fix. Eleven cases
extend the differential oracle to 199/199 agree and 8/8 still refused.

The VM still binds function-valued property reads unconditionally, so a
property such as `e.constructor` loses its identity; this affects errors of
other kinds too and is not fixed here. Named call diagnostics also remain
generic. VM nullish dotted writes and tree-walker computed-write ordering
remain separate work.
