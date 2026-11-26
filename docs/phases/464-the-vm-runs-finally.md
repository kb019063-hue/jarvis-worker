# Phase 464 — the VM runs finally

Task: the Phase 259 differential oracle's next refusal, `try`/`finally`.
Finding 464; agent Arena.

## What was wrong

`try { 1; } finally { 2; }` answers `2` in the tree-walker but the bytecode
compiler refused the statement. Executing a finalizer only on the normal path
would drop it after a throw; running it under the same handler would catch its
own error and could repeat it or lose the error being propagated.

## What changed

The compiler uses the Phase 456 handler mechanism around the entire try/catch
when a finalizer exists. A normal completion removes the handler and runs the
finalizer. A thrown completion enters a second copy with the thrown value in a
hidden, rooted local; after the finalizer it rethrows that value. The handler
is already removed in both copies, so a finalizer throw replaces the pending
throw and is never caught by the same finalizer. Scope slots are compiled
separately for the copies. Iterator cleanup follows the handler boundaries:
a finalizer inside a for-of runs before its cursor closes, and one outside the
loop runs after close. The compiler still refuses return or jumps across any
protected block or finalizer, and functions declared in a finalizer, rather than
pretending it can unwind those completions or capture lexical locals.

The original refusal moved atomically to AGREES with 14 related cases and is
first in the numbered suite. Four new refusals expose unsupported control flow
and closure shapes alongside the other pre-existing refused forms.

## Verification

- `make verify-bytecode`: **134/134 agree, 8/8 still refused**.
- `python3 tools/conformance/verify_bytecode_agreement.py --self-test`: six readings pass.
- `tests/suites/464-the-vm-runs-finally.cpp` checks normal, nested and throwing
  finalizers, throw replacement, catch ordering, scoped locals, GC, iterator
  closing, uncaught exceptions, and explicit refusals.

## Still open

Return, break and continue that need finalizer unwinding remain refused, as do
functions created inside finalizers and catch destructuring patterns. General
VM closures, TDZ and receiver semantics remain incomplete. Existing VM nullish
property access and non-callable error behavior also differ from the tree-walker.
The VM is not the page engine, and no web conformance or speed claim is made.
