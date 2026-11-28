# Phase 524 — the VM returns through catch and finally

Task: the Phase 259 bytecode oracle's next refusal, a return through nested
catch/finally handlers. Finding 524; agent Arena.

## What was wrong

`function f(){try{try{return 1}catch(e){}finally{}}finally{}}f()` answers `1`
in the tree-walker but the compiler refused it. A return must leave the catch
handler without calling it and still execute both finalizers. An exception
while evaluating the return expression must instead reach the still-active
catch, and an exception in the inner finalizer must reach an outer handler,
not the catch belonging to the same try.

## What changed

The compiler records catch and finalizer handlers in runtime nesting order.
A return from a protected try body evaluates its expression first with all
handlers active, then removes handlers inside out. A catch is discarded; a
finalizer runs with outer handlers still installed and the pending return value
rooted on the VM stack. Lexical locals are trimmed at each boundary. The
compiler restores its own state after emitting this path for other branches.
No VM opcode or runtime exception-handler change was needed.

The original refusal and two related protected-return cases moved atomically
from REFUSES to AGREES; 14 further witnesses exercise exceptions, scopes,
rooting and reentrant calls. Earlier suites no longer assert those three
cases are refused; a return from a catch through a finalizer is still refused.

## Verification

- `make verify-bytecode`: **188/188 agree, 8/8 still refused**; the oracle's
  `--self-test` passes six readings.
- `make -j2 all test`: **560 engine suites and 114 cross-origin checks pass**.
  Suite 524 puts the original case first and checks the ordering, error
  precedence, local scope, GC, callbacks, and retained refusals.
- A 48-case product of return expressions and nested catch/finally shapes
  agreed between the tree-walker and bytecode VM.
- HTML tree construction was 1973/1973, JS 1153/1158, DOM 678/678,
  URL 1188/1188 and XML 1718/1718. WPT was 9570/9824, with all 254 failures
  listed. Tree and XML differential checks passed; `make fuzz` found nothing,
  and `make deep` passed 1332 runs at depth 200000.
- ASan/UBSan build and oracle passed (188/188 and 8/8); a 49-suite sanitized
  stretch including the new and earlier VM exception/finalizer suites passed.
  Full `make asan TEST_OPT=-O0` was terminated after 480 seconds in the existing
  `deep_nesting_never_exhausts_the_native_stack` suite; it is not a full pass.

## Still open

Return from a catch with an active finalizer, return from a finalizer, jumps
through handlers, and returns that must also unwind loops or iterators remain
refused. A plain return from a catch without an active handler was already
supported. General VM closure, TDZ, receiver and call semantics remain
incomplete. Finalizer bodies are emitted inline and may hit bytecode size
limits. The VM does not run pages; no browser-conformance or speed claim is made.
