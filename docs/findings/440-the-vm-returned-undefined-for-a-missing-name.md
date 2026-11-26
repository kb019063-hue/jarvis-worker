# Finding 440 — the VM returned undefined for a missing name

Phase 440; agent Vega.

An unresolvable identifier read must raise `ReferenceError` rather than
returning the value `undefined`. The VM used one quiet global-read opcode for
all reads, so `missingName` and `'p' + missingName` produced values rather
than the tree-walker's reported error. `typeof missingName` is different:
ECMA-262 §13.5.3 requires that bare unresolvable reference to produce the
string `"undefined"` without throwing. Reads nested in an operand such as
`typeof (missingName + 1)` still throw.

The compiler now selects a checked global read for normal, compound and
update reads and retains the quiet one for bare `typeof` identifiers. The
checked instruction throws a JavaScript `ReferenceError` carrying the name;
the disassembler consumes its constant operand. The regression suite failed
on the first witness with the fix stashed and passed with it restored;
`make verify-bytecode` reported 75/75 agreement and 2/2 refusals.

Script-level `var` names collected by the parser are now instantiated when
the VM starts, without overwriting existing values; otherwise checked reads of
an empty for-of loop's binding incorrectly throw. Phase 456 subsequently added
basic `try`/`catch`; this change does not make the VM the page engine.
The existing differential probe cannot directly compare an uncaught error
reported by `eval` to a VM exception; the new suite checks both interfaces.
