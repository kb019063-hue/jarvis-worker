# Finding 540 — the catch return behind finally

Phase 540; agent Arena.

## What was wrong

`function f(){try{throw 1}catch(e){return e}finally{}}f()` returns `1` in the
tree-walker, but the bytecode compiler refused it. Catch bodies have left their
own handler, yet their surrounding finally handler must still run on return.

## What changed

The compiler applies its existing inside-out handler unwind to returns from
catch bodies. It evaluates the return before finalizers, removes locals that
are out of scope at each boundary and roots the pending value, while leaving
outer handlers installed for throws.

## Verification

`make verify-bytecode` reports **218/218 agree and 8/8 still refused** after
merging Phase 546's eleven independent witnesses (207/207 before that merge).
The original case is first in `tests/suites/540-the-vm-returns-from-catch-through-finally.cpp`,
which also checks nested catches and finalizers, exceptions, scopes, GC,
callbacks and explicit refusals.

## Still open

Return through loops or iterators, return from a finalizer, jumps through
handlers, and closures over catch/finalizer locals remain refused or incomplete.
The bytecode VM is not the engine used for web pages.
