# Phase 552 — the VM rejects nullish dotted writes

Task: the bytecode differential-oracle row's nullish dotted-write defect;
finding 552; agent Vega.

## What was wrong

`try { null.x = 1 } catch (e) { e.name }` answers `TypeError` in Node
and the tree-walker, but the VM silently completed the store. Just checking
`OpSetProperty` would not suffice: compiling the RHS before the base reverses
observable side effects, or incorrectly runs an RHS when the base throws.
For a nullish but successfully evaluated base, however, the RHS must run
before the store throws; an RHS exception takes precedence.

## What changed

Simple dotted assignments compile the base first and leave it rooted on the
VM stack while evaluating the RHS. The new `OpSwap` puts the values in the
existing `[value, base]` order for `OpSetProperty`; the latter raises the
same JavaScript TypeError and diagnostic as the tree-walker when the base is
nullish. Existing VM exception handling unwinds frames and cleanup. Other
assignment targets keep their existing paths.

## Verification

The new suite first failed against the unmodified VM and passes with the fix.
Node v22.22.3 independently checked the differential traces: null and
undefined, exact diagnostics, base and RHS effects and exceptions, nullish
compound writes and updates, ordinary and nested assignments, frame and
finalizer unwinding, and iterator cleanup. The suite also checks an uncaught
JavaScript throw and the TypeError prototype. All 16 differential traces
matched Node. Thirteen cases extend `make verify-bytecode` to 231/231
agreements and 8/8 still refused after Phase 540. After merging Phases 547
and 553, `make test` passed 586 suites and 114 cross-origin checks. HTML conformance
was 1973/1973, JS 1154/1158 and everyday 90/90, DOM 678/678, URL
1188/1188, XML 1718/1718, and WPT 9570/9824 with no unlisted failures.
`diff-trees`, `diff-xml`, `check-numbers`, `check-suites` and `check_docs.py`
passed. An earlier test run was killed during the deep-nesting suite while
concurrent conformance checks were running; the subsequent isolated run passed.
Full sanitizer and deep checks are required in CI.

## Still open

Compound and update dotted writes on non-nullish bases re-evaluate their base,
so a side-effecting base may be evaluated twice or select a different object.
Non-nullish computed-write key conversion and broader function-valued
property-read identity remain separate work. This phase makes no external
conformance improvement claim.
