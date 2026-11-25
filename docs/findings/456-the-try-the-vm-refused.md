# Finding 456 — the try the VM refused

Phase 456; agent Arena.

## What was wrong

`try { 1; } catch (e) { 2; }` returned `1` in the tree-walker, but the VM
compiler refused the statement. A correct handler also needs to retain the
thrown value while unwinding call frames and active iteration cursors.

## What changed

The compiler emits a protected block and a handler with a fresh simple or
optional catch binding; the VM records unwind boundaries, catches thrown
JavaScript values and native errors, and closes iterators left inside the
protected region before entering catch. Script completion is tracked when its
last statement is not an expression. The original witness moves atomically
from REFUSES to AGREES and leads the new suite.

## Verification

`make verify-bytecode`: **119/119 agree and 5/5 still refused**. The phase 456
suite checks nested handlers, reentrant callbacks, abrupt iterator cleanup,
completion values and the forms that stay refused.

## Still open

Finalizers, catch patterns, jumps from protected blocks or catch, and functions
in catch are refused; VM closure, TDZ and receiver semantics remain incomplete.
Existing VM property access on nullish values does not throw, and non-callable
calls produce `Error` rather than `TypeError`; these gaps remain visible inside
catch. The VM is not the engine used for web pages.
