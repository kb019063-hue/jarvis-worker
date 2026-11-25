# Phase 456 — the VM catches exceptions

Task: the Phase 259 bytecode oracle's last original refusal, `try`/`catch`.
Finding 456; agent Arena.

## What was wrong

`try { 1; } catch (e) { 2; }` completes with `1` in the tree-walker but was
refused by the bytecode compiler. Exception handling cannot simply jump over
the handler: an exception can cross a function frame, a reentrant native
callback, or an open iterator, and must restore the right locals and preserve
the thrown value.

## What changed

`OpTry` records the handler's frame, stack and iterator depths and its target;
`OpEndTry` removes it on normal completion and `OpThrow` raises the original
JavaScript value. A caught exception closes cursors opened inside that handler
quietly, keeps outer cursors, restores the frame and stack, and places the
caught value in a fresh local binding. Native exceptions are converted by the
same operation as the tree-walker. A reentrant VM call cannot catch an exception
in its suspended caller until its C++ invocation has unwound. Reacquiring the
frame after an instruction avoids a stale pointer when callbacks grow the frame
vector. Completion tracking makes a script's last statement return the result
of its executed expression statements, including the original witness.

The original refusal is first in the suite and moves atomically into AGREES,
with 18 additional witnesses. The new refused cases protect unsupported forms.

## Verification

- `make verify-bytecode`: **119/119 agree and 5/5 still refused**.
- `python3 tools/conformance/verify_bytecode_agreement.py --self-test`: 6 readings pass.
- `make -j2 all test`: 499 engine suites and 114 cross-origin checks pass;
  two additional assertions were then added and passed in the isolated suite.
- `make deep`: 1,332 runs, 0 failures. The normal HTML, DOM, URL, XML and WPT
  checks and tree/XML differentials pass. JS conformance met its floor but
  recorded 2 timeouts under concurrent load (1,151/1,158); a clean rerun and
  full sanitizer checks are pending.
- `tests/suites/456-the-vm-catches-exceptions.cpp` covers nested catches,
  catch shadowing, native errors, frame and callback unwinding, iterator close,
  garbage collection, completions, and explicit refusals.

## Still open

`finally`, destructuring catch parameters, returns or jumps from a protected
block, and jumps or nested functions from catch are explicitly refused. Nested
functions in catch could capture the catch binding; the VM has no closure
machinery for it. Existing VM operations still diverge independently of the
handler: `try{null.x}catch(e){e.name}` returns `undefined` in the VM rather
than `TypeError`, and a non-callable invocation produces `Error` rather than
`TypeError`. These are not fixed by this phase. More general closure, TDZ and
receiver semantics remain open, and the bytecode VM is not the page engine. No browser conformance or speed
improvement is claimed.
