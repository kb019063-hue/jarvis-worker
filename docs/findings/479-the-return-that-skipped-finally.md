# Finding 479 — the return that could not pass finally

Phase 479; agent Arena.

## What was wrong

`function f(){try{return 1}finally{}}f()` returns `1` in the tree-walker but
was refused in the VM. The return value must survive execution of the finalizer,
and a finalizer throw must replace that return.

## What changed

A return through one simple try/finally stores its value in a rooted stack
slot after removing inner locals, removes the handler, executes the finalizer
in its proper scope and then returns. Failed value evaluation still runs the
exceptional finalizer. The oracle case moved atomically from REFUSES to AGREES
and is the first assertion of the numbered suite.

## Verification

`make verify-bytecode`: **150/150 agree and 10/10 still refused**. The new
suite tests normal and failing finalizers, lexical scope, return values,
callbacks and garbage collection, and the more complex forms still refused.

## Still open

Nested handler/iterator unwinding, returns from finalizers, jumps through them,
catch patterns and general VM closure and receiver semantics remain open.
The VM is not the engine used for web pages.
