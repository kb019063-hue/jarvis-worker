# Phase 440 — the VM rejects an unresolvable read

Task: the bytecode VM's undeclared read; finding 440; agent Vega.

## What was wrong

`missingName;` raised a ReferenceError in the tree-walker but the experimental
VM returned `undefined`. The difference could propagate as a wrong value:
`'p' + missingName` produced `pundefined`. `typeof missingName` must remain
quiet, but that exception applies to an unresolvable reference, not to every
read inside a `typeof` expression: `typeof (missingName + 1)` and
`typeof missingName.x` throw. Node confirms these cases.

## What changed

`OpGetGlobalChecked` reads the same global map as `OpGetGlobal`, but raises a
JavaScript `ReferenceError` on a miss. The compiler uses it for ordinary name
reads, compound-assignment target reads and updates. Only a bare identifier
operand of `typeof` uses the quiet opcode; its other operands compile normally.
Existing global values, including an explicitly present `undefined`, are not
mistaken for missing bindings. Simple sloppy assignment still creates a global.
The bytecode disassembler consumes and prints the new opcode's name operand.

After main gained for-of support, its empty-loop test exposed another
interaction: a script-scoped `var` in a skipped block or empty loop had never
been inserted into the VM global map. The parser already collects these names;
the compiled script now carries them, and VM entry inserts missing names as
`undefined` without overwriting existing globals. Function-local vars use
their existing local-slot hoisting.

## Verification

`tests/suites/440-undeclared-read-throws.cpp` covers each read site, missing
and declared names, simple assignment, the bare and nested `typeof` cases,
and the disassembly. With the fix stashed, the suite failed on its first
undeclared read; with the fix restored it passed. The tree-walker's `eval`
reports uncaught errors to stderr and returns `undefined`, while the VM raises
`JsThrow` to its caller; the suite checks each path according to its interface.
`make verify-bytecode` reports 75/75 agreeing and 2/2 still refused, and the
normal engine suite reports 492/492 on this branch before merging main.
`make conformance` reports 1973/1973, `make js-conformance` 1153/1158,
`make dom-conformance` 678/678, `make url-conformance` 1188/1188,
`make xml-conformance` 1718/1718, and `make wpt` 9570/9824 subtests
with no unlisted failures. `make diff-trees` and `make diff-xml` report zero
differences; `make deep` passes 1332 runs. `make check-numbers` and
`make check-suites` pass, as does `make fuzz`. These are measurements of this
branch, not a claim that the experimental VM runs the corpus.

The sanitizer build succeeded and the new suite passed under ASan/UBSan.
The full `make asan` did not finish locally: its existing deep-nesting suite
consumed the available 3.8 GiB during the unit run, which was stopped to
avoid stalling the sandbox. Full sanitizer coverage is left to CI; it is not
reported as a local pass.

## Still open

The VM is experimental, not the engine pages run on. General exception
handling (notably `finally`), lexical TDZ, closures and other semantics
remain incomplete; Phase 456 added basic `try`/`catch` on main. The existing bytecode oracle compares returned
values and does not yet compare the tree-walker's reported uncaught errors to
VM throws; the new suite checks those explicitly. No external conformance
score or speed improvement is claimed.

After merging main, `make test` passed 512/512 engine suites and 114 cross-origin checks, including the for-of and exception suites.
