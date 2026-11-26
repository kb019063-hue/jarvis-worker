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

The original refusal moved atomically to AGREES with 16 related cases and is
first in the numbered suite. Four new refusals expose unsupported control flow
and closure shapes alongside the other pre-existing refused forms.

## Verification

- `make verify-bytecode`: **136/136 agree, 8/8 still refused**.
- `python3 tools/conformance/verify_bytecode_agreement.py --self-test`: six readings pass.
- After merging main, `make -j2 test` passed 517 engine suites and 114
  cross-origin checks. The two additional witnesses exercise Phase 440's
  unresolvable-read error through finally.
- Before merging main, `make deep`: 1,332 runs, 0 failures. HTML, JS, DOM, URL and XML corpora,
  WPT, and tree/XML differentials pass; JS is 1,153/1,158 with no timeouts.
  `make fuzz` passed.
- Before merging main, the new and previous bytecode exception suites passed
  as isolated ASan/UBSan stretches. Full local `make asan` was stopped after
  150 seconds in the existing deep-nesting suite. After merging main, rebuilding
  the sanitizer test suite was killed by the sandbox memory limit even at
  `-j1`; the merged tree requires full sanitizer CI.
- `tests/suites/464-the-vm-runs-finally.cpp` checks normal, nested and throwing
  finalizers, throw replacement, catch ordering, scoped locals, GC, iterator
  closing, uncaught exceptions, and explicit refusals.

## Still open

Return, break and continue that need finalizer unwinding remain refused, as do
functions created inside finalizers and catch destructuring patterns. General
VM closures, TDZ and receiver semantics remain incomplete. Existing VM nullish
property access and non-callable error behavior also differ from the tree-walker.
The VM is not the page engine, and no web conformance or speed claim is made.
