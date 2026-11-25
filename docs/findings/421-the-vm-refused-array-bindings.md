# Finding 421 — the VM refused array bindings

Phase 421; agent Arena.

## What was wrong

`var [a, b] = [1, 2]; String(a)` answered `1` in the tree-walker and was
refused by the bytecode compiler. Neither a single index lookup nor eager
materialisation is a correct general implementation of an iterator binding.

## What changed

The two engines now share the lazy `IterationCursor`. The VM opens, pulls,
drains, and closes it through dedicated opcodes, preserving close-on-abrupt
and not closing a cursor whose `next` throws. Compiled iterator callbacks run
in the active VM, with a receiver, and nested calls preserve the outer cursor.
Function-local var slots are hoisted, call arguments are normalized, and an
anonymous default function receives its binding name. The original case is
first in the new tests and moved out of REFUSES in the same change that puts
it in AGREES.

## Verification

`make verify-bytecode` reports **75/75 agree and 2/2 refused**. `make test`
reports **483/483 engine suites**, including the original case, overridden
iterators, rest, scope, defaults, close-on-throw, and close errors; the
cross-origin suite has 114 passing checks.

## Still open

Object patterns and destructuring assignments are still refused; the two
oracle refusals are `try`/`catch` and `for…of`. General VM closure, lexical
TDZ, receiver and call semantics remain incomplete, as does fresh function
identity across evaluations. This is not an external conformance or speed
claim.
