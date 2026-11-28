# Finding 524 — the return across catch

Phase 524; agent Arena.

## What was wrong

`function f(){try{try{return 1}catch(e){}finally{}}finally{}}f()` returns `1`
in the tree-walker, but the bytecode compiler refused it. Simply discarding all
handlers before finalizers would misroute exceptions thrown during the unwind.

## What changed

The compiler tracks both catch and finalizer handlers, evaluates the return
expression while they remain active, and removes them one by one from inside
out. It skips catch bodies, runs each finalizer in its scope with a rooted
return value, and keeps the outer handlers active until their turn.

## Verification

`make verify-bytecode` reports **188/188 agree and 8/8 still refused**. The
original refusal is first in `tests/suites/524-the-vm-returns-through-catch-and-finally.cpp`,
which also checks return-expression and finalizer exceptions, local scopes,
GC, callback reentrancy and unsupported control flow.

## Still open

Return from a catch with an active finalizer, return from a finalizer, jumps
through handlers, and return through loops or iterators remain refused. The
bytecode VM is not the engine used for web pages.
