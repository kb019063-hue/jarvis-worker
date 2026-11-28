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

- `make verify-bytecode`: **169/169 agree, 10/10 still refused** before
  merging main; **172/172 agree, 10/10 refused** afterward (Phase 496 added
  three independent witnesses). The oracle's six self-test readings pass.
- `make -j2 all test` after merging main: **544 engine suites and 114
  cross-origin checks pass**. The numbered suite checks ordering, scope
  shadowing, exceptions replacing returns, GC, callbacks and refused forms.
- Before merging main, HTML tree construction was 1973/1973, JS 1153/1158,
  DOM 678/678, URL 1188/1188, XML 1718/1718 and WPT 9570/9824 with all 254
  failures listed. Both tree and XML differential checks passed; `make fuzz`
  found nothing, and `make deep` passed all 1332 runs at depth 200000.
- After merging main, the ASan/UBSan oracle passed 172/172 with 10/10
  refusals, and a 16-suite sanitized stretch including both return/finally
  suites passed. Full `make asan TEST_OPT=-O0` did not finish: a 1200-second
  limit terminated it in the existing `deep_nesting_never_exhausts_the_native_stack`
  suite after the sanitizer rebuild. This is not a full sanitizer pass.

## Still open

Returning through a loop, iterator, switch or catch handler, returning from a
finalizer, and break/continue through finalizers are still refused. General VM
closure, TDZ, receiver and call semantics remain incomplete. Finalizer bodies
are emitted inline, so deeply nested cases may hit existing bytecode size
limits. The VM does not run pages; no browser conformance or speed claim is made.
