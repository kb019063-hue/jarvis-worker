# Phase 540 — the VM returns from catch through finally

Task: the Phase 259 bytecode oracle's next refusal, return from a catch through
finally. Finding 540; agent Arena.

## What was wrong

`function f(){try{throw 1}catch(e){return e}finally{}}f()` answers `1` in the
tree-walker but the bytecode compiler refused it. A catch handler has already
been removed when its body runs, but a surrounding finally handler remains
active. A return must evaluate the caught value first, then run finalizers
without keeping catch bindings in scopes that cannot see them.

## What changed

The compiler now uses Phase 524's existing ordered handler unwind for returns
from catch bodies as well as try bodies. Each active handler is removed in
turn; pending results stay rooted while finalizers execute, and lexical locals
are trimmed to the scope of each boundary. Outer handlers remain active so
that a finalizer's throw can replace the return and be caught outside it.
Returns through loops/iterators and returns from finalizers are still refused.
No VM opcode or runtime exception-handler change was required.

The original refusal moved atomically into AGREES with 18 witnesses. The old
suite assertions that expected this case to be refused were removed; a new
refusal records catch-return cleanup through a for-of iterator.

## Verification

- `make verify-bytecode`: **207/207 agree, 8/8 still refused**; six oracle
  self-test readings pass.
- `make -j2 all test`: **573 engine suites and 114 cross-origin checks pass**.
  Suite 540 puts the original refusal first and checks catch binding scope,
  nested catches, throws, finalizer order, GC, callbacks and refused forms.
- A 180-case product of return expressions, nested handlers and finalizers
  agreed between tree-walker and VM.
- HTML tree construction was 1973/1973, JS 1154/1158, DOM 678/678,
  URL 1188/1188 and XML 1718/1718. WPT was 9570/9824, with all 254 failures
  listed. Tree and XML differential gates passed; `make fuzz` found nothing,
  and `make deep` passed 1332 runs at depth 200000.
- The ASan/UBSan build and oracle passed (207/207 and 8/8); a 62-suite
  sanitized stretch including this and earlier VM exception/finalizer suites
  passed. Full `make asan TEST_OPT=-O0` was terminated after 480 seconds in
  the existing `deep_nesting_never_exhausts_the_native_stack` suite; it is not
  a full pass.

## Still open

Returns that must also unwind loops, switches or iterators, returns from
finalizers, and jumps through handlers remain refused. Catch binding patterns,
closures capturing catch/finalizer locals, and general VM closure, TDZ,
receiver and call semantics remain incomplete. Inline finalizers may hit
bytecode size limits. The VM does not run pages; no browser-conformance or
speed claim is made.
