# Phase 479 — the VM returns through finally

Task: the Phase 259 bytecode oracle's next refusal, a return through finally.
Finding 479; agent Arena.

## What was wrong

`function f(){try{return 1}finally{}}f()` answers `1` in the tree-walker,
while the bytecode compiler refused the return. A return cannot just jump out:
its value is evaluated before the finalizer, and a finalizer throw replaces it.
The finalizer also sees the scope outside the try, not the try body's locals.

## What changed

For a return in one simple try/finally, with no enclosing catch, nested handler
loop or switch, the compiler evaluates the value with the handler still active. It
moves that value over any inner block locals, removes the handler, and compiles
the finalizer with the try-entry locals plus a hidden stack slot holding the
return value. That slot keeps the value rooted through calls and garbage
collection. A normal finalizer returns it; a throw in the finalizer escapes
instead. A throw evaluating the return expression still goes through the
ordinary exceptional finalizer path. Compiler scope state is restored after
emitting the return path, so other branches are compiled normally.

The original refusal moved atomically to AGREES with 13 more witnesses and is
first in the new suite. Three additional REFUSES witnesses keep more complex
completion unwinding explicit.

## Verification

- `make verify-bytecode`: **150/150 agree, 10/10 still refused**.
- `python3 tools/conformance/verify_bytecode_agreement.py --self-test`: six readings pass.
- `make -j2 all test`: 524 engine suites and 114 cross-origin checks pass.
- HTML, JS, DOM, URL and XML corpora, WPT, and tree/XML differentials pass.
  A 56-case combination probe for returns, scopes and finalizers agrees.
- `make fuzz` passes. An ASan/UBSan build with the suite compiled at `-O0`
  passed the isolated 13-suite stretch and all **150/150** bytecode oracle
  cases; deep and full sanitizer checks remain in progress.
- `tests/suites/479-the-vm-returns-through-finally.cpp` checks evaluation
  order, scope cleanup, returned objects, GC, throws, callbacks and refusals.

## Still open

Nested handlers or finalizers, returns through a loop, switch, iterator or a
try/catch/finally, returns from the finalizer itself, and break/continue through
finalizers are explicitly refused. Catch patterns and general closure, TDZ,
receiver and other VM semantics remain incomplete. The VM is not the page
engine; no browser conformance or performance improvement is claimed.
