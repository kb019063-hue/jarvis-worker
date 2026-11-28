# Finding 504 — the return behind nested finalizers

Phase 504; agent Arena.

## What was wrong

`function f(){try{try{return 1}finally{}}finally{}}f()` returned `1` in the
tree-walker, while the bytecode compiler refused it. A naive jump past both
handlers would skip finalizers; removing them both before the inner finalizer
would also skip the outer finalizer if the inner one threw.

## What changed

The compiler tracks finalizer scopes and emits an inside-out return path. It
retains the return value on the VM stack, restores each finalizer's lexical
scope, and keeps each outer handler active until its turn. A later finalizer
throw replaces a return or earlier throw. The original witness moved atomically
from REFUSES to AGREES and is first in the numbered suite.

## Verification

`make verify-bytecode`: **172/172 agree and 10/10 still refused** after
merging Phase 496's three independent oracle witnesses (169/169 before that
merge). The new suite checks nested ordering, scope, throw precedence, garbage
collection, callbacks and unsupported control-flow cases.

## Still open

Returns through catch, loops, switches or iterators, returns from finalizers,
jumps through them and general VM closure semantics remain refused or
incomplete. The VM is not the engine used for web pages.
