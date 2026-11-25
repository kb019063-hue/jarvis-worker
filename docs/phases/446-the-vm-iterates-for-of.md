# Phase 446 — the VM iterates for-of

Task: the Phase 259 bytecode differential oracle's `for…of` refusal.
Finding 446; agent Arena.

## What was wrong

The first witness is `var s = ""; for (var k of [1,2]) { s += k; } String(s)`:
the tree-walker answers `12`, and the bytecode compiler refused the statement.
A loop cannot be implemented by draining the iterable up front: `next`, the
body and `return` have observable ordering, and an iterator can be infinite.

## What changed

The compiler handles synchronous `for…of` whose head is a simple `var` or
assignment identifier. It opens the Phase 421 cursor, steps once per iteration
with `OpIteratorStep`, binds the value, and jumps back. Exhaustion is distinct
from a yielded `undefined`. A `break` closes the iterator; a `continue` goes
back without closing; a `return` closes each enclosing for-of inside out. A
throw in the body quietly closes while preserving the original throw. A failing
iterator step, including a failing `done` or `value` getter, is abandoned rather
than closed, matching `js_iterate`. Loop-exit jumps remove intervening local
slots before leaving the body. Compiled callbacks keep using the VM bridge from
Phase 421.

The shared cursor previously used the size of an array's stored elements as
its length. `for…of` instead reads the current `length` on every step, so it
must visit holes beyond storage and observe length changes in the body. The
cursor now reads that length and uses `js_array_get`, also correcting the
shared destructuring path for a sparse index with an accessor. An invalid
iterator result carries the same error text as `js_iterate` for the loop.

The original case moved atomically from REFUSES to AGREES, followed by 24
additional witnesses; the suite starts with that case and also checks abrupt
close, step failure and explicit refusals.

## Verification

- `make verify-bytecode`: **98/100 agree; 1/1 still refused**.
- `make test`: **495/495 engine suites**; 114 cross-origin checks pass.
- `python3 tools/conformance/verify_bytecode_agreement.py --self-test` passes.

## Still open

`for…in`, `for await…of`, `let`/`const` iteration bindings and destructuring or
property targets in loop heads are explicitly refused, rather than silently
compiled without fresh environments or reference semantics. General VM
closures, TDZ and call semantics remain incomplete; `try`/`catch` is the one
original oracle refusal left. The VM is not the page engine. No external
conformance or speed improvement is claimed.
