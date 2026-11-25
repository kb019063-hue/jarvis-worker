# Phase 421 — the VM binds an array

Task: Phase 259's differential oracle, taking its array-binding refusal.
Finding 421; agent Arena.

## What was wrong

The first witness is `var [a, b] = [1, 2]; String(a)`. The tree-walker
answers `1`, but the bytecode compiler rejected every destructuring
declaration. Indexing a source or draining it ahead of the bindings would be
wrong: a custom iterator and default expressions can have side effects, an
iterator can be infinite, and early termination must close it.

## What changed

`IterationCursor` is shared with the tree-walker. The compiler emits cursor
initialisation, one pull per element (including holes), defaults only for
undefined, a drain for rest, and close. On an abrupt completion the VM closes
open cursors inside out; when `next` itself throws, that cursor is not closed.
The normal close reports a broken `return`. Cursors survive nested calls;
compiled iterator callbacks re-enter the active VM and retain their receiver
instead of running against the tree-walker's unrelated globals. The parser's
function-scoped var names are reserved on function entry, existing slots are
updated, and arguments are normalized before locals are laid out. Anonymous
function defaults receive the binding's name.

The original witness moved atomically from REFUSES to AGREES, followed by 15
other cases. The refusal marker `Destructuring` was redundant with the longer
compiler messages and could hide an unrelated error, so it was removed. The
unit suite starts with the original refusal and separately checks abrupt
closing and a broken normal close. This extends an experimental engine, not the
engine pages run on.

## Verification

- `make verify-bytecode`: **75/75 agree; 2/2 still refused**.
- `make test`: **483/483 engine suites** and 114 cross-origin checks passed.
- `python3 tools/conformance/verify_bytecode_agreement.py --self-test` passed.
- After merging main into the branch, `make deep` passed 1,332 runs,
  `make fuzz` passed, and the new suite passed in an ASan build as its own
  test stretch. The full `make asan` could not complete in this 3.8 GiB
  sandbox: its existing deep-nesting suite exhausted memory even in
  isolation. Full sanitizer CI is required before landing; this is not
  reported as a local pass.

## Still open

Object binding patterns, destructuring assignments, `try`/`catch` and
`for…of` are refused. The VM is not the page engine and does not implement
general closure, lexical TDZ, `this` or full call semantics. Its iterator
callback bridge covers the exercised cases, not a claim of complete JavaScript
function semantics. Function literals are still compiled as constants, so
fresh function identity across repeated evaluations is not addressed here.
No speed improvement or external conformance score is claimed.
