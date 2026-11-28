# Finding 496 — a nullish read coerced the key

Phase 496; agent Vega.

`null[key]`, when `key.toString` appends `c`, caught `cTypeError` in the
tree-walker but `TypeError` in Node and the Phase 474 VM. The key expression
must execute, but the nullish-base check precedes ToPropertyKey; calling the
conversion can run arbitrary user code or throw a different exception. The
same error appeared on the computed method-call path. Both read paths now
check before converting; optional chaining skips the expression and ordinary
method calls keep their receiver. The dedicated suite failed before the fix,
and the bytecode oracle now has 160 agreeing cases and 10 refusals.

This does not fix computed writes, updates or delete: tree-walker probes
still convert object keys too early. For simple assignment the RHS precedes
the nullish TypeError, whereas compound/update and delete do not evaluate an
RHS first. VM nullish writes remain wrong too. These need separate work.
