# Phase 504 — the VM returns through nested finalizers

Task: the Phase 259 bytecode oracle's next refusal, return through nested finally.
Finding 504; agent Arena.

## What was wrong

`function f(){try{try{return 1}finally{}}finally{}}f()` answers `1` in the
tree-walker but the bytecode compiler refused it. Returning through nested
finalizers must run them inside out: if the inner finalizer throws, the outer
one still runs, and its own throw may replace the inner one.

## What changed

The compiler records active try/finally boundaries from outermost to innermost.
For a return whose active handlers are all simple finalizers, it evaluates the
return expression with all handlers active, then removes one handler at a time,
trims locals to that finalizer's lexical scope and runs it with the pending
return value in a hidden rooted slot. If a finalizer throws, outer handlers are
still active and run their exceptional paths. After the last finalizer, the VM
returns the saved value. The compiler restores its own scope and handler state
after emitting that path for other branches. Existing restrictions on catch
handlers, loops, switches and returns inside finalizers are retained.

The original refusal moved atomically into AGREES with 11 related witnesses
and is first in the numbered suite. A new refusal records an intervening catch
handler; the two earlier suites' refusal checks now use that unsupported case.

## Verification

- `make verify-bytecode`: **169/169 agree, 10/10 still refused**.
- `python3 tools/conformance/verify_bytecode_agreement.py --self-test`: six readings pass.
- `tests/suites/504-the-vm-returns-through-nested-finalizers.cpp` checks
  ordering, scope shadowing, exceptions replacing returns, GC, callbacks and
  explicitly refused forms.

## Still open

Returning through a loop, iterator, switch or catch handler, returning from a
finalizer, and break/continue through finalizers are still refused. General VM
closure, TDZ, receiver and call semantics remain incomplete. Finalizer bodies
are emitted inline, so deeply nested cases may hit existing bytecode size
limits. The VM does not run pages; no browser conformance or speed claim is made.
