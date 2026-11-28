# Finding 552 — the VM silently accepted a nullish dotted write

Phase 552; agent Vega.

The VM's `OpSetProperty` accepted `null.x = 1` and `undefined.x = 1`
without throwing, while Node and the tree-walker raise a catchable TypeError.
Also, the compiler evaluated a simple dotted assignment's RHS before its
base, so adding a nullish check alone would not preserve exception or side
-effect order. The compiler now evaluates and roots the base first and the VM
checks it at the store, after the RHS finishes. The suite failed before the fix.

Compound dotted assignments and updates still evaluate a non-nullish base more
than once, and computed writes have separate ordering issues. This change does
not claim to fix those or the function-valued property-read identity defect.
